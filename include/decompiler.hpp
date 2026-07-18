// decompiler.hpp
//
// Top-level orchestration: structures every proto in a module, then
// prints the main proto as a chunk (nested closures are expanded inline
// by the printer using the precomputed structured bodies).

#pragma once

#include "bytecode_types.hpp"
#include <string>

namespace luaudec
{

std::string decompileModule(const Module& module);

} // namespace luaudec
