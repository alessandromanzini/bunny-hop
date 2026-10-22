module;

#include <bhop/pch.hpp>

export module bhop:cache_traits;


export namespace bhop::traits
{
   inline constexpr std::size_t cache_line_size_v{ std::hardware_destructive_interference_size };
}
