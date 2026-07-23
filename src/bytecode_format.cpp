#include "bytecode_format.hpp"

#include <array>
#include <string_view>

namespace luaudec
{

std::string_view builtinName(int id)
{
    static constexpr std::array<std::string_view, 133> names = {{
        "",                    // 0 NONE
        "assert",              // 1
        "math.abs",            // 2
        "math.acos",           // 3
        "math.asin",           // 4
        "math.atan2",          // 5
        "math.atan",           // 6
        "math.ceil",           // 7
        "math.cosh",           // 8
        "math.cos",            // 9
        "math.deg",            // 10
        "math.exp",            // 11
        "math.floor",          // 12
        "math.fmod",           // 13
        "math.frexp",          // 14
        "math.ldexp",          // 15
        "math.log10",          // 16
        "math.log",            // 17
        "math.max",            // 18
        "math.min",            // 19
        "math.modf",           // 20
        "math.pow",            // 21
        "math.rad",            // 22
        "math.sinh",           // 23
        "math.sin",            // 24
        "math.sqrt",           // 25
        "math.tanh",           // 26
        "math.tan",            // 27
        "bit32.arshift",       // 28
        "bit32.band",          // 29
        "bit32.bnot",          // 30
        "bit32.bor",           // 31
        "bit32.bxor",          // 32
        "bit32.btest",         // 33
        "bit32.extract",       // 34
        "bit32.lrotate",       // 35
        "bit32.lshift",        // 36
        "bit32.replace",       // 37
        "bit32.rrotate",       // 38
        "bit32.rshift",        // 39
        "type",                // 40
        "string.byte",         // 41
        "string.char",         // 42
        "string.len",          // 43
        "typeof",              // 44
        "string.sub",          // 45
        "math.clamp",          // 46
        "math.sign",           // 47
        "math.round",          // 48
        "rawset",              // 49
        "rawget",              // 50
        "rawequal",            // 51
        "table.insert",        // 52
        "table.unpack",        // 53
        "vector",              // 54
        "bit32.countlz",       // 55
        "bit32.countrz",       // 56
        "select",              // 57 (select(_, ...))
        "rawlen",              // 58
        "bit32.extractk",      // 59
        "getmetatable",        // 60
        "setmetatable",        // 61
        "tonumber",            // 62
        "tostring",            // 63
        "bit32.byteswap",      // 64
        "buffer.readi8",       // 65
        "buffer.readu8",       // 66
        "buffer.writeu8",      // 67
        "buffer.readi16",      // 68
        "buffer.readu16",      // 69
        "buffer.writeu16",     // 70
        "buffer.readi32",      // 71
        "buffer.readu32",      // 72
        "buffer.writeu32",     // 73
        "buffer.readf32",      // 74
        "buffer.writef32",     // 75
        "buffer.readf64",      // 76
        "buffer.writef64",     // 77
        "vector.magnitude",    // 78
        "vector.normalize",    // 79
        "vector.cross",        // 80
        "vector.dot",          // 81
        "vector.floor",        // 82
        "vector.ceil",         // 83
        "vector.abs",          // 84
        "vector.sign",         // 85
        "vector.clamp",        // 86
        "vector.min",          // 87
        "vector.max",          // 88
        "math.lerp",           // 89
        "vector.lerp",         // 90
        "math.isnan",          // 91
        "math.isinf",          // 92
        "math.isfinite",       // 93
        "Integer.create",      // 94
        "Integer.tonumber",    // 95
        "Integer.neg",         // 96
        "Integer.add",         // 97
        "Integer.sub",         // 98
        "Integer.mul",         // 99
        "Integer.div",         // 100
        "Integer.min",         // 101
        "Integer.max",         // 102
        "Integer.rem",         // 103
        "Integer.idiv",        // 104
        "Integer.udiv",        // 105
        "Integer.urem",        // 106
        "Integer.mod",         // 107
        "Integer.clamp",       // 108
        "Integer.band",        // 109
        "Integer.bor",         // 110
        "Integer.bnot",        // 111
        "Integer.bxor",        // 112
        "Integer.lt",          // 113
        "Integer.le",          // 114
        "Integer.ult",         // 115
        "Integer.ule",         // 116
        "Integer.gt",          // 117
        "Integer.ge",          // 118
        "Integer.ugt",         // 119
        "Integer.uge",         // 120
        "Integer.lshift",      // 121
        "Integer.rshift",      // 122
        "Integer.arshift",     // 123
        "Integer.lrotate",     // 124
        "Integer.rrotate",     // 125
        "Integer.extract",     // 126
        "Integer.btest",       // 127
        "Integer.countrz",     // 128
        "Integer.countlz",     // 129
        "Integer.bswap",       // 130
        "buffer.readinteger",  // 131
        "buffer.writeinteger", // 132
    }};

    if (id < 0 || static_cast<size_t>(id) >= names.size())
        return "";
    return names[static_cast<size_t>(id)];
}

} // namespace luaudec
