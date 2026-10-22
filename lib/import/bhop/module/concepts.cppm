module;

#include <bhop/pch.hpp>

export module bhop:concepts;


export namespace bhop::traits
{
   /**
    * Satisfied when \p T is exactly one of \p Ts.
    */
   template <typename T, typename... Ts> concept one_of = ( std::same_as<T, Ts> || ... );

   /**
    * Satisfied by the character types, cv-qualifiers ignored.
    */
   template <typename T> concept character = one_of<std::remove_cv_t<T>, char, char8_t, char16_t, char32_t>;

   /**
    * Satisfied by any type exposing \c lock() and \c unlock(), mirroring the standard BasicLockable requirement.
    */
   template <typename L>
   concept basic_lockable = requires( L& lock ) {
      lock.lock( );
      lock.unlock( );
   };

   /**
    * Smallest unsigned integer type able to represent \p value.
    */
   template <std::size_t value>
   using uint_fit_t = std::conditional_t<
     value <= std::numeric_limits<std::uint8_t>::max( ), std::uint8_t,
     std::conditional_t<value <= std::numeric_limits<std::uint16_t>::max( ), std::uint16_t,
                        std::conditional_t<value <= std::numeric_limits<std::uint32_t>::max( ), std::uint32_t, std::uint64_t>>>;
}
