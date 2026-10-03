#pragma once
/**
 * @file
 *
 * Conversion between UTF-16 code-unit sequences and WTF-8.
 *
 * Windows file names are sequences of 16-bit code units with no validity
 * requirement: an unpaired surrogate is a legal name component, and such
 * files can be created. UTF-8 cannot represent one, so a strict UTF-8
 * conversion has to either fail or corrupt the name.
 *
 * WTF-8 is UTF-8 extended to encode unpaired surrogates as their own
 * three-byte sequences, while still combining valid surrogate pairs into
 * the four-byte form for the supplementary code point they denote. That
 * makes `utf16FromWtf8(wtf8FromUtf16(s)) == s` for every `s`, including
 * names no UTF-8 encoder accepts.
 *
 * Both directions are total: neither throws, and neither has a failure
 * mode a caller must handle. Byte sequences that are not valid WTF-8 are
 * decoded to U+FFFD rather than rejected, so a caller cannot be handed a
 * path that silently lost content.
 *
 * These live here, rather than beside the Windows-only string conversion
 * that uses them, so that they are compiled and tested on every platform.
 *
 * @see https://simonsapin.github.io/wtf-8/
 */

#include <string>
#include <string_view>

namespace nix {

/**
 * Encode a UTF-16 code-unit sequence as WTF-8.
 *
 * Valid surrogate pairs become the four-byte encoding of the code point
 * they denote. Every other code unit, unpaired surrogates included, is
 * encoded on its own.
 */
std::string wtf8FromUtf16(std::u16string_view s);

/**
 * Decode WTF-8 into a UTF-16 code-unit sequence.
 *
 * Three-byte sequences denoting surrogates are restored as the unpaired
 * surrogates they came from. Code points above the basic multilingual
 * plane become surrogate pairs.
 *
 * Input that is not valid WTF-8 -- a truncated sequence, a stray
 * continuation byte, an overlong encoding, or a value above U+10FFFF --
 * yields U+FFFD for the offending byte or sequence.
 */
std::u16string utf16FromWtf8(std::string_view s);

} // namespace nix
