#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"

namespace nix {

struct Source;
struct Sink;

namespace derivation {

/**
 * Whether the store directory may contain characters that the ATerm
 * format escapes --- in practice the `\` of a Windows store directory
 * such as `C:\ProgramData\nix\store`.
 *
 * We could always enable it, as escape sequences should only be allowed
 * to be used when needed, but out of an abundance of caution we make
 * it a flag instead.
 */
inline constexpr bool defaultSupportWindowsStoreDir =
#ifdef _WIN32
    true
#else
    false
#endif
    ;

/**
 * Print a derivation.
 */
template<typename Out>
std::string unparse(
    const Derivation<std::set<SingleDerivedPath>, Out> & drv,
    const StoreDirConfig & store,
    bool supportWindowsStoreDir = defaultSupportWindowsStoreDir);

extern template std::string unparse(const Full & drv, const StoreDirConfig & store, bool supportWindowsStoreDir);
extern template std::string
unparse(const FullDeferred & drv, const StoreDirConfig & store, bool supportWindowsStoreDir);
extern template std::string
unparse(const FullInputAddressed & drv, const StoreDirConfig & store, bool supportWindowsStoreDir);

namespace masked {
struct HashInputs;
}

/**
 * The derivation shapes `unparse` below can print: with the inputs
 * already flattened (`FullInputs`), or with them replaced by their
 * hashes modulo (`masked::HashInputs`), which is the form whose hash is
 * an input address.
 *
 * @note This is a named concept rather than a `requires` clause written
 * out at each declaration because two atomic constraints are only
 * identical when formed from the *same appearance* of an expression;
 * spelling the same condition twice would declare two distinct
 * overloads.
 */
template<typename Inputs, typename Out>
concept RenderableDerivation =
    // Regular `FullInputs` case takes the `Output` variant, or either of
    // the input-addressing alternatives on their own
    (std::is_same_v<Inputs, FullInputs>
     && (std::is_same_v<Out, Output> || std::is_same_v<Out, Output::InputAddressed>
         || std::is_same_v<Out, Output::Deferred>) )
    // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
    || (std::is_same_v<Inputs, masked::HashInputs>
        && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) );

/**
 * Print a derivation in one of the intermediate forms.
 *
 * The `masked::HashInputs` cases name their input derivations by hash
 * rather than by store path, so the default `parse` cannot read them
 * back; `parse<masked::HashInputs, Output::Deferred>` is their inverse.
 */
template<typename Inputs, typename Out>
    requires RenderableDerivation<Inputs, Out>
std::string unparse(
    const Derivation<Inputs, Out> & drv,
    const StoreDirConfig & store,
    bool supportWindowsStoreDir = defaultSupportWindowsStoreDir);

/**
 * The derivation shapes `parse` below can read. Same as
 * `RenderableDerivation`, except that the regular form's inputs are the
 * flat set callers hold, not the nested `FullInputs` the ATerm encodes.
 */
template<typename Inputs, typename Out>
concept ParsableDerivation =
    (std::is_same_v<Inputs, std::set<SingleDerivedPath>>
     && (std::is_same_v<Out, Output> || std::is_same_v<Out, Output::InputAddressed>
         || std::is_same_v<Out, Output::Deferred>) )
    || (std::is_same_v<Inputs, masked::HashInputs>
        && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) );

/**
 * Read a derivation from a file.
 *
 * The type arguments say which form is expected. They default to the
 * regular one, so ordinary callers need not mention them; reading back
 * a masked derivation --- the inverse of `unparse` for that form ---
 * is `parse<masked::HashInputs, Output::Deferred>`.
 *
 * Nothing in production reads the masked form, which exists to be
 * hashed. Being able to read it is what lets the tests check that the
 * encoding is unambiguous, which is what makes an input address well
 * defined.
 */
template<typename Inputs = std::set<SingleDerivedPath>, typename Out = Output>
    requires ParsableDerivation<Inputs, Out>
Derivation<Inputs, Out> parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir = defaultSupportWindowsStoreDir,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

extern template Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);
extern template Derivation<masked::HashInputs, Output::Deferred> parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    bool supportWindowsStoreDir,
    const ExperimentalFeatureSettings &);

Source & read(Source & in, const StoreDirConfig & store, Basic & drv, std::string_view name);
void write(Sink & out, const StoreDirConfig & store, const Basic & drv);

} // namespace derivation

} // namespace nix
