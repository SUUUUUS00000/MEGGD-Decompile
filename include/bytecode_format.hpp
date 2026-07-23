// bytecode_format.hpp
//
// Defines the Luau bytecode instruction set: opcode numbers, instruction
// word encodings (ABC / AD / E), and per-opcode metadata (word length,
// encoding mode). This mirrors the public, MIT-licensed bytecode ISA that
// Roblox ships as part of the open-source Luau project
// (https://github.com/luau-lang/luau), since any tool that reads or writes
// this bytecode format must agree with the VM on these numeric values.
//
// Supports bytecode versions LBC_VERSION_MIN..LBC_VERSION_MAX (3..12).

#pragma once

#include <cstdint>
#include <array>
#include <string_view>

namespace luaudec
{

// ---------------------------------------------------------------------
// Bytecode / type-info format versions
// ---------------------------------------------------------------------
constexpr uint8_t kBytecodeVersionMin = 3;
constexpr uint8_t kBytecodeVersionMax = 12;

constexpr uint8_t kTypeVersionMin = 1;
constexpr uint8_t kTypeVersionMax = 3;

// ---------------------------------------------------------------------
// Opcodes (order defines numeric value, exactly as in the VM)
// ---------------------------------------------------------------------
enum class Op : uint8_t
{
    NOP = 0,
    BREAK,
    LOADNIL,
    LOADB,
    LOADN,
    LOADK,
    MOVE,
    GETGLOBAL,
    SETGLOBAL,
    GETUPVAL,
    SETUPVAL,
    CLOSEUPVALS,
    GETIMPORT,
    GETTABLE,
    SETTABLE,
    GETTABLEKS,
    SETTABLEKS,
    GETTABLEN,
    SETTABLEN,
    NEWCLOSURE,
    NAMECALL,
    CALL,
    RETURN,
    JUMP,
    JUMPBACK,
    JUMPIF,
    JUMPIFNOT,
    JUMPIFEQ,
    JUMPIFLE,
    JUMPIFLT,
    JUMPIFNOTEQ,
    JUMPIFNOTLE,
    JUMPIFNOTLT,
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    POW,
    ADDK,
    SUBK,
    MULK,
    DIVK,
    MODK,
    POWK,
    AND,
    OR,
    ANDK,
    ORK,
    CONCAT,
    NOT,
    MINUS,
    LENGTH,
    NEWTABLE,
    DUPTABLE,
    SETLIST,
    FORNPREP,
    FORNLOOP,
    FORGLOOP,
    FORGPREP_INEXT,
    FASTCALL3,
    FORGPREP_NEXT,
    NATIVECALL,
    GETVARARGS,
    DUPCLOSURE,
    PREPVARARGS,
    LOADKX,
    JUMPX,
    FASTCALL,
    COVERAGE,
    CAPTURE,
    SUBRK,
    DIVRK,
    FASTCALL1,
    FASTCALL2,
    FASTCALL2K,
    FORGPREP,
    JUMPXEQKNIL,
    JUMPXEQKB,
    JUMPXEQKN,
    JUMPXEQKS,
    IDIV,
    IDIVK,
    GETUDATAKS,
    SETUDATAKS,
    NAMECALLUDATA,
    NEWCLASSMEMBER,
    CALLFB,
    CMPPROTO,
    COUNT
};

constexpr int kOpCount = static_cast<int>(Op::COUNT);

// Instruction word encoding shape.
enum class Mode : uint8_t
{
    ABC, // opcode(8) A(8) B(8) C(8)
    AD,  // opcode(8) A(8) D(16, signed)
    E    // opcode(8) E(24, signed)
};

struct OpInfo
{
    const char* name;
    Mode mode;
    bool hasAux; // instruction occupies 2 words (extra raw uint32 follows)
};

// Metadata table, indexed by opcode value. Built once at namespace scope.
constexpr std::array<OpInfo, kOpCount> kOpTable = {{
    {"NOP", Mode::ABC, false},
    {"BREAK", Mode::ABC, false},
    {"LOADNIL", Mode::ABC, false},
    {"LOADB", Mode::ABC, false},
    {"LOADN", Mode::AD, false},
    {"LOADK", Mode::AD, false},
    {"MOVE", Mode::ABC, false},
    {"GETGLOBAL", Mode::ABC, true},
    {"SETGLOBAL", Mode::ABC, true},
    {"GETUPVAL", Mode::ABC, false},
    {"SETUPVAL", Mode::ABC, false},
    {"CLOSEUPVALS", Mode::ABC, false},
    {"GETIMPORT", Mode::AD, true},
    {"GETTABLE", Mode::ABC, false},
    {"SETTABLE", Mode::ABC, false},
    {"GETTABLEKS", Mode::ABC, true},
    {"SETTABLEKS", Mode::ABC, true},
    {"GETTABLEN", Mode::ABC, false},
    {"SETTABLEN", Mode::ABC, false},
    {"NEWCLOSURE", Mode::AD, false},
    {"NAMECALL", Mode::ABC, true},
    {"CALL", Mode::ABC, false},
    {"RETURN", Mode::ABC, false},
    {"JUMP", Mode::AD, false},
    {"JUMPBACK", Mode::AD, false},
    {"JUMPIF", Mode::AD, false},
    {"JUMPIFNOT", Mode::AD, false},
    {"JUMPIFEQ", Mode::AD, true},
    {"JUMPIFLE", Mode::AD, true},
    {"JUMPIFLT", Mode::AD, true},
    {"JUMPIFNOTEQ", Mode::AD, true},
    {"JUMPIFNOTLE", Mode::AD, true},
    {"JUMPIFNOTLT", Mode::AD, true},
    {"ADD", Mode::ABC, false},
    {"SUB", Mode::ABC, false},
    {"MUL", Mode::ABC, false},
    {"DIV", Mode::ABC, false},
    {"MOD", Mode::ABC, false},
    {"POW", Mode::ABC, false},
    {"ADDK", Mode::ABC, false},
    {"SUBK", Mode::ABC, false},
    {"MULK", Mode::ABC, false},
    {"DIVK", Mode::ABC, false},
    {"MODK", Mode::ABC, false},
    {"POWK", Mode::ABC, false},
    {"AND", Mode::ABC, false},
    {"OR", Mode::ABC, false},
    {"ANDK", Mode::ABC, false},
    {"ORK", Mode::ABC, false},
    {"CONCAT", Mode::ABC, false},
    {"NOT", Mode::ABC, false},
    {"MINUS", Mode::ABC, false},
    {"LENGTH", Mode::ABC, false},
    {"NEWTABLE", Mode::ABC, true},
    {"DUPTABLE", Mode::AD, false},
    {"SETLIST", Mode::ABC, true},
    {"FORNPREP", Mode::AD, false},
    {"FORNLOOP", Mode::AD, false},
    {"FORGLOOP", Mode::AD, true},
    {"FORGPREP_INEXT", Mode::AD, false},
    {"FASTCALL3", Mode::ABC, true},
    {"FORGPREP_NEXT", Mode::AD, false},
    {"NATIVECALL", Mode::ABC, false},
    {"GETVARARGS", Mode::ABC, false},
    {"DUPCLOSURE", Mode::AD, false},
    {"PREPVARARGS", Mode::ABC, false},
    {"LOADKX", Mode::ABC, true},
    {"JUMPX", Mode::E, false},
    {"FASTCALL", Mode::ABC, false},
    {"COVERAGE", Mode::E, false},
    {"CAPTURE", Mode::ABC, false},
    {"SUBRK", Mode::ABC, false},
    {"DIVRK", Mode::ABC, false},
    {"FASTCALL1", Mode::ABC, false},
    {"FASTCALL2", Mode::ABC, true},
    {"FASTCALL2K", Mode::ABC, true},
    {"FORGPREP", Mode::AD, false},
    {"JUMPXEQKNIL", Mode::AD, true},
    {"JUMPXEQKB", Mode::AD, true},
    {"JUMPXEQKN", Mode::AD, true},
    {"JUMPXEQKS", Mode::AD, true},
    {"IDIV", Mode::ABC, false},
    {"IDIVK", Mode::ABC, false},
    {"GETUDATAKS", Mode::ABC, true},
    {"SETUDATAKS", Mode::ABC, true},
    {"NAMECALLUDATA", Mode::ABC, true},
    {"NEWCLASSMEMBER", Mode::ABC, true},
    {"CALLFB", Mode::ABC, true},
    {"CMPPROTO", Mode::AD, true},
}};

inline const OpInfo& opInfo(uint8_t op)
{
    static const OpInfo unknown{"UNKNOWN", Mode::ABC, false};
    if (op >= kOpCount)
        return unknown;
    return kOpTable[op];
}

// Number of 32-bit words occupied by an instruction (1, or 2 if it has AUX).
inline int opWordCount(uint8_t op)
{
    return opInfo(op).hasAux ? 2 : 1;
}

// ---------------------------------------------------------------------
// Instruction word decoding
// ---------------------------------------------------------------------
inline uint8_t insnOp(uint32_t insn) { return static_cast<uint8_t>(insn & 0xff); }
inline uint8_t insnA(uint32_t insn) { return static_cast<uint8_t>((insn >> 8) & 0xff); }
inline uint8_t insnB(uint32_t insn) { return static_cast<uint8_t>((insn >> 16) & 0xff); }
inline uint8_t insnC(uint32_t insn) { return static_cast<uint8_t>((insn >> 24) & 0xff); }
inline int32_t insnD(uint32_t insn) { return static_cast<int32_t>(insn) >> 16; }
inline int32_t insnE(uint32_t insn) { return static_cast<int32_t>(insn) >> 8; }

inline uint8_t auxA(uint32_t aux) { return static_cast<uint8_t>(aux & 0xff); }
inline uint8_t auxB(uint32_t aux) { return static_cast<uint8_t>((aux >> 8) & 0xff); }
inline uint32_t auxKV(uint32_t aux) { return aux & 0xffffff; }
inline uint32_t auxKB(uint32_t aux) { return aux & 0x1; }
inline uint32_t auxNot(uint32_t aux) { return aux >> 31; }
inline uint16_t auxKV16(uint32_t aux) { return static_cast<uint16_t>(aux & 0xffff); }
inline uint16_t auxSlot(uint32_t aux) { return static_cast<uint16_t>(aux >> 16); }

// ---------------------------------------------------------------------
// Constant table entry tags
// ---------------------------------------------------------------------
enum class ConstTag : uint8_t
{
    Nil = 0,
    Boolean,
    Number,
    String,
    Import,
    Table,
    Closure,
    Vector,
    TableWithConstants,
    Integer,
    ClassShape,
};

// ---------------------------------------------------------------------
// Static type tags (used in optional per-function type-info blob)
// ---------------------------------------------------------------------
enum class TypeTag : uint8_t
{
    Nil = 0,
    Boolean,
    Number,
    String,
    Table,
    Function,
    Thread,
    Userdata,
    Vector,
    Buffer,
    Integer,
    Any = 15,
};
constexpr uint8_t kTypeTaggedUserdataBase = 64;
constexpr uint8_t kTypeTaggedUserdataEnd = 64 + 32;
constexpr uint8_t kTypeOptionalBit = 1 << 7;

// ---------------------------------------------------------------------
// LOP_CAPTURE capture kinds
// ---------------------------------------------------------------------
enum class CaptureType : uint8_t
{
    Val = 0,
    Ref,
    Upval,
};

// ---------------------------------------------------------------------
// Proto::flags bits
// ---------------------------------------------------------------------
enum ProtoFlag : uint8_t
{
    ProtoFlag_NativeModule = 1 << 0,
    ProtoFlag_NativeCold = 1 << 1,
    ProtoFlag_NativeFunction = 1 << 2,
    ProtoFlag_Inlinable = 1 << 3,
};

// ---------------------------------------------------------------------
// Builtin ("fastcall") function ids used by FASTCALL / FASTCALL1/2/2K/3.
// Only the ones we want to render with a friendly name are named here;
// unknown ids just print as bfunc<id>.
// ---------------------------------------------------------------------
std::string_view builtinName(int id);

} // namespace luaudec
