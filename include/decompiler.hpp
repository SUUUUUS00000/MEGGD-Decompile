// decompiler.hpp
//
// Provides the top-level API for the decompiler. Combines the
// BytecodeReader, Structurizer, and CodePrinter stages into a single
// function that takes a raw Luau bytecode buffer and returns the
// decompiled source code as a string.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace luaudec
{

std::string decompile_bytecode(const uint8_t* data, size_t size);

} // namespace luaudec
