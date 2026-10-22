module;

#include <bhop/pch.hpp>

export module bhop:thread_rings;

export import :cache_traits;

import :spsc_ring;


export namespace bhop
{
   /**
    * @brief Many producers, one consumer, no contention: every producer thread pushes into its own \c SpscRing.
    *
    * A thread finds its ring through a one-entry thread-local cache. Its first push, or its first after pushing into another \c ThreadRings of the
    * same type, registers or looks up its ring under a lock. Rings are owned by this object, never by the threads, so values pushed by a thread that
    * has since exited are still consumed. They are freed with this object.
    *
    * Order: exact within each producer thread. Across threads, \c drain( consume ) empties one thread's ring after another, while
    * \c drain( consume, key ) merges them by ascending key (e.g. a timestamp) among the values published when the consumer looks; a value published
    * slightly later with a smaller key can still come after a larger one.
    *
    * @warning Any number of threads may push; at most one thread may consume at a time.
    * @note Memory: one ring of \p Capacity slots per thread that has ever pushed.
    *
    * @tparam T         Value type. Must be default-constructible; slots are overwritten in place by the producer's fill.
    * @tparam Capacity  Number of slots in each thread's ring.
    * @tparam Alignment Alignment of each slot.
    */
   template <std::default_initializable T, std::size_t _capacity, std::size_t alignment = traits::cache_line_size_v> class ThreadRings final
   {
      using ring_type = SpscRing<T, _capacity, alignment>;

      struct Lane
      {
         explicit Lane( std::thread::id thread ) noexcept
            : owner{ thread }
         { }

         ring_type ring{};
         std::thread::id const owner;
         Lane* next{ nullptr }; // set before the lane is published, never changed afterwards
      };

   public:
      using value_type = T;
      static constexpr auto capacity = ring_type::capacity;
      static constexpr auto slot_size = ring_type::slot_size;

      ThreadRings( ) = default;
      ThreadRings( ThreadRings const& ) = delete;
      ThreadRings( ThreadRings&& ) = delete;
      auto operator=( ThreadRings const& ) -> ThreadRings& = delete;
      auto operator=( ThreadRings&& ) -> ThreadRings& = delete;

      /**
       * Producer side, any thread. Pushes into the calling thread's ring.
       * @return false, without calling \p fill, when that ring is full.
       */
      template <std::invocable<T&> F> auto try_push( F&& fill ) noexcept( std::is_nothrow_invocable_v<F, T&> ) -> bool
      {
         return local( ).try_push( std::forward<F>( fill ) );
      }

      /**
       * Consumer side. Empties each thread's ring in turn.
       * @return The number of values consumed.
       */
      template <std::invocable<T&> F> auto drain( F&& consume ) noexcept( std::is_nothrow_invocable_v<F&, T&> ) -> std::size_t
      {
         std::size_t consumed{ 0 };
         for ( Lane* lane{ first_.load( std::memory_order_acquire ) }; lane != nullptr; lane = lane->next )
         {
            consumed += lane->ring.drain( consume );
         }
         return consumed;
      }

      /**
       * Consumer side. Consumes values across threads by ascending \p key, repeatedly taking the smallest head among all rings. Ties keep ring order.
       * @return The number of values consumed.
       */
      template <std::invocable<T&> F, std::invocable<T const&> K>
      auto drain( F&& consume, K&& key ) noexcept( std::is_nothrow_invocable_v<F&, T&> && std::is_nothrow_invocable_v<K&, T const&> ) -> std::size_t
      {
         Lane* const first{ first_.load( std::memory_order_acquire ) };
         //
         // A single producer so far: nothing to merge.
         if ( first != nullptr && first->next == nullptr )
         {
            return first->ring.drain( consume );
         }
         //
         std::size_t consumed{ 0 };
         while ( true )
         {
            struct LaneRef
            {
               Lane* lane{ nullptr };
               T* value{ nullptr };

               auto has_value( ) -> bool { return value != nullptr; }
            } earliest{ nullptr, nullptr };
            //
            for ( Lane* lane{ first }; lane != nullptr; lane = lane->next )
            {
               if ( T* const value{ lane->ring.peek( ) }; value != nullptr && ( not earliest.has_value( ) || key( *value ) < key( *earliest.value ) ) )
               {
                  earliest = { lane, value };
               }
            }
            //
            if ( not earliest.has_value( ) )
            {
               return consumed;
            }
            //
            consume( *earliest.value );
            earliest.lane->ring.pop( );
            ++consumed;
         }
      }

   private:
      // Identifies this instance in the threads' caches. An address could be reused by a later instance, an id never is.
      static inline std::atomic<std::uint64_t> next_id_{ 1 };
      std::uint64_t const id_{ next_id_.fetch_add( 1, std::memory_order_relaxed ) };

      // Lanes form an append-only list the consumer walks without locking; registration (rare) takes the lock.
      std::atomic<Lane*> first_{ nullptr };
      std::vector<std::unique_ptr<Lane>> owned_{};
      std::mutex registry_lock_{};

      [[nodiscard]] auto local( ) noexcept -> ring_type&
      {
         struct Cache
         {
            std::uint64_t owner;
            Lane* lane;
         };
         thread_local Cache cache{ 0, nullptr };
         //
         if ( cache.owner != id_ ) [[unlikely]]
         {
            cache = { id_, &acquire_lane( ) };
         }
         return cache.lane->ring;
      }

      /**
       * Find the calling thread's lane, or create and publish one.
       */
      [[nodiscard]] auto acquire_lane( ) noexcept -> Lane&
      {
         std::lock_guard lock{ registry_lock_ };
         std::thread::id const self{ std::this_thread::get_id( ) };
         //
         for ( Lane* lane{ first_.load( std::memory_order_relaxed ) }; lane != nullptr; lane = lane->next )
         {
            if ( lane->owner == self )
            {
               return *lane;
            }
         }
         //
         Lane& lane{ *owned_.emplace_back( std::make_unique<Lane>( self ) ) };
         lane.next = first_.load( std::memory_order_relaxed );
         first_.store( &lane, std::memory_order_release );
         return lane;
      }
   };
}