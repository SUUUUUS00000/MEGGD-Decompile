// disassembler.hpp
//
// Renders a parsed Module/Proto as human-readable assembly text: one
// operation per line, register operands as R<n>, resolved constant/import/
// upvalue annotations in brackets, and L<n> labels at jump targets.
//
// This is intentionally a faithful, literal rendering (no attempt at
// structuring) -- it exists both as useful output on its own and as the
// direct input to the decompiler's control-flow analysis.

#pragma once

#include "bytecode_types.hpp"
#include <string>

namespace luaudec
{

class Disassembler
{
public:
    static std::string disassembleModule(const Module& module);
    static std::string disassembleProto(const Module& module, const Proto& proto);

    // Renders constant `idx` from `proto`'s constant table as source-like
    // text, e.g. `"hello"`, `42`, `true`, `nil`, `game.Workspace`.
    static std::string renderConstant(const Module& module, const Proto& proto, uint32_t idx);

    // Decodes a GETIMPORT/LBC_CONSTANT_IMPORT packed id into a dotted path
    // by resolving each 10-bit segment against `proto`'s string constants.
    static std::string renderImportPath(const Module& module, const Proto& proto, uint32_t packedId);

private:
    static std::string formatInstruction(const Module& module, const Proto& proto, const Instruction& insn, const std::vector<bool>& isLabel);
    static std::string escapeString(const std::string& s);
    static std::string formatNumber(double v);
};

} // namespace luaudec
