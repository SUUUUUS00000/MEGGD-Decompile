// bytecode_types.hpp
//
// In-memory representation of a deserialized Luau bytecode module. This is
// the output of BytecodeReader and the input to the disassembler and
// decompiler stages.

#pragma once

#include "bytecode_format.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace luaudec
{

// A fully decoded logical instruction (header word + optional AUX word
// folded together). `pc` is the word-offset of the header word within the
// proto's flat instruction stream, which is what jump offsets are relative
// to; `wordCount` is 1 or 2 depending on whether an AUX word follows.
struct Instruction
{
    uint32_t pc = 0;
    uint32_t wordCount = 1;
    Op op = Op::NOP;
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    int32_t d = 0;
    int32_t e = 0;
    uint32_t aux = 0;
    bool hasAux = false;

    // Resolved target instruction index (into Proto::instructions), filled
    // in for any opcode whose D or E field is a jump offset. -1 if n/a.
    // A value equal to Proto::instructions.size() means "falls off the end".
    int32_t jumpTarget = -1;
};

// One entry of a table "shape" constant (used by NEWTABLE-adjacent
// DUPTABLE). `keyConstant` indexes another constant in the same proto that
// holds the key (almost always a string). `valueConstant`, if >= 0, is the
// index of a constant holding a pre-baked value (LBC_CONSTANT_TABLE_WITH_CONSTANTS,
// bytecode version 7+); otherwise the value is filled at runtime (SETLIST).
struct TableShapeEntry
{
    int32_t keyConstant = -1;
    int32_t valueConstant = -1;
};

// Roblox's Luau fork additionally supports declaring "class" shapes
// (properties + methods) as a constant kind, backing NEWCLASSMEMBER.
struct ClassShapeInfo
{
    int32_t classNameConstant = -1;
    uint32_t numProperties = 0;
    uint32_t numMethods = 0;
    std::vector<uint32_t> memberNameConstants; // size == numProperties + numMethods
};

struct Constant
{
    ConstTag tag = ConstTag::Nil;

    bool boolValue = false;
    double numberValue = 0.0;
    float vecValue[4] = {0, 0, 0, 0};

    // String constant: index into Module::strings (0-based, already adjusted
    // from the on-disk 1-based / 0-means-null encoding).
    std::optional<uint32_t> stringIndex;

    // Import constant: packed id, 10-10-10-2 bits, decoded to a dotted path
    // by resolving through Module::strings using the proto's own constant
    // table (each 10-bit segment indexes a constant, which is a String).
    uint32_t importId = 0;

    // Closure constant.
    uint32_t closureProto = 0;

    // Table / TableWithConstants shape.
    std::vector<TableShapeEntry> tableShape;

    // Class shape (Luau/Roblox extension).
    ClassShapeInfo classShape;

    // 64-bit integer constant (LBC_CONSTANT_INTEGER).
    int64_t integerValue = 0;
};

struct LocalVarInfo
{
    std::optional<uint32_t> nameString; // index into Module::strings
    uint32_t startpc = 0;
    uint32_t endpc = 0;
    uint8_t reg = 0;
};

struct UpvalInfo
{
    std::optional<uint32_t> nameString;
};

struct Proto
{
    uint32_t selfIndex = 0; // this proto's index in Module::protos

    uint8_t maxStackSize = 0;
    uint8_t numParams = 0;
    uint8_t numUpvalues = 0;
    bool isVararg = false;
    uint8_t flags = 0;

    std::vector<uint8_t> typeInfoRaw; // opaque, kept for completeness/dumping

    std::vector<Instruction> instructions;
    uint32_t totalWordCount = 0; // sizecode from the file (words, incl. AUX)

    // Maps a raw word-pc to a logical instruction index; sized
    // totalWordCount+1, where the trailing extra slot represents "one past
    // the end" (a valid jump target meaning "fall through past the last
    // instruction").
    std::vector<int32_t> pcToInsnIndex;

    std::vector<Constant> constants;
    std::vector<uint32_t> childProtoIds; // indices into Module::protos

    int32_t lineDefined = 0;
    std::optional<uint32_t> debugNameString;

    bool hasLineInfo = false;
    uint8_t lineGapLog2 = 0;
    std::vector<uint8_t> lineInfoDeltas; // one per word (totalWordCount entries)
    std::vector<int32_t> lineInfoAbs;    // one per (sizecode-1)>>gap + 1 interval

    bool hasDebugInfo = false;
    std::vector<LocalVarInfo> locals;
    std::vector<UpvalInfo> upvalNames;

    uint32_t feedbackVecSize = 0;

    // Returns the source line for the instruction at word-pc `pc`, or -1 if
    // no line info was embedded.
    int32_t lineForPc(uint32_t pc) const
    {
        if (!hasLineInfo || pc >= lineInfoDeltas.size())
            return -1;
        uint32_t interval = pc >> lineGapLog2;
        if (interval >= lineInfoAbs.size())
            return -1;
        return lineInfoAbs[interval] + lineInfoDeltas[pc];
    }

    // Instruction index -> logical index resolution helper for jump/branch
    // targets. Returns instructions.size() for "past the end".
    int32_t resolveTarget(uint32_t targetPc) const
    {
        if (targetPc < pcToInsnIndex.size())
            return pcToInsnIndex[targetPc];
        return static_cast<int32_t>(instructions.size());
    }
};

struct UserdataTypeEntry
{
    uint8_t index = 0;
    std::string name;
};

struct Module
{
    uint8_t version = 0;
    uint8_t typesVersion = 0;

    std::vector<std::string> strings; // 0-based
    std::vector<UserdataTypeEntry> userdataTypes;

    std::vector<Proto> protos; // 0-based; protos[i].selfIndex == i
    uint32_t mainProtoId = 0;

    const std::string& str(uint32_t index) const
    {
        static const std::string empty;
        return index < strings.size() ? strings[index] : empty;
    }

    std::string strOr(const std::optional<uint32_t>& index, const std::string& fallback) const
    {
        return index ? str(*index) : fallback;
    }
};

// Thrown by BytecodeReader on any malformed / unsupported input. Carries a
// human-readable message; never thrown for "the code is obfuscated" reasons
// -- only for genuinely truncated/corrupt streams or version numbers outside
// [kBytecodeVersionMin, kBytecodeVersionMax].
class BytecodeReadError : public std::runtime_error
{
public:
    explicit BytecodeReadError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace luaudec
