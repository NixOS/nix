#include "nix/store/derivation/v1.hh"
#include "nix/store/derivation/structured-attrs.hh"
#include "nix/store/derivation/v1/elaborate.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/store-api.hh"
#include "nix/util/hash.hh"

namespace nix::derivation::v1 {

/**
 * The traditional-shape decoding of a derivation's options, shared by
 * both modes; each then keeps only what it can express.
 */
template<typename Inputs>
static auto decode(
    const TraditionalParsedT<Inputs> & parsed,
    std::string_view name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings,
    bool shouldWarn)
{
    return decodeTraditional(parsed, store, name, xpSettings, shouldWarn);
}

template<typename Inputs>
EnvDerivationCommon<Inputs>::EnvDerivationCommon(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings,
    bool shouldWarn)
    : Common<Inputs>(std::move(parsed), std::move(name))
{
    auto drv = decode(this->parsed_, this->name_, store, xpSettings, shouldWarn);
    assert(!drv.structuredAttrs);
    options_ = drv.options;
    allOutputChecks_ = drv.options.allOutputChecks;
    for (auto & [varName, var] : drv.env)
        if (var.passAsFile)
            passAsFile_.insert(varName);
}

template<typename Inputs>
StructuredAttrsDerivationCommon<Inputs>::StructuredAttrsDerivationCommon(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings,
    bool shouldWarn)
    : Common<Inputs>(std::move(parsed), std::move(name))
{
    auto drv = decode(this->parsed_, this->name_, store, xpSettings, shouldWarn);
    assert(drv.structuredAttrs);
    /* With structured attributes, the top-level check attributes have
       no effect, so there are no checks for all outputs at once. */
    assert(!drv.options.allOutputChecks);
    structuredAttrs_ = std::move(*drv.structuredAttrs);
    options_ = drv.options;
    for (auto & [outputName, output] : drv.outputs)
        outputOptions_.emplace(outputName, output.options);
}

template<typename Inputs>
DerivationT<Inputs> fromParsed(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings,
    bool shouldWarn)
{
    if (parsed.env.contains(std::string{StructuredAttrs::envVarName}))
        return StructuredAttrsDerivation<Inputs>{std::move(parsed), std::move(name), store, xpSettings, shouldWarn};
    return EnvDerivation<Inputs>{std::move(parsed), std::move(name), store, xpSettings, shouldWarn};
}

static std::set<SingleDerivedPath> inputSet(const FullInputs & inputs)
{
    return inputs.toSet();
}

static StorePathSet inputSet(const StorePathSet & inputs)
{
    return inputs;
}

template<typename Inputs>
InputSet<typename Common<Inputs>::Input> Common<Inputs>::inputs() const
{
    return inputSet(parsed_.inputs);
}

/**
 * The reference to the output of a derivation's own, in the form
 * `render` takes for a derivation with `Input`s.
 */
static DrvRef<SingleDerivedPath> outputReference(const OutputName & name, const SingleDerivedPath *)
{
    return name;
}

static OutputName outputReference(const OutputName & name, const StorePath *)
{
    return name;
}

template<typename Inputs>
std::string Common<Inputs>::render(std::string s, const Render & render) const
{
    StringMap rewrites;
    for (auto & [outputName, _] : parsed_.outputs)
        rewrites.emplace(hashPlaceholder(outputName), render(outputReference(outputName, (const Input *) nullptr)));
    return rewriteStrings(std::move(s), rewrites);
}

template<typename Inputs>
std::string Common<Inputs>::builder(const Render & render) const
{
    return this->render(parsed_.builder, render);
}

template<typename Inputs>
Strings Common<Inputs>::args(const Render & render) const
{
    Strings res;
    for (auto & a : parsed_.args)
        res.push_back(this->render(a, render));
    return res;
}

template<typename Inputs>
auto EnvDerivationCommon<Inputs>::outputs() const -> std::map<OutputName, OutputInfo<Input>, std::less<>>
{
    std::map<OutputName, OutputInfo<Input>, std::less<>> res;
    for (auto & [outputName, output] : this->parsed_.outputs)
        res.emplace(
            outputName,
            OutputInfo<Input>{
                .output = output,
                .options{.checks = allOutputChecks_},
            });
    return res;
}

template<typename Inputs>
std::map<std::string, RenderedEnvVar, std::less<>> EnvDerivationCommon<Inputs>::env(const Render & render) const
{
    std::map<std::string, RenderedEnvVar, std::less<>> res;
    for (auto & [varName, value] : this->parsed_.env)
        res.emplace(
            varName,
            RenderedEnvVar{
                .value = this->render(value, render),
                .passAsFile = passAsFile_.contains(varName),
            });
    return res;
}

template<typename Inputs>
auto StructuredAttrsDerivationCommon<Inputs>::outputs() const -> std::map<OutputName, OutputInfo<Input>, std::less<>>
{
    std::map<OutputName, OutputInfo<Input>, std::less<>> res;
    for (auto & [outputName, output] : this->parsed_.outputs) {
        auto i = outputOptions_.find(outputName);
        res.emplace(
            outputName,
            OutputInfo<Input>{
                .output = output,
                .options = i == outputOptions_.end() ? OutputOptions<Input>{} : i->second,
            });
    }
    return res;
}

template<typename Inputs>
std::map<std::string, RenderedEnvVar, std::less<>>
StructuredAttrsDerivationCommon<Inputs>::env(const Render & render) const
{
    std::map<std::string, RenderedEnvVar, std::less<>> res;
    for (auto & [varName, value] : this->parsed_.env)
        if (varName != StructuredAttrs::envVarName)
            res.emplace(varName, RenderedEnvVar{.value = this->render(value, render)});
    return res;
}

EnvDerivation<FullInputs>::~EnvDerivation() = default;
StructuredAttrsDerivation<FullInputs>::~StructuredAttrsDerivation() = default;

/**
 * What was parsed of a version 1 derivation, with each input derivation
 * output replaced by its store path. Its strings already have those
 * paths in them: a version 1 derivation only depends on outputs whose
 * paths are known in advance.
 */
static std::optional<BasicTraditionalParsed> resolveParsed(const TraditionalParsed & parsed, const PathOf & pathOf)
{
    StorePathSet inputs;
    for (auto & input : parsed.inputs.toSet()) {
        auto path = std::visit(
            overloaded{
                [](const SingleDerivedPath::Opaque & o) -> std::optional<StorePath> { return o.path; },
                [&](const SingleDerivedPath::Built & b) { return pathOf(b); },
            },
            input.raw());
        if (!path)
            return std::nullopt;
        inputs.insert(std::move(*path));
    }
    return BasicTraditionalParsed{
        .outputs = parsed.outputs,
        .inputs = std::move(inputs),
        .platform = parsed.platform,
        .builder = parsed.builder,
        .args = parsed.args,
        .env = parsed.env,
    };
}

std::shared_ptr<const Interface<StorePath>>
EnvDerivation<FullInputs>::resolve(const StoreDirConfig & store, const PathOf & pathOf) const
{
    auto basic = resolveParsed(parsed_, pathOf);
    if (!basic)
        return nullptr;
    return std::make_shared<const EnvDerivation<StorePathSet>>(std::move(*basic), name_, store);
}

std::shared_ptr<const Interface<StorePath>>
StructuredAttrsDerivation<FullInputs>::resolve(const StoreDirConfig & store, const PathOf & pathOf) const
{
    auto basic = resolveParsed(parsed_, pathOf);
    if (!basic)
        return nullptr;
    return std::make_shared<const StructuredAttrsDerivation<StorePathSet>>(std::move(*basic), name_, store);
}

DesugaredEnv EnvDerivation<StorePathSet>::desugarEnv(
    Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths) const
{
    DesugaredEnv res;

    /* Set all bindings either directly in the environment or via a
       file, as specified by `passAsFile`. */
    for (auto & [envName, envVar] : env(render)) {
        if (!envVar.passAsFile)
            res.variables.insert_or_assign(envName, DesugaredEnv::EnvEntry{.value = std::move(envVar.value)});
        else
            res.atFileEnvPair(
                envName + "Path",
                ".attr-" + hashString(HashAlgorithm::SHA256, envName).to_string(HashFormat::Nix32, false)) =
                std::move(envVar.value);
    }

    /* Write closure info of each export to its file, in the legacy
       textual format. */
    for (auto & [fileName, storePaths] : options().exportReferencesGraph)
        res.extraFiles.insert_or_assign(
            fileName, store.makeValidityRegistration(store.exportReferences(storePaths, inputPaths), false, false));

    return res;
}

DesugaredEnv StructuredAttrsDerivation<StorePathSet>::desugarEnv(
    Store & store, const fun<std::string(const OutputName &)> & render, const StorePathSet & inputPaths) const
{
    DesugaredEnv res;
    auto json = structuredAttrs().prepareStructuredAttrs(
        store, options().exportReferencesGraph, inputPaths, outputNames(), render);
    res.atFileEnvPair("NIX_ATTRS_SH_FILE", ".attrs.sh") = StructuredAttrs::writeShell(json);
    res.atFileEnvPair("NIX_ATTRS_JSON_FILE", ".attrs.json") = static_cast<nlohmann::json>(std::move(json)).dump();
    return res;
}

/**
 * The parts of the shared view that do not depend on the mode.
 */
template<typename Inputs>
static auto common(const TraditionalParsedT<Inputs> & parsed, const std::string & name)
{
    using Input = typename InputOf<Inputs>::type;
    derivation::Derivation<Input> res{
        .name = name,
        .inputs = inputSet(parsed.inputs),
        .platform = parsed.platform,
        .builder = parsed.builder,
        .args = parsed.args,
    };
    for (auto & [outputName, output] : parsed.outputs)
        res.outputs.emplace(outputName, OutputWithOptions<Input, Output>{.output = output});
    return res;
}

template<typename Inputs>
static auto elaboratedImpl(const DerivationT<Inputs> & drv)
{
    return std::visit(
        overloaded{
            [](const EnvDerivation<Inputs> & d) {
                auto res = common(d.parsed(), d.name());
                for (auto & [varName, value] : d.parsed().env)
                    res.env.emplace(varName, EnvValue{.value = value, .passAsFile = d.passAsFile().contains(varName)});
                static_cast<CommonTopOptions<typename InputOf<Inputs>::type> &>(res.options) = d.options();
                res.options.allOutputChecks = d.allOutputChecks();
                return res;
            },
            [](const StructuredAttrsDerivation<Inputs> & d) {
                auto res = common(d.parsed(), d.name());
                for (auto & [varName, value] : d.parsed().env)
                    if (varName != StructuredAttrs::envVarName)
                        res.env.emplace(varName, EnvValue{.value = value});
                res.structuredAttrs = d.structuredAttrs();
                static_cast<CommonTopOptions<typename InputOf<Inputs>::type> &>(res.options) = d.options();
                for (auto & [outputName, options] : d.outputOptions())
                    res.outputs.at(outputName).options = options;
                return res;
            },
        },
        drv);
}

derivation::Full elaborated(const Full & drv)
{
    return elaboratedImpl(drv);
}

derivation::Basic elaborated(const Basic & drv)
{
    return elaboratedImpl(drv);
}

template class Common<FullInputs>;
template class Common<StorePathSet>;
template class EnvDerivationCommon<FullInputs>;
template class EnvDerivationCommon<StorePathSet>;
template class StructuredAttrsDerivationCommon<FullInputs>;
template class StructuredAttrsDerivationCommon<StorePathSet>;

template Full
fromParsed(TraditionalParsed, std::string, const StoreDirConfig &, const ExperimentalFeatureSettings &, bool);
template Basic
fromParsed(BasicTraditionalParsed, std::string, const StoreDirConfig &, const ExperimentalFeatureSettings &, bool);

} // namespace nix::derivation::v1

namespace nix::derivation {

Full elaborate(
    const TraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    return v1::elaborated(v1::fromParsed(aterm, std::string{name}, store, xpSettings));
}

Basic elaborate(
    const BasicTraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    return v1::elaborated(v1::fromParsed(aterm, std::string{name}, store, xpSettings));
}

} // namespace nix::derivation
