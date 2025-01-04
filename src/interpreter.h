#pragma once

#include <array>
#include <vector>

#ifdef _MSC_VER
#    pragma warning(push, 0)
#    include <unicorn/unicorn.h>
#    pragma warning(pop)
#else
#    include <unicorn/unicorn.h>
#endif

#include <mcl/stdint.hpp>

extern uc_engine *uc;
