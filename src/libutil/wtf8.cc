#include <cstdint>

#include "nix/util/wtf8.hh"

namespace nix {

namespace {

constexpr char16_t highSurrogateFirst = 0xD800;
constexpr char16_t highSurrogateLast = 0xDBFF;
constexpr char16_t lowSurrogateFirst = 0xDC00;
constexpr char16_t lowSurrogateLast = 0xDFFF;

constexpr uint32_t maxCodePoint = 0x10FFFF;
constexpr char16_t replacementChar = 0xFFFD;

bool isHighSurrogate(char16_t c)
{
    return c >= highSurrogateFirst && c <= highSurrogateLast;
}

bool isLowSurrogate(char16_t c)
{
    return c >= lowSurrogateFirst && c <= lowSurrogateLast;
}

/**
 * Append the UTF-8-style encoding of `cp`.
 *
 * Unlike a UTF-8 encoder this accepts surrogate values, which is the one
 * way WTF-8 differs on the encoding side.
 */
void encodeCodePoint(std::string & out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

std::string wtf8FromUtf16(std::u16string_view s)
{
    std::string out;
    /* Most path characters are ASCII, so this is usually exact. */
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (isHighSurrogate(c) && i + 1 < s.size() && isLowSurrogate(s[i + 1])) {
            /* A valid pair denotes one supplementary code point, and is
               encoded as that code point rather than as two surrogates.
               This is what keeps WTF-8 a superset of UTF-8 for names that
               happen to be well formed. */
            uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - highSurrogateFirst) << 10)
                          + (static_cast<uint32_t>(s[i + 1]) - lowSurrogateFirst);
            encodeCodePoint(out, cp);
            ++i;
        } else {
            /* Everything else, unpaired surrogates included. Encoding one
               is the entire reason this function exists: a strict UTF-8
               encoder rejects it, and rejecting it means being unable to
               name a file that exists. */
            encodeCodePoint(out, c);
        }
    }

    return out;
}

std::u16string utf16FromWtf8(std::string_view s)
{
    std::u16string out;
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t cp;
        size_t len;

        /* The range the *second* byte is restricted to. Constraining it,
           rather than range-checking the assembled code point afterwards,
           is what makes an overlong encoding unrepresentable instead of
           merely detected. That matters because accepting one would let a
           caller smuggle a separator or a NUL past a check that inspected
           the bytes. */
        uint8_t secondLo = 0x80;
        uint8_t secondHi = 0xBF;

        if (b < 0x80) {
            cp = b;
            len = 1;
        } else if (b >= 0xC2 && b <= 0xDF) {
            /* 0xC0 and 0xC1 are excluded: they can only ever begin an
               overlong two-byte form of an ASCII character. */
            cp = b & 0x1F;
            len = 2;
        } else if (b >= 0xE0 && b <= 0xEF) {
            cp = b & 0x0F;
            len = 3;
            if (b == 0xE0)
                secondLo = 0xA0;
            /* 0xED is deliberately left alone. Strict UTF-8 restricts it to
               0x80-0x9F to exclude surrogates; WTF-8 encodes surrogates, so
               the full range is legal here. This is the one place the
               decoder is intentionally more permissive than UTF-8. */
        } else if (b >= 0xF0 && b <= 0xF4) {
            cp = b & 0x07;
            len = 4;
            if (b == 0xF0)
                secondLo = 0x90;
            if (b == 0xF4)
                secondHi = 0x8F; /* else the result would exceed U+10FFFF */
        } else {
            /* A stray continuation byte (0x80-0xBF), an overlong lead
               (0xC0, 0xC1), or a lead that cannot begin any valid sequence
               (0xF5-0xFF). */
            out += replacementChar;
            ++i;
            continue;
        }

        /* Validate the whole sequence before consuming any of it, so that a
           malformed one costs a single byte and the bytes after it get
           examined on their own terms rather than being swallowed. */
        bool wellFormed = true;
        for (size_t k = 1; k < len; ++k) {
            if (i + k >= s.size()) {
                wellFormed = false;
                break;
            }
            uint8_t c = static_cast<uint8_t>(s[i + k]);
            uint8_t lo = k == 1 ? secondLo : 0x80;
            uint8_t hi = k == 1 ? secondHi : 0xBF;
            if (c < lo || c > hi) {
                wellFormed = false;
                break;
            }
        }

        if (!wellFormed) {
            /* Exactly one byte, so progress is guaranteed and the decoder
               cannot loop. */
            out += replacementChar;
            ++i;
            continue;
        }

        for (size_t k = 1; k < len; ++k)
            cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        i += len;

        /* Unreachable given the byte ranges above; kept as an assertion of
           the invariant rather than as live error handling. */
        if (cp > maxCodePoint) {
            out += replacementChar;
            continue;
        }

        if (cp < 0x10000) {
            /* Includes the surrogate range, which is how an unpaired
               surrogate survives the round trip. */
            out += static_cast<char16_t>(cp);
        } else {
            cp -= 0x10000;
            out += static_cast<char16_t>(highSurrogateFirst + (cp >> 10));
            out += static_cast<char16_t>(lowSurrogateFirst + (cp & 0x3FF));
        }
    }

    return out;
}

} // namespace nix
