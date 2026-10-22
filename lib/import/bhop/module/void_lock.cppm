module;

#include <bhop/pch.hpp>

export module bhop:void_lock;


export namespace bhop
{
   /**
    * A lock that does nothing, for loggers only ever used from one thread. Locking it compiles to nothing.
    */
   struct VoidLock
   {
      constexpr auto lock( ) noexcept -> void { }
      constexpr auto unlock( ) noexcept -> void { }
   };
}
