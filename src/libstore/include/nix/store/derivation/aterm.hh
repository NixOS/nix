#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"

namespace nix {

struct Source;
struct Sink;

namespace derivation {

/**
 * Print a derivation.
 */
std::string unparse(const Full & drv, const StoreDirConfig & store);

namespace modulo {
struct HashInputs;
}

/**
 * Print a derivation in one of the intermediate forms: with the inputs
 * already flattened (`FullInputs`), or with them replaced by their
 * hashes modulo (`modulo::HashInputs`), which is the form whose hash is
 * an input address.
 *
 * The `modulo::HashInputs` cases are not round-trippable: `parse` cannot
 * read them back, as their input derivations are named by hash rather
 * than by store path.
 */
template<typename Inputs, typename Out>
std::string unparse(const Derivation<Inputs, Out> & drv, const StoreDirConfig & store)
    requires(
        // Regular `FullInputs` case must have regular `Output` outputs
        (std::is_same_v<Inputs, FullInputs> && std::is_same_v<Out, Output>)
        // Hash modulo is only for input addressing, with masked (`Deferred`) or unmasked (`InputAddressed`) outputs
        || (std::is_same_v<Inputs, modulo::HashInputs>
            && (std::is_same_v<Out, Output::InputAddressed> || std::is_same_v<Out, Output::Deferred>) ));

/**
 * Read a derivation from a file.
 */
Full parse(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

Source & read(Source & in, const StoreDirConfig & store, Basic & drv, std::string_view name);
void write(Sink & out, const StoreDirConfig & store, const Basic & drv);

} // namespace derivation

} // namespace nix
