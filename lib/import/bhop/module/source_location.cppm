module;

#include <bhop/pch.hpp>

export module bhop:source_location;


export namespace bhop
{
   /**
    * Key equality for \c std::source_location structure. The file name is compared last for faster evaluation. When the line and column match, it
    * performs a final character of the file name, short-circuiting in case of the same TU case where the c-string address would be the same, falling
    * back to character-per-character test.
    */
   struct SourceLocationCmpEq
   {
      [[nodiscard]] inline auto operator( )( std::source_location const& lhs, std::source_location const& rhs ) const noexcept -> bool
      {
         return lhs.line( ) == rhs.line( ) && lhs.column( ) == rhs.column( ) &&
                ( lhs.file_name( ) == rhs.file_name( ) || std::string_view{ lhs.file_name( ) } == rhs.file_name( ) );
      }
   };

   /**
    * Hash for \c std::source_location, keyed on the call site's line/column pair.
    * @note The file name is deliberately left out of the hash since its pointer is not stable for the same header included in different TUs, which
    *       means each hash would cost a full scan.
    */
   struct SourceLocationHash
   {
      [[nodiscard]] inline auto operator( )( std::source_location const& loc ) const noexcept -> std::size_t
      {
         // Fibonacci (golden ratio) multiplicative hash over the packed line/column pair.
         std::uint64_t const packed{ static_cast<std::uint64_t>( loc.line( ) ) << 32 | loc.column( ) };
         return static_cast<std::size_t>( packed * 0x9E37'79B9'7F4A'7C15ULL );
      }
   };
}
