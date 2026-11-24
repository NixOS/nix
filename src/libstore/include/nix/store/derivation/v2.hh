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

#include "nix/store/derivation/full-inputs.hh"
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
 * What an `"input"` or `"output"` piece of an interpolated string, or
 * an element of an output check, refers to: one of the derivation's
 * own outputs, by name, or one of its inputs, by deriving path.
 */
using Reference = DrvRef<SingleDerivedPath>;

/**
 * A string of literal text and references.
 *
 * The pieces are kept in canonical form --- no empty literal, and no
 * two literals next to each other --- by only being built up through
 * `append`, so that each interpolated string has exactly one
 * representation, as it has exactly one encoding.
 */
class Interpolated
{
public:
    using Piece = std::variant<std::string, Reference>;

    Interpolated() = default;

    explicit Interpolated(std::string literal)
    {
        append(std::move(literal));
    }

    void append(std::string literal);

    void append(Reference reference);

    const std::vector<Piece> & pieces() const
    {
        return pieces_;
    }

    /**
     * Replace each reference by the string `resolve` gives for it, and
     * concatenate.
     */
    std::string render(fun<std::string(const Reference &)> resolve) const;

    bool operator==(const Interpolated &) const = default;

private:
    std::vector<Piece> pieces_;
};

/**
 * An environment variable's value, with its *pass as file* flag.
 */
struct EnvVar
{
    Interpolated value;
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
struct OutputOptions
{
    OutputChecks<SingleDerivedPath> checks;
    bool unsafeDiscardReferences = false;
    UnknownOptions unknown;

    bool operator==(const OutputOptions &) const = default;
};

/**
 * The options of the derivation as a whole. Version 2 derivations have
 * no checks for all outputs at once.
 */
struct TopOptions : CommonTopOptions<SingleDerivedPath>
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

struct Output
{
    std::variant<InputAddressed, derivation::Output::CAFloating, derivation::Output::Impure> raw;
    OutputOptions options;

    bool operator==(const Output &) const = default;
};

struct Derivation
{
    std::string name;
    std::map<OutputName, Output, std::less<>> outputs;
    FullInputs inputs;
    std::string platform;
    Interpolated builder;
    std::vector<Interpolated> args;
    std::map<std::string, EnvVar, std::less<>> env;
    TopOptions options;

    bool operator==(const Derivation &) const = default;
};

} // namespace nix::derivation::v2
