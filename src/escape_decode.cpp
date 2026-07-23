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
        {
            out.push_back('\\'); // trailing backslash, nothing after it -- keep it literally
            break;
        }
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
            int h1 = (i + 1 < end) ? hexVal(text[i + 1]) : -1;
            int h2 = (i + 2 < end) ? hexVal(text[i + 2]) : -1;
            if (h1 >= 0 && h2 >= 0)
            {
                out.push_back(static_cast<uint8_t>(h1 * 16 + h2));
                i += 3;
            }
            else
            {
                // Malformed \x -- keep going rather than rejecting the
                // whole span over it.
                out.push_back('x');
                ++i;
            }
            break;
        }
        case 'z':
            ++i;
            while (i < end && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            break;
        case 'u':
        {
            // \u{XXX}: Lua 5.4/Luau Unicode escape, encoded as UTF-8.
            if (i + 1 < end && text[i + 1] == '{')
            {
                size_t k = i + 2;
                uint32_t cp = 0;
                bool any = false;
                while (k < end && hexVal(text[k]) >= 0)
                {
                    cp = cp * 16 + static_cast<uint32_t>(hexVal(text[k]));
                    ++k;
                    any = true;
                }
                if (any && k < end && text[k] == '}')
                {
                    if (cp < 0x80)
                    {
                        out.push_back(static_cast<uint8_t>(cp));
                    }
                    else if (cp < 0x800)
                    {
                        out.push_back(static_cast<uint8_t>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
                    }
                    else if (cp < 0x10000)
                    {
                        out.push_back(static_cast<uint8_t>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
                    }
                    else
                    {
                        out.push_back(static_cast<uint8_t>(0xF0 | (cp >> 18)));
                        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
                        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
                    }
                    i = k + 1;
                    break;
                }
            }
            // Malformed \u -- fall through to lenient handling below rather
            // than rejecting the whole (possibly otherwise-fine) span.
            out.push_back('u');
            ++i;
            break;
        }
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
                    val &= 0xff; // be lenient rather than rejecting the whole span
                out.push_back(static_cast<uint8_t>(val));
            }
            else
            {
                // Unrecognized escape: rather than rejecting the entire
                // (possibly large, otherwise-correct) literal over one odd
                // sequence, degrade gracefully -- drop the backslash and
                // keep the character literally, same as Lua does for a
                // handful of its own "escape is just the character" cases.
                out.push_back(static_cast<uint8_t>(e));
                ++i;
            }
        }
    }
    return true;
}

} // namespace

std::optional<std::vector<uint8_t>> tryDecodeLuaEscapedBytes(const std::string& text)
{
    // Find every quoted string literal in the text, decode each
    // individually, and remember its [start, end) span (end = index right
    // after the closing quote) so adjacent ones can be checked for `..`
    // concatenation afterwards.
    struct Span
    {
        size_t begin, end; // of the literal incl. quotes
        std::vector<uint8_t> bytes;
    };
    std::vector<Span> spans;

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
            if (decodeSpan(text, i + 1, j, candidate) && !candidate.empty())
                spans.push_back(Span{i, j + 1, std::move(candidate)});
            i = j; // resume scanning right after this literal
        }
    }

    if (spans.empty())
    {
        // Shape 2: no quotes anywhere, but the file still has backslash
        // escapes -- maybe it's a bare dump of the escaped content with
        // no surrounding Lua syntax at all.
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

    // Merge consecutive spans that are joined by `..` (only whitespace and
    // exactly one `..` token allowed in the gap between them) -- a large
    // bytecode dump is very commonly split across several literals this
    // way, e.g. `"\1\2..." ..\n"\3\4..."`.
    std::vector<uint8_t> best = spans[0].bytes;
    std::vector<uint8_t> chain = spans[0].bytes;
    for (size_t k = 1; k < spans.size(); ++k)
    {
#ifdef LUAUDEC_DEBUG_ESCAPE
        fprintf(stderr, "[escdbg] span %zu: begin=%zu end=%zu bytes=%zu\n", k - 1, spans[k - 1].begin, spans[k - 1].end, spans[k - 1].bytes.size());
#endif
        size_t gapBegin = spans[k - 1].end;
        size_t gapEnd = spans[k].begin;
        size_t p = gapBegin;
        while (p < gapEnd && std::isspace(static_cast<unsigned char>(text[p])))
            ++p;
        bool hasConcat = (p + 1 < gapEnd && text[p] == '.' && text[p + 1] == '.');
        if (hasConcat)
        {
            p += 2;
            while (p < gapEnd && std::isspace(static_cast<unsigned char>(text[p])))
                ++p;
        }
        bool onlyWhitespaceAndOptionalConcat = (p == gapEnd);
#ifdef LUAUDEC_DEBUG_ESCAPE
        fprintf(stderr, "[escdbg] gap [%zu,%zu) text=%s hasConcat=%d p=%zu gapEnd=%zu merge=%d\n", gapBegin, gapEnd,
                text.substr(gapBegin, gapEnd - gapBegin).c_str(), hasConcat, p, gapEnd,
                onlyWhitespaceAndOptionalConcat && (hasConcat || gapBegin == gapEnd));
#endif

        if (onlyWhitespaceAndOptionalConcat && (hasConcat || gapBegin == gapEnd))
        {
            chain.insert(chain.end(), spans[k].bytes.begin(), spans[k].bytes.end());
        }
        else
        {
            chain = spans[k].bytes; // gap breaks the chain; restart from here
        }
        if (chain.size() > best.size())
            best = chain;
        if (spans[k].bytes.size() > best.size())
            best = spans[k].bytes;
    }

    return best;
}

} // namespace luaudec
