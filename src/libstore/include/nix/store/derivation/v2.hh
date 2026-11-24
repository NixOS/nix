#pragma once
///@file
///
/// [Version 2](@docroot@/store/derivation/versions.md#version-2)
/// derivations, as they are in their own right: options and *pass as
/// file* flags first-class, strings interpolated with references
/// rather than placeholders, no structured attributes.
///
/// The types admit exactly what the [version 2 "ATerm"
/// format](@docroot@/protocols/derivation-aterm/v2.md) can encode.
///
/// As for version 1, there is a *full* form, whose inputs are deriving
/// paths, and a *basic* form, whose inputs are store paths: the form a
/// full derivation takes once it is resolved.

#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derivation/interface.hh"
#include "nix/store/derivation/options.hh"
#include "nix/store/derivation/output.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/downstream-placeholder.hh"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace nix::derivation::v2 {

/**
 * What an output check element, or an export references graph entry,
 * of a derivation whose inputs are `Input`s refers to: one of the
 * derivation's own outputs, by name, or one of its inputs.
 */
template<typename Input>
using Reference = DrvRef<Input>;

/**
 * What a reference in an interpolated string of a derivation whose
 * inputs are `Input`s can refer to.
 *
 * A full derivation's strings may refer to its inputs as well as its
 * outputs. A basic derivation's may only refer to its outputs: its
 * inputs are store paths, which are written literally.
 */
template<typename Input>
struct StringReferenceOf;

template<>
struct StringReferenceOf<SingleDerivedPath>
{
    using type = Reference<SingleDerivedPath>;
};

/**
 * A reference to one of the derivation's own outputs, from an
 * interpolated string of a basic derivation. A type of its own, rather
 * than the bare `OutputName`, so that it cannot be confused with the
 * literal text it sits among.
 */
struct OutputReference
{
    OutputName name;

    auto operator<=>(const OutputReference &) const = default;
};

template<>
struct StringReferenceOf<StorePath>
{
    using type = OutputReference;
};

/**
 * A string of literal text and references.
 *
 * The pieces are kept in canonical form --- no empty literal, and no
 * two literals next to each other --- by only being built up through
 * `append`, so that each interpolated string has exactly one
 * representation, as it has exactly one encoding.
 */
template<typename Ref>
class InterpolatedT
{
public:
    using Piece = std::variant<std::string, Ref>;

    InterpolatedT() = default;

    explicit InterpolatedT(std::string literal)
    {
        append(std::move(literal));
    }

    void append(std::string literal)
    {
        if (literal.empty())
            return;
        if (!pieces_.empty())
            if (auto * last = std::get_if<std::string>(&pieces_.back())) {
                *last += literal;
                return;
            }
        pieces_.emplace_back(std::move(literal));
    }

    void append(Ref reference)
    {
        pieces_.emplace_back(std::move(reference));
    }

    const std::vector<Piece> & pieces() const
    {
        return pieces_;
    }

    /**
     * Replace each reference by the string `resolve` gives for it, and
     * concatenate.
     */
    std::string render(auto resolve) const
    {
        std::string res;
        for (auto & piece : pieces_)
            if (auto * literal = std::get_if<std::string>(&piece))
                res += *literal;
            else
                res += resolve(std::get<Ref>(piece));
        return res;
    }

    bool operator==(const InterpolatedT &) const = default;

private:
    std::vector<Piece> pieces_;
};

template<typename Input>
using Interpolated = InterpolatedT<typename StringReferenceOf<Input>::type>;

/**
 * An environment variable's value, with its *pass as file* flag.
 */
template<typename Input>
struct EnvVar
{
    Interpolated<Input> value;
    bool passAsFile = false;

    bool operator==(const EnvVar &) const = default;
};

/**
 * The generic value of an option: what any option's value can be
 * parsed as without knowing what the option means.
 */
struct Term
{
    struct List
    {
        std::vector<Term> items;
        bool operator==(const List &) const = default;
    };

    /**
     * Never empty: an empty tuple has no encoding.
     */
    struct Tuple
    {
        std::vector<Term> items;
        bool operator==(const Tuple &) const = default;
    };

    std::variant<std::string, List, Tuple> raw;

    bool operator==(const Term &) const = default;
};

/**
 * Options this version of Nix does not know, by their namespaced name
 * (`<namespace>:<name>`), kept verbatim. A derivation with any of them
 * can be read, written and copied, but not built.
 */
using UnknownOptions = std::map<std::string, Term, std::less<>>;

/**
 * The options of one output. Checks are always per output in version 2
 * derivations, so unlike version 1 there is no "no checks given" state
 * distinct from checking nothing.
 */
template<typename Input>
struct OutputOptions
{
    OutputChecks<Input> checks;
    bool unsafeDiscardReferences = false;
    UnknownOptions unknown;

    bool operator==(const OutputOptions &) const = default;
};

/**
 * The options of the derivation as a whole. Version 2 derivations have
 * no checks for all outputs at once.
 */
template<typename Input>
struct TopOptions : CommonTopOptions<Input>
{
    UnknownOptions unknown;

    bool operator==(const TopOptions &) const = default;
};

/**
 * An input-addressed output of a version 2 derivation.
 *
 * It has no path of its own: the path is a function of the rest of the
 * derivation, computed by static speculation when the store paths of
 * all inputs are known in advance, and otherwise deferred until the
 * derivation is resolved. See `staticOutputPaths`.
 */
struct InputAddressed
{
    bool operator==(const InputAddressed &) const = default;
};

template<typename Input>
struct Output
{
    std::variant<InputAddressed, derivation::Output::CAFixed, derivation::Output::CAFloating, derivation::Output::Impure>
        raw;
    OutputOptions<Input> options;

    bool operator==(const Output &) const = default;
};

/**
 * How the inputs of a derivation whose inputs are `Input`s are held:
 * as nested input derivations and sources for a full derivation, and
 * as just store paths for a basic one.
 */
template<typename Input>
struct InputsOf;

template<>
struct InputsOf<SingleDerivedPath>
{
    using type = FullInputs;
};

template<>
struct InputsOf<StorePath>
{
    using type = StorePathSet;
};

template<typename Input>
struct DerivationT
{
    std::string name;
    std::map<OutputName, Output<Input>, std::less<>> outputs;
    typename InputsOf<Input>::type inputs;
    std::string platform;
    Interpolated<Input> builder;
    std::vector<Interpolated<Input>> args;
    std::map<std::string, EnvVar<Input>, std::less<>> env;
    TopOptions<Input> options;

    bool operator==(const DerivationT &) const = default;
};

using Full = DerivationT<SingleDerivedPath>;
using Basic = DerivationT<StorePath>;

/**
 * A version 2 derivation as the rest of Nix sees it: one implementing
 * the shared `Interface`, by being what was parsed, plus the paths of
 * its input-addressed outputs where they are known.
 *
 * (This nests the other way round from version 1, whose derivations
 * hold what was parsed plus what is decoded from it. That is not a
 * mistake: it is just how each format relates to the derivations it
 * encodes.)
 */
template<typename Input>
class DerivationCommon : public Interface<Input>
{
public:
    using typename Interface<Input>::Render;

    /**
     * @param outputPaths The paths of the input-addressed outputs, if
     * they are known: for a full derivation, by static speculation, and
     * for a basic one, always.
     */
    DerivationCommon(DerivationT<Input> drv, std::map<OutputName, StorePath, std::less<>> outputPaths);

    const DerivationT<Input> & drv() const
    {
        return drv_;
    }

    const std::map<OutputName, StorePath, std::less<>> & outputPaths() const
    {
        return outputPaths_;
    }

    const std::string & name() const override;
    const std::string & platform() const override;
    InputSet<Input> inputs() const override;
    std::map<OutputName, OutputInfo<Input>, std::less<>> outputs() const override;
    const CommonTopOptions<Input> & options() const override;
    std::string builder(const Render & render) const override;
    Strings args(const Render & render) const override;
    std::map<std::string, RenderedEnvVar, std::less<>> env(const Render & render) const override;

    bool operator==(const DerivationCommon & other) const
    {
        return drv_ == other.drv_ && outputPaths_ == other.outputPaths_;
    }

protected:
    DerivationT<Input> drv_;
    std::map<OutputName, StorePath, std::less<>> outputPaths_;
};

template<typename Input>
class Derivation;

template<>
class Derivation<SingleDerivedPath> final : public DerivationCommon<SingleDerivedPath>
{
public:
    using DerivationCommon::DerivationCommon;
};

template<>
class Derivation<StorePath> final : public DerivationCommon<StorePath>
{
public:
    using DerivationCommon::DerivationCommon;

    /**
     * @throws Error if the derivation has options this version of Nix
     * does not know, since then it cannot be built faithfully.
     */
    DesugaredEnv desugarEnv(
        Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths)
        const override;
};

} // namespace nix::derivation::v2
