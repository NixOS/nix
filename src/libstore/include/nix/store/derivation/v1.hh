#pragma once
///@file
///
/// [Version 1](@docroot@/store/derivation/versions.md#version-1)
/// derivations, as they are in their own right.
///
/// A version 1 derivation is what was parsed --- the traditional shape
/// of its encoding --- plus what the encoding leaves out: its name, and
/// its options, decoded once from where the encoding hides them, and
/// kept only as a cache. The options cannot be varied independently of
/// what they were decoded from: to change a version 1 derivation, change
/// what was parsed, and construct it again.
///
/// The two modes of version 1 derivations are different types, since
/// they can express different options.
///
/// (A version 2 derivation's parsed form nests the other way round,
/// holding the derivation plus the output paths it records. That is not
/// a mistake: it is just how each format happens to relate to the
/// derivations it encodes, not something either is defined by.)

#include "nix/store/derivations.hh"
#include "nix/store/derivation/traditional.hh"

namespace nix::derivation::v1 {

/**
 * The input type of a derivation whose traditional-shape inputs are
 * `Inputs`.
 */
template<typename Inputs>
struct InputOf;

template<>
struct InputOf<FullInputs>
{
    using type = SingleDerivedPath;
};

template<>
struct InputOf<StorePathSet>
{
    using type = StorePath;
};

/**
 * What both modes of version 1 derivation have: what was parsed, and the
 * name the encoding leaves out.
 */
template<typename Inputs>
class Common
{
public:
    using Input = typename InputOf<Inputs>::type;

    const TraditionalParsedT<Inputs> & parsed() const
    {
        return parsed_;
    }

    const std::string & name() const
    {
        return name_;
    }

    bool operator==(const Common &) const = default;

protected:
    Common(TraditionalParsedT<Inputs> parsed, std::string name)
        : parsed_(std::move(parsed))
        , name_(std::move(name))
    {
    }

    TraditionalParsedT<Inputs> parsed_;
    std::string name_;
};

/**
 * A version 1 derivation without structured attributes, whose options
 * are encoded as specially-named environment variables.
 *
 * In this mode, output checks can only be given once for all outputs,
 * and there is no way to discard an output's references; environment
 * variables can be passed as files.
 */
template<typename Inputs>
class EnvDerivation : public Common<Inputs>
{
public:
    using typename Common<Inputs>::Input;

    EnvDerivation(
        TraditionalParsedT<Inputs> parsed,
        std::string name,
        const StoreDirConfig & store,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    const CommonTopOptions<Input> & options() const
    {
        return options_;
    }

    const std::optional<OutputChecks<Input>> & allOutputChecks() const
    {
        return allOutputChecks_;
    }

    const StringSet & passAsFile() const
    {
        return passAsFile_;
    }

    bool operator==(const EnvDerivation &) const = default;

private:
    /* Decoded from `parsed_`, and only a cache of it. */
    CommonTopOptions<Input> options_;
    std::optional<OutputChecks<Input>> allOutputChecks_;
    StringSet passAsFile_;
};

/**
 * A version 1 derivation with structured attributes, whose options are
 * encoded as specially-named attributes in them.
 *
 * In this mode, output checks are given per output, and an output's
 * references can be discarded; nothing is passed as a file.
 */
template<typename Inputs>
class StructuredAttrsDerivation : public Common<Inputs>
{
public:
    using typename Common<Inputs>::Input;

    StructuredAttrsDerivation(
        TraditionalParsedT<Inputs> parsed,
        std::string name,
        const StoreDirConfig & store,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    const StructuredAttrs & structuredAttrs() const
    {
        return structuredAttrs_;
    }

    const CommonTopOptions<Input> & options() const
    {
        return options_;
    }

    const std::map<OutputName, OutputOptions<Input>, std::less<>> & outputOptions() const
    {
        return outputOptions_;
    }

    bool operator==(const StructuredAttrsDerivation &) const = default;

private:
    /* Decoded from `parsed_`, and only a cache of it. */
    StructuredAttrs structuredAttrs_;
    CommonTopOptions<Input> options_;
    std::map<OutputName, OutputOptions<Input>, std::less<>> outputOptions_;
};

/**
 * A version 1 derivation, in whichever mode its encoding is.
 */
template<typename Inputs>
using DerivationT = std::variant<EnvDerivation<Inputs>, StructuredAttrsDerivation<Inputs>>;

using Full = DerivationT<FullInputs>;
using Basic = DerivationT<StorePathSet>;

/**
 * Construct a version 1 derivation from what was parsed, in the mode its
 * encoding is in: with structured attributes iff its environment has a
 * `__json` variable.
 */
template<typename Inputs>
DerivationT<Inputs> fromParsed(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * The shared, version-independent view of a version 1 derivation, which
 * the rest of Nix works on.
 */
derivation::Full elaborated(const Full & drv);
derivation::Basic elaborated(const Basic & drv);

} // namespace nix::derivation::v1
