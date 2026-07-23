#ifndef NIX_API_STORE_FWD_H
#define NIX_API_STORE_FWD_H
/** @file
 * @brief Forward declarations shared by the libstore C headers.
 *
 * Centralised here so that sibling headers (`derivation.h`,
 * `realisation.h`, ...) can refer to `Store` without each redeclaring
 * the typedef and tripping strict C parsers (C99 `-pedantic-errors`,
 * some cffi parsers) on duplicate typedefs.
 */

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Reference to a Nix store */
typedef struct Store Store;

// cffi end
#ifdef __cplusplus
}
#endif
#endif // NIX_API_STORE_FWD_H
