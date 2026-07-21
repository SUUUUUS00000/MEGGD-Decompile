// escape_decode.cpp
#include "escape_decode.hpp"

#include <cctype>

namespace luaudec
{

namespace
{

// Decodes Lua escape sequences within [begin,end) of `text`, appending
// decoded bytes to `out`. Returns false on anything that doesn't look
// like valid Lua string-escape syntax (used to reject a wrong guess about
// where the literal starts/ends).
bool decodeSpan(const std::string& text, size_t begin, size_t end, std::vector<uint8_t>& out)
{
    auto hexVal = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        return -1;
    };

    size_t i = begin;
    while (i < end)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c != '\\')
        {
            out.push_back(c);
            ++i;
            continue;
        }

        ++i; // consume backslash
        if (i >= end)
            return false; // trailing backslash, nothing after it
        char e = text[i];

        switch (e)
        {
        case 'a': out.push_back(7); ++i; break;
        case 'b': out.push_back(8); ++i; break;
        case 'f': out.push_back(12); ++i; break;
        case 'n': out.push_back(10); ++i; break;
        case 'r': out.push_back(13); ++i; break;
        case 't': out.push_back(9); ++i; break;
        case 'v': out.push_back(11); ++i; break;
        case '\\': out.push_back('\\'); ++i; break;
        case '"': out.push_back('"'); ++i; break;
        case '\'': out.push_back('\''); ++i; break;
        case '\n': out.push_back('\n'); ++i; break;
        case '\r':
            out.push_back('\n');
            ++i;
            if (i < end && text[i] == '\n')
                ++i;
            break;
        case 'x':
        {
            if (i + 2 >= end)
                return false;
            int h1 = hexVal(text[i + 1]);
            int h2 = hexVal(text[i + 2]);
            if (h1 < 0 || h2 < 0)
                return false;
            out.push_back(static_cast<uint8_t>(h1 * 16 + h2));
            i += 3;
            break;
        }
        case 'z':
            ++i;
            while (i < end && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            break;
        default:
            if (e >= '0' && e <= '9')
            {
                int val = 0, n = 0;
                while (n < 3 && i < end && text[i] >= '0' && text[i] <= '9')
                {
                    val = val * 10 + (text[i] - '0');
                    ++i;
                    ++n;
                }
                if (val > 255)
                    return false;
                out.push_back(static_cast<uint8_t>(val));
            }
            else
            {
                return false; // unrecognized escape
            }
        }
    }
    return true;
}

} // namespace

std::optional<std::vector<uint8_t>> tryDecodeLuaEscapedBytes(const std::string& text)
{
    std::vector<uint8_t> best;
    bool found = false;

    // Shape 1: one or more quoted string literals somewhere in the text
    // (e.g. `return "\7\0\3..."`, `local b = "..."`). Try every one found
    // and keep the largest successfully-decoded result -- the bytecode
    // payload is virtually always the largest string literal in a small
    // wrapper file, as opposed to a short label or comment string.
    for (size_t i = 0; i < text.size(); ++i)
    {
        char q = text[i];
        if (q != '"' && q != '\'')
            continue;

        size_t j = i + 1;
        bool sawBackslash = false;
        while (j < text.size())
        {
            char c = text[j];
            if (sawBackslash)
            {
                sawBackslash = false;
                ++j;
                continue;
            }
            if (c == '\\')
            {
                sawBackslash = true;
                ++j;
                continue;
            }
            if (c == q || c == '\n')
                break;
            ++j;
        }

        if (j < text.size() && text[j] == q)
        {
            std::vector<uint8_t> candidate;
            if (decodeSpan(text, i + 1, j, candidate) && candidate.size() > best.size())
            {
                best = std::move(candidate);
                found = true;
            }
        }
        // Whether or not this one worked out, resume scanning right after
        // it (or from i+1 if unterminated) for any other literal.
    }
    if (found)
        return best;

    // Shape 2: no quotes anywhere, but the file still has backslash
    // escapes -- maybe it's a bare dump of the escaped content with no
    // surrounding Lua syntax at all.
    if (text.find('"') == std::string::npos && text.find('\'') == std::string::npos && text.find('\\') != std::string::npos)
    {
        size_t end = text.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;
        std::vector<uint8_t> candidate;
        if (decodeSpan(text, 0, end, candidate) && !candidate.empty())
            return candidate;
    }

    return std::nullopt;
}

} // namespace luaudec