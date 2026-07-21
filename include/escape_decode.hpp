// escape_decode.hpp
//
// Bytecode obtained from a Lua string value (e.g. via string.dump-style
// APIs) is very often saved or pasted as *text*: a Lua string literal
// where printable bytes appear as themselves and everything else appears
// as a backslash escape (`\7`, `\255`, `\xFF`, ...). Fed directly as
// "raw bytes", that text is nonsense -- the literal characters of the
// escape sequences (backslash, digits, ...) get read instead of the byte
// values they denote. This decodes that back into the real byte stream.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace luaudec
{

// Tries to interpret `text` as a Lua-escaped byte string and decode it.
// Handles three shapes, tried in order:
//   1. A quoted string literal somewhere in the text (optionally preceded
//      by `return`, an assignment, etc.) -- e.g. `return "\7\0\3..."`.
//   2. The entire input, if it contains no quote characters at all but
//      does look like it's made of escape sequences / printable bytes
//      with no quoting (a bare dump of the escaped content).
// Returns nullopt if neither shape is plausible (e.g. the text has no
// backslash escapes at all, suggesting it isn't this kind of file).
std::optional<std::vector<uint8_t>> tryDecodeLuaEscapedBytes(const std::string& text);

} // namespace luaudec