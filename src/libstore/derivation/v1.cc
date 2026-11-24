#include "nix/store/derivation/v1.hh"
#include "nix/store/derivation/structured-attrs.hh"
#include "nix/store/derivation/elaborate.hh"

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
    const ExperimentalFeatureSettings & xpSettings)
{
    return decodeTraditional(parsed, store, name, xpSettings);
}

template<typename Inputs>
EnvDerivation<Inputs>::EnvDerivation(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings)
    : Common<Inputs>(std::move(parsed), std::move(name))
{
    auto drv = decode(this->parsed_, this->name_, store, xpSettings);
    assert(!drv.structuredAttrs);
    options_ = drv.options;
    allOutputChecks_ = drv.options.allOutputChecks;
    for (auto & [varName, var] : drv.env)
        if (var.passAsFile)
            passAsFile_.insert(varName);
}

template<typename Inputs>
StructuredAttrsDerivation<Inputs>::StructuredAttrsDerivation(
    TraditionalParsedT<Inputs> parsed,
    std::string name,
    const StoreDirConfig & store,
    const ExperimentalFeatureSettings & xpSettings)
    : Common<Inputs>(std::move(parsed), std::move(name))
{
    auto drv = decode(this->parsed_, this->name_, store, xpSettings);
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
    const ExperimentalFeatureSettings & xpSettings)
{
    if (parsed.env.contains(std::string{StructuredAttrs::envVarName}))
        return StructuredAttrsDerivation<Inputs>{std::move(parsed), std::move(name), store, xpSettings};
    return EnvDerivation<Inputs>{std::move(parsed), std::move(name), store, xpSettings};
}

static std::set<SingleDerivedPath> inputSet(const FullInputs & inputs)
{
    return inputs.toSet();
}

static StorePathSet inputSet(const StorePathSet & inputs)
{
    return inputs;
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

template class EnvDerivation<FullInputs>;
template class EnvDerivation<StorePathSet>;
template class StructuredAttrsDerivation<FullInputs>;
template class StructuredAttrsDerivation<StorePathSet>;

template Full fromParsed(TraditionalParsed, std::string, const StoreDirConfig &, const ExperimentalFeatureSettings &);
template Basic
fromParsed(BasicTraditionalParsed, std::string, const StoreDirConfig &, const ExperimentalFeatureSettings &);

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
