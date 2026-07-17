// bytecode_reader.hpp
//
// Parses a raw Luau bytecode blob (as produced by luau-compile --binary,
// or by string.dump-style embedding) into the in-memory Module structure
// defined in bytecode_types.hpp.
//
// The wire format implemented here matches the official deserializer in
// VM/src/lvmload.cpp of https://github.com/luau-lang/luau (function
// `loadsafe`), field for field, but is written as a *safe*, bounds-checked
// reader suitable for parsing arbitrary/untrusted files rather than
// compiler-trusted input: every read is range-checked and reports a
// BytecodeReadError with a byte offset instead of reading out of bounds.

#pragma once

#include "bytecode_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace luaudec
{

class BytecodeReader
{
public:
    // Parses `size` bytes at `data`. Throws BytecodeReadError on any
    // malformed or truncated input, or on a bytecode/type version outside
    // the supported range.
    static Module read(const uint8_t* data, size_t size);

    // Convenience: reads an entire file from disk and parses it.
    static Module readFile(const std::string& path);

private:
    BytecodeReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    Module parseModule();
    void parseUserdataTypeRemap(Module& module);
    Proto parseProto(uint32_t index, Module& module);
    Constant parseConstant(Proto& proto, Module& module);
    void parseLineInfo(Proto& proto);
    void parseDebugInfo(Proto& proto, Module& module);
    void decodeInstructions(Proto& proto, const std::vector<uint32_t>& words);

    // Low-level primitives (all bounds-checked; throw BytecodeReadError).
    uint8_t readU8();
    uint32_t readU32();
    double readF64();
    float readF32();
    uint32_t readVarInt();
    uint64_t readVarInt64();
    std::string readRawString(size_t length);
    // Reads a varint string-table reference: 0 => not present, else index-1.
    std::optional<uint32_t> readStringRef(const Module& module);

    void requireBytes(size_t n) const;

    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
};

} // namespace luaudec
