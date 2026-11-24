#include "nix/store/derivation/v2.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/derivation/structured-attrs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/hash.hh"

#include <nlohmann/json.hpp>

namespace nix::derivation::v2 {

template<typename Input>
DerivationCommon<Input>::DerivationCommon(
    DerivationT<Input> drv, std::map<OutputName, StorePath, std::less<>> outputPaths)
    : drv_(std::move(drv))
    , outputPaths_(std::move(outputPaths))
{
}

template<typename Input>
const std::string & DerivationCommon<Input>::name() const
{
    return drv_.name;
}

template<typename Input>
const std::string & DerivationCommon<Input>::platform() const
{
    return drv_.platform;
}

static std::set<SingleDerivedPath> inputSet(const FullInputs & inputs)
{
    return inputs.toSet();
}

static StorePathSet inputSet(const StorePathSet & inputs)
{
    return inputs;
}

template<typename Input>
InputSet<Input> DerivationCommon<Input>::inputs() const
{
    return inputSet(drv_.inputs);
}

template<typename Input>
std::map<OutputName, OutputInfo<Input>, std::less<>> DerivationCommon<Input>::outputs() const
{
    std::map<OutputName, OutputInfo<Input>, std::less<>> res;
    for (auto & [outputName, output] : drv_.outputs) {
        auto & checks = output.options.checks;
        res.emplace(
            outputName,
            OutputInfo<Input>{
                .output = std::visit(
                    overloaded{
                        [&](const InputAddressed &) -> derivation::Output {
                            /* Without its path, the output is deferred until
                               the derivation is resolved. */
                            if (auto i = outputPaths_.find(outputName); i != outputPaths_.end())
                                return derivation::Output::InputAddressed{.path = i->second};
                            return derivation::Output::Deferred{};
                        },
                        [](const auto & o) -> derivation::Output { return o; },
                    },
                    output.raw),
                .options =
                    {
                        .checks = checks == OutputChecks<Input>{} ? std::nullopt : std::optional{checks},
                        .unsafeDiscardReferences = output.options.unsafeDiscardReferences,
                    },
            });
    }
    return res;
}

template<typename Input>
const CommonTopOptions<Input> & DerivationCommon<Input>::options() const
{
    return drv_.options;
}

/**
 * Write out a reference in an interpolated string, as `render` says.
 */
static std::string renderReference(const OutputReference & ref, const fun<std::string(const OutputName &)> & render)
{
    return render(ref.name);
}

static std::string renderReference(
    const Reference<SingleDerivedPath> & ref, const fun<std::string(const DrvRef<SingleDerivedPath> &)> & render)
{
    return render(ref);
}

template<typename Input>
static std::string renderString(const Interpolated<Input> & s, const typename Interface<Input>::Render & render)
{
    return s.render([&](const auto & ref) { return renderReference(ref, render); });
}

template<typename Input>
std::string DerivationCommon<Input>::builder(const Render & render) const
{
    return renderString<Input>(drv_.builder, render);
}

template<typename Input>
Strings DerivationCommon<Input>::args(const Render & render) const
{
    Strings res;
    for (auto & a : drv_.args)
        res.push_back(renderString<Input>(a, render));
    return res;
}

template<typename Input>
std::map<std::string, RenderedEnvVar, std::less<>> DerivationCommon<Input>::env(const Render & render) const
{
    std::map<std::string, RenderedEnvVar, std::less<>> res;
    for (auto & [name, var] : drv_.env)
        res.emplace(
            name,
            RenderedEnvVar{
                .value = renderString<Input>(var.value, render),
                .passAsFile = var.passAsFile,
            });
    return res;
}

template class DerivationCommon<SingleDerivedPath>;
template class DerivationCommon<StorePath>;

DesugaredEnv Derivation<StorePath>::desugarEnv(
    Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths) const
{
    /* Unknown options can be read, written, and copied, but not built
       with, since building without them would not be faithful. */
    auto check = [&](const UnknownOptions & unknown) {
        if (!unknown.empty())
            throw Error(
                "derivation '%s' has the option '%s', which this version of Nix does not know, so it cannot be built",
                drv_.name,
                unknown.begin()->first);
    };
    check(drv_.options.unknown);
    for (auto & [_, output] : drv_.outputs)
        check(output.options.unknown);

    DesugaredEnv res;

    /* Every environment variable is set directly, or via a file, as its
       *pass as file* flag says. None has any special meaning. */
    for (auto & [envName, var] : env(render)) {
        if (!var.passAsFile)
            res.variables.insert_or_assign(envName, DesugaredEnv::EnvEntry{.value = std::move(var.value)});
        else
            res.atFileEnvPair(
                envName + "Path",
                ".attr-" + hashString(HashAlgorithm::SHA256, envName).to_string(HashFormat::Nix32, false)) =
                std::move(var.value);
    }

    /* The references graph of each export is a JSON file of its own. */
    for (auto & [fileName, storePaths] : drv_.options.exportReferencesGraph)
        res.extraFiles.insert_or_assign(
            fileName, exportReferencesGraphToJSON(store, store.exportReferences(storePaths, inputPaths)).dump());

    return res;
}

} // namespace nix::derivation::v2
