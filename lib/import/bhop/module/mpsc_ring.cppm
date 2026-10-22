module;

#include <bhop/pch.hpp>

export module bhop:mpsc_ring;

export import :cache_traits;


export namespace bhop
{
   /**
    * @brief Lock-free multi-producer, single-consumer ring with exact global order (a bounded queue in the style of Dmitry Vyukov's).
    *
    * Each slot carries a sequence number: \c position when free, \c position+1 once published, \c position+Capacity once consumed (free for the next
    * lap). A producer claims the next position with a compare-and-swap on the shared tail, fills the slot, then publishes it through its sequence.
    * The claimed position is the value's place in the global order: if one push happens before another, it claims an earlier position. The
    * consumer reads positions strictly in order, waiting for a claimed slot to be published before moving past it.
    *
    * @warning Any number of threads may push; at most one thread may consume at a time.
    * @note Producers contend on the tail: one compare-and-swap per push, retried while other producers win.
    *
    * @tparam T         Value type. Must be default-constructible; slots are overwritten in place by the producer's fill.
    * @tparam _capacity Number of slots. Shared by every producer.
    * @tparam alignment Alignment of each slot, which also holds its sequence number.
    */
   template <std::default_initializable T, std::size_t _capacity, std::size_t alignment = traits::cache_line_size_v> class MpscRing final
   {
      static_assert( _capacity > 0, "A ring needs at least one slot." );

      struct alignas( alignment ) Slot
      {
         std::atomic<std::size_t> sequence;
         T value;
      };
      using storage_type = std::array<Slot, _capacity>;

   public:
      using value_type = T;
      static constexpr auto capacity = _capacity;
      static constexpr std::size_t slot_size{ sizeof( Slot ) };

      MpscRing( ) noexcept
      {
         for ( std::size_t index{ 0 }; index < capacity; ++index )
         {
            ( *slots_ )[index].sequence.store( index, std::memory_order_relaxed );
         }
      }

      MpscRing( MpscRing const& ) = delete;
      MpscRing( MpscRing&& ) = delete;
      auto operator=( MpscRing const& ) -> MpscRing& = delete;
      auto operator=( MpscRing&& ) -> MpscRing& = delete;

      /**
       * Producer side, any thread. Claims the next position, calls \p fill on its slot, then publishes it.
       * @return false, without calling \p fill, when the ring is full.
       */
      template <std::invocable<T&> F> auto try_push( F&& fill ) noexcept( std::is_nothrow_invocable_v<F, T&> ) -> bool
      {
         std::size_t position{ tail_.load( std::memory_order_relaxed ) };
         Slot* slot;
         while ( true )
         {
            slot = &slot_at( position );
            std::size_t const sequence{ slot->sequence.load( std::memory_order_acquire ) };
            //
            if ( auto const lag = static_cast<std::ptrdiff_t>( sequence - position ); lag == 0 )
            {
               // Free for this lap: try to claim it.
               if ( tail_.compare_exchange_weak( position, position + 1, std::memory_order_relaxed ) )
               {
                  break;
               }
            }
            else if ( lag < 0 )
            {
               // Still holds a value from the previous lap: the ring is full.
               return false;
            }
            else
            {
               // Another producer claimed it first: catch up.
               position = tail_.load( std::memory_order_relaxed );
            }
         }
         std::forward<F>( fill )( slot->value );
         slot->sequence.store( position + 1, std::memory_order_release );
         return true;
      }

      /**
       * Consumer side.
       * @return The next value in global order, or nullptr when the ring is empty or that position is claimed but not yet published.
       */
      [[nodiscard]] auto peek( ) noexcept -> T*
      {
         Slot& slot{ slot_at( head_ ) };
         return slot.sequence.load( std::memory_order_acquire ) == head_ + 1 ? &slot.value : nullptr;
      }

      /**
       * Consumer side. Frees the slot returned by the last successful \c peek for the next lap.
       */
      auto pop( ) noexcept -> void
      {
         slot_at( head_ ).sequence.store( head_ + capacity, std::memory_order_release );
         ++head_;
      }

      /**
       * Consumer side. Consumes every published value in global order.
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
      auto const slots_ = std::make_unique<storage_type>( );

      // Producers' line: the next position to claim.
      alignas( traits::cache_line_size_v ) std::atomic<std::size_t> tail_{ 0 };

      // Consumer's line: the next position to read. Only the consumer touches it.
      alignas( traits::cache_line_size_v ) std::size_t head_{ 0 };

      [[nodiscard]] auto slot_at( std::size_t index ) noexcept -> Slot& { return ( *slots_ )[index % capacity]; }
   };
}
