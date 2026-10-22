module;

#include <bhop/config/config.hpp>

export module bhop:config;


export namespace bhop::cfg
{
   using cfg::PlatformType;
   using cfg::CompilerType;
   using cfg::ArchType;
}

export namespace bhop::cfg
{
   using cfg::debug_mode;
   using cfg::platform_type;
   using cfg::compiler_type;
   using cfg::arch_type;
}
