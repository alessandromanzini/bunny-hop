module;

#include <bhop/pch.hpp>

export module bhop:spsc_ring;

export import :cache_traits;


export namespace bhop
{
   /**
    * @brief Lock-free single-producer, single-consumer ring.
    *
    * The producer owns the tail, the consumer owns the head, and values are published and observed through release/acquire ordering on them. Each
    * side keeps a cached copy of the other's position and only reads the other side's cache line when the cache says the ring may be full or
    * empty.
    *
    * @warning At most one thread may push and at most one thread may consume at a time.
    *
    * @tparam T         Value type. Must be default-constructible; slots are overwritten in place by the producer's fill.
    * @tparam _capacity Number of slots. A power of two keeps the index computation a mask.
    * @tparam alignment Alignment of each slot.
    */
   template <std::default_initializable T, std::size_t _capacity, std::size_t alignment = traits::cache_line_size_v> class SpscRing final
   {
      static_assert( _capacity > 0, "A ring needs at least one slot." );

      struct alignas( alignment ) Slot
      {
         T value;
      };
      using storage_type = std::array<Slot, _capacity>;

   public:
      using value_type = T;
      static constexpr auto capacity = _capacity;
      static constexpr std::size_t slot_size{ sizeof( Slot ) };

      SpscRing( ) = default;
      SpscRing( SpscRing const& ) = delete;
      SpscRing( SpscRing&& ) = delete;
      auto operator=( SpscRing const& ) -> SpscRing& = delete;
      auto operator=( SpscRing&& ) -> SpscRing& = delete;

      /**
       * Producer side. Calls \p fill on the next free slot, then publishes it.
       * @return false, without calling \p fill, when the ring is full.
       */
      template <std::invocable<T&> F> auto try_push( F&& fill ) noexcept( std::is_nothrow_invocable_v<F, T&> ) -> bool
      {
         std::size_t const tail{ tail_.load( std::memory_order_relaxed ) };
         //
         // Only read the consumer's line when the cached head says the ring may be full.
         if ( tail - head_cache_ == capacity )
         {
            head_cache_ = head_.load( std::memory_order_acquire );
            if ( tail - head_cache_ == capacity )
            {
               return false;
            }
         }
         std::forward<F>( fill )( slot_at( tail ).value );
         tail_.store( tail + 1, std::memory_order_release );
         return true;
      }

      /**
       * Consumer side.
       * @return The oldest published value, or nullptr when the ring is empty.
       */
      [[nodiscard]] auto peek( ) noexcept -> T*
      {
         std::size_t const head{ head_.load( std::memory_order_relaxed ) };
         if ( head == tail_cache_ )
         {
            tail_cache_ = tail_.load( std::memory_order_acquire );
            if ( head == tail_cache_ )
            {
               return nullptr;
            }
         }
         return &slot_at( head ).value;
      }

      /**
       * Consumer side. Frees the slot returned by the last successful \c peek.
       */
      auto pop( ) noexcept -> void { head_.store( head_.load( std::memory_order_relaxed ) + 1, std::memory_order_release ); }

      /**
       * Consumer side. Consumes every published value in order.
       * @return The number of values consumed.
       */
      template <std::invocable<T&> F> auto drain( F&& consume ) noexcept( std::is_nothrow_invocable_v<F&, T&> ) -> std::size_t
      {
         std::size_t consumed{ 0 };
         while ( T* const value{ peek( ) } )
         {
            consume( *value );
            pop( );
            ++consumed;
         }
         return consumed;
      }

   private:
      // On the heap, so the ring stays small enough to live on any thread's stack.
      std::unique_ptr<storage_type> const slots_{ std::make_unique<storage_type>( ) };

      // Producer's line: its write position, and its last known copy of the consumer's position.
      alignas( traits::cache_line_size_v ) std::atomic<std::size_t> tail_{ 0 };
      std::size_t head_cache_{ 0 };

      // Consumer's line: its read position, and its last known copy of the producer's position.
      alignas( traits::cache_line_size_v ) std::atomic<std::size_t> head_{ 0 };
      std::size_t tail_cache_{ 0 };

      [[nodiscard]] auto slot_at( std::size_t index ) noexcept -> Slot& { return ( *slots_ )[index % capacity]; }
   };
}
