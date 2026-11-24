#pragma once
///@file
///
/// The interface to derivations that the rest of Nix works on, whatever
/// their [version](@docroot@/store/derivation/versions.md).
///
/// It is all getters and no fields: a version 1 or version 2 derivation
/// simply *is* one, by implementing it, rather than being converted to
/// some version-independent form. Only the few places that deal in a
/// version's own formats look past it.

#include "nix/store/derivation/output.hh"
#include "nix/store/derivation/options.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/fun.hh"

namespace nix {
class Store;
struct DesugaredEnv;
} // namespace nix

namespace nix::derivation {

/**
 * The inputs of a derivation whose inputs are `Input`s.
 */
template<typename Input>
using InputSet = std::set<Input>;

/**
 * How a reference, in one of a derivation's strings, is to be written
 * out: what a string refers to, given that the strings of a basic
 * derivation only refer to its own outputs, and those of a full
 * derivation also to its inputs.
 */
template<typename Input>
struct RenderReference;

template<>
struct RenderReference<StorePath>
{
    using type = fun<std::string(const OutputName &)>;
};

template<>
struct RenderReference<SingleDerivedPath>
{
    using type = fun<std::string(const DrvRef<SingleDerivedPath> &)>;
};

/**
 * An environment variable, written out.
 */
struct RenderedEnvVar
{
    std::string value;
    bool passAsFile = false;

    bool operator==(const RenderedEnvVar &) const = default;
};

/**
 * An output, as the rest of Nix sees it: how it is addressed, and its
 * per-output options. Checks given once for all outputs, as version 1
 * derivations without structured attributes do, are those of each.
 */
template<typename Input>
struct OutputInfo
{
    Output output;
    OutputOptions<Input> options;

    bool operator==(const OutputInfo &) const = default;
};

/**
 * What only a basic derivation can do: it is the kind that is built.
 */
template<typename Input>
class BasicOnly
{
protected:
    ~BasicOnly() = default;
};

template<>
class BasicOnly<StorePath>
{
public:
    /**
     * The environment the builder is to be run with, and the files
     * to be put in its build directory alongside: how they are made from
     * the environment variables and options is up to each version (and,
     * for version 1, its mode).
     *
     * @param render How to write out each reference to an output.
     * @param inputPaths The closure of the inputs.
     */
    virtual DesugaredEnv
    desugarEnv(Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths)
        const = 0;

protected:
    virtual ~BasicOnly();
};

/**
 * A derivation, of either version.
 *
 * @tparam Input `SingleDerivedPath` for a full derivation, whose inputs
 * may be outputs of other derivations; `StorePath` for a basic one,
 * whose inputs are all store paths.
 */
template<typename Input>
class Interface : public BasicOnly<Input>
{
public:
    using Render = typename RenderReference<Input>::type;

    virtual ~Interface() = default;

    /**
     * The name of the derivation, without `.drv`.
     */
    virtual const std::string & name() const = 0;

    virtual const std::string & platform() const = 0;

    virtual InputSet<Input> inputs() const = 0;

    virtual std::map<OutputName, OutputInfo<Input>, std::less<>> outputs() const = 0;

    /**
     * The options of the derivation as a whole.
     */
    virtual const CommonTopOptions<Input> & options() const = 0;

    /**
     * @param render How to write out each reference.
     */
    virtual std::string builder(const Render & render) const = 0;

    virtual Strings args(const Render & render) const = 0;

    virtual std::map<std::string, RenderedEnvVar, std::less<>> env(const Render & render) const = 0;

    /* Derived from the above, for every version alike. */

    StringSet outputNames() const;

    bool isBuiltin() const;

    StringSet getRequiredSystemFeatures() const;

    bool useUidRange() const;

protected:
    Interface() = default;
    Interface(const Interface &) = default;
    Interface & operator=(const Interface &) = default;
};

using FullInterface = Interface<SingleDerivedPath>;
using BasicInterface = Interface<StorePath>;

} // namespace nix::derivation
