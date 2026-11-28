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
#include "nix/store/derivation/v1/traditional.hh"
#include "nix/store/derivation/interface.hh"
#include "nix/store/derivation/structured-attrs.hh"

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
 *
 * Its strings refer to the derivation's own outputs by their stable
 * placeholders, and to its inputs by their store paths, which are all
 * known: a version 1 derivation cannot depend on a version 2 one.
 */
template<typename Inputs>
class Common : public Interface<typename InputOf<Inputs>::type>
{
public:
    using Input = typename InputOf<Inputs>::type;
    using typename Interface<Input>::Render;

    const TraditionalParsedT<Inputs> & parsed() const
    {
        return parsed_;
    }

    const std::string & name() const override
    {
        return name_;
    }

    const std::string & platform() const override
    {
        return parsed_.platform;
    }

    InputSet<Input> inputs() const override;

    std::string builder(const Render & render) const override;

    Strings args(const Render & render) const override;

    bool operator==(const Common & other) const
    {
        return parsed_ == other.parsed_ && name_ == other.name_;
    }

protected:
    Common(TraditionalParsedT<Inputs> parsed, std::string name)
        : parsed_(std::move(parsed))
        , name_(std::move(name))
    {
    }

    /**
     * Write out the references to the derivation's own outputs in `s`.
     */
    std::string render(std::string s, const Render & render) const;

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
class EnvDerivationCommon : public Common<Inputs>
{
public:
    using typename Common<Inputs>::Input;
    using typename Common<Inputs>::Render;

    EnvDerivationCommon(
        TraditionalParsedT<Inputs> parsed,
        std::string name,
        const StoreDirConfig & store,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings,
        bool shouldWarn = true);

    const CommonTopOptions<Input> & options() const override
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

    /**
     * The checks given for all outputs are those of each.
     */
    std::map<OutputName, OutputInfo<Input>, std::less<>> outputs() const override;

    std::map<std::string, RenderedEnvVar, std::less<>> env(const Render & render) const override;

    bool operator==(const EnvDerivationCommon &) const = default;

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
class StructuredAttrsDerivationCommon : public Common<Inputs>
{
public:
    using typename Common<Inputs>::Input;
    using typename Common<Inputs>::Render;

    StructuredAttrsDerivationCommon(
        TraditionalParsedT<Inputs> parsed,
        std::string name,
        const StoreDirConfig & store,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings,
        bool shouldWarn = true);

    const StructuredAttrs & structuredAttrs() const
    {
        return structuredAttrs_;
    }

    const CommonTopOptions<Input> & options() const override
    {
        return options_;
    }

    const std::map<OutputName, OutputOptions<Input>, std::less<>> & outputOptions() const
    {
        return outputOptions_;
    }

    std::map<OutputName, OutputInfo<Input>, std::less<>> outputs() const override;

    /**
     * The environment variables other than the structured attributes
     * themselves.
     */
    std::map<std::string, RenderedEnvVar, std::less<>> env(const Render & render) const override;

    bool operator==(const StructuredAttrsDerivationCommon &) const = default;

private:
    /* Decoded from `parsed_`, and only a cache of it. */
    StructuredAttrs structuredAttrs_;
    CommonTopOptions<Input> options_;
    std::map<OutputName, OutputOptions<Input>, std::less<>> outputOptions_;
};

/* The full and basic forms of each mode. Only a basic derivation is
   built, so only it says how its builder's environment is made. */

template<typename Inputs>
class EnvDerivation;

template<>
class EnvDerivation<FullInputs> final : public EnvDerivationCommon<FullInputs>
{
public:
    using EnvDerivationCommon::EnvDerivationCommon;
    ~EnvDerivation() override;

    std::shared_ptr<const Interface<StorePath>> resolve(const StoreDirConfig & store, const PathOf & pathOf) const override;
};

template<>
class EnvDerivation<StorePathSet> final : public EnvDerivationCommon<StorePathSet>
{
public:
    using EnvDerivationCommon::EnvDerivationCommon;

    /**
     * Each environment variable is set directly or via a file, and each
     * export references graph is in the legacy textual format.
     */
    DesugaredEnv desugarEnv(
        Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths)
        const override;
};

template<typename Inputs>
class StructuredAttrsDerivation;

template<>
class StructuredAttrsDerivation<FullInputs> final : public StructuredAttrsDerivationCommon<FullInputs>
{
public:
    using StructuredAttrsDerivationCommon::StructuredAttrsDerivationCommon;
    ~StructuredAttrsDerivation() override;

    std::shared_ptr<const Interface<StorePath>> resolve(const StoreDirConfig & store, const PathOf & pathOf) const override;
};

template<>
class StructuredAttrsDerivation<StorePathSet> final : public StructuredAttrsDerivationCommon<StorePathSet>
{
public:
    using StructuredAttrsDerivationCommon::StructuredAttrsDerivationCommon;

    /**
     * The structured attributes, with the export references graphs in
     * them, are given as JSON and shell files.
     */
    DesugaredEnv desugarEnv(
        Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths)
        const override;
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
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings,
    bool shouldWarn = true);

/**
 * The shared, version-independent view of a version 1 derivation, which
 * the rest of Nix works on.
 */
derivation::Full elaborated(const Full & drv);
derivation::Basic elaborated(const Basic & drv);

} // namespace nix::derivation::v1
