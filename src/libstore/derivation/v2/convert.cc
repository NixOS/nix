#include "nix/store/derivation/v2/convert.hh"
#include "nix/store/derivation/v2/aterm.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

namespace nix::derivation::v2 {

/* --------------------------------------------------------------------------
   Resolution
   -------------------------------------------------------------------------- */

template<typename From, typename To>
static std::set<Reference<To>> mapReferences(const std::set<Reference<From>> & refs, auto f)
{
    std::set<Reference<To>> res;
    for (auto & r : refs)
        res.insert(std::visit(
            overloaded{
                [](const OutputName & name) -> Reference<To> { return name; },
                [&](const From & p) -> Reference<To> { return f(p); },
            },
            r));
    return res;
}

template<typename From, typename To>
static OutputChecks<To> mapChecks(const OutputChecks<From> & c, auto f)
{
    return {
        .ignoreSelfRefs = c.ignoreSelfRefs,
        .maxSize = c.maxSize,
        .maxClosureSize = c.maxClosureSize,
        .allowedReferences = c.allowedReferences
                                 ? std::optional{mapReferences<From, To>(*c.allowedReferences, f)}
                                 : std::nullopt,
        .disallowedReferences = mapReferences<From, To>(c.disallowedReferences, f),
        .allowedRequisites = c.allowedRequisites
                                 ? std::optional{mapReferences<From, To>(*c.allowedRequisites, f)}
                                 : std::nullopt,
        .disallowedRequisites = mapReferences<From, To>(c.disallowedRequisites, f),
    };
}

template<typename From, typename To>
static TopOptions<To> mapTopOptions(const CommonTopOptions<From> & o, auto f)
{
    TopOptions<To> res;
    for (auto & [fileName, paths] : o.exportReferencesGraph) {
        std::set<To> ps;
        for (auto & p : paths)
            ps.insert(f(p));
        res.exportReferencesGraph.emplace(fileName, std::move(ps));
    }
    res.additionalSandboxProfile = o.additionalSandboxProfile;
    res.noChroot = o.noChroot;
    res.impureHostDeps = o.impureHostDeps;
    res.impureEnvVars = o.impureEnvVars;
    res.allowLocalNetworking = o.allowLocalNetworking;
    res.requiredSystemFeatures = o.requiredSystemFeatures;
    res.preferLocalBuild = o.preferLocalBuild;
    res.allowSubstitutes = o.allowSubstitutes;
    return res;
}

std::optional<Basic> resolve(const StoreDirConfig & store, const Full & drv, PathOf pathOf)
{
    std::map<SingleDerivedPath, StorePath> resolved;
    for (auto & input : drv.inputs.toSet()) {
        auto path = std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & o) -> std::optional<StorePath> { return o.path; },
                [&](const SingleDerivedPath::Built & b) { return pathOf(b); },
            },
            input.raw());
        if (!path)
            return std::nullopt;
        resolved.emplace(input, *path);
    }
    auto pathFor = [&](const SingleDerivedPath & p) { return resolved.at(p); };

    auto string = [&](const Interpolated<SingleDerivedPath> & s) {
        Interpolated<StorePath> res;
        for (auto & piece : s.pieces())
            std::visit(
                overloaded{
                    [&](const std::string & literal) { res.append(literal); },
                    [&](const Reference<SingleDerivedPath> & ref) {
                        std::visit(
                            overloaded{
                                [&](const OutputName & name) { res.append(OutputReference{name}); },
                                [&](const SingleDerivedPath & p) { res.append(store.printStorePath(pathFor(p))); },
                            },
                            ref);
                    },
                },
                piece);
        return res;
    };

    Basic res{
        .name = drv.name,
        .platform = drv.platform,
        .builder = string(drv.builder),
    };
    for (auto & [_, path] : resolved)
        res.inputs.insert(path);
    for (auto & a : drv.args)
        res.args.push_back(string(a));
    for (auto & [name, var] : drv.env)
        res.env.emplace(name, EnvVar<StorePath>{.value = string(var.value), .passAsFile = var.passAsFile});
    res.options = mapTopOptions<SingleDerivedPath, StorePath>(drv.options, pathFor);
    res.options.unknown = drv.options.unknown;
    for (auto & [outputName, output] : drv.outputs)
        res.outputs.emplace(
            outputName,
            Output<StorePath>{
                .raw = output.raw,
                .options =
                    {
                        .checks = mapChecks<SingleDerivedPath, StorePath>(output.options.checks, pathFor),
                        .unsafeDiscardReferences = output.options.unsafeDiscardReferences,
                        .unknown = output.options.unknown,
                    },
            });
    return res;
}

PathOf staticPathOf(Store & store)
{
    return [&store](const SingleDerivedPath::Built & b) -> std::optional<StorePath> {
        auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&b.drvPath->raw());
        if (!opaque)
            /* A higher-order input never has a static store path. */
            return std::nullopt;
        auto input = store.readInvalidDerivation(opaque->path);
        auto i = input.outputs.find(b.output);
        if (i == input.outputs.end())
            throw Error("derivation '%s' has no output '%s'", store.printStorePath(opaque->path), b.output);
        return std::visit(
            overloaded{
                [&](const derivation::Output::InputAddressed & ia) -> std::optional<StorePath> { return ia.path; },
                [&](const derivation::Output::CAFixed & f) -> std::optional<StorePath> {
                    return f.path(store, input.name, b.output);
                },
                [&](const auto &) -> std::optional<StorePath> { return std::nullopt; },
            },
            i->second.output.raw);
    };
}

/* --------------------------------------------------------------------------
   Input addressing
   -------------------------------------------------------------------------- */

template<typename Input>
static bool inputAddressed(const DerivationT<Input> & drv)
{
    for (auto & [_, output] : drv.outputs)
        if (!std::holds_alternative<InputAddressed>(output.raw))
            return false;
    return !drv.outputs.empty();
}

std::map<OutputName, StorePath, std::less<>> outputPaths(const StoreDirConfig & store, const Basic & drv)
{
    std::map<OutputName, StorePath, std::less<>> res;
    if (!inputAddressed(drv))
        return res;
    /* The derivation's own output paths are left empty in the hashed
       form, as they are what is being computed; since version 2
       derivations refer to their own outputs only by reference, that
       is all the masking of outputs there is. */
    auto hash = hashString(HashAlgorithm::SHA256, unparse(drv, store, {}));
    for (auto & [outputName, _] : drv.outputs)
        res.emplace(outputName, store.makeOutputPath(outputName, hash, drv.name));
    return res;
}

std::optional<std::map<OutputName, StorePath, std::less<>>>
staticOutputPaths(const StoreDirConfig & store, const Full & drv, PathOf staticPathOf)
{
    if (!inputAddressed(drv))
        return std::map<OutputName, StorePath, std::less<>>{};
    auto resolved = resolve(store, drv, staticPathOf);
    if (!resolved)
        return std::nullopt;
    return outputPaths(store, *resolved);
}

/* --------------------------------------------------------------------------
   Conversion to and from the elaborated form
   -------------------------------------------------------------------------- */

static void checkNoUnknownOptions(const Full & drv)
{
    auto check = [&](const UnknownOptions & unknown) {
        if (!unknown.empty())
            throw Error(
                "derivation '%s' has the option '%s', which this version of Nix does not know, so it cannot be built",
                drv.name,
                unknown.begin()->first);
    };
    check(drv.options.unknown);
    for (auto & [_, output] : drv.outputs)
        check(output.options.unknown);
}

derivation::Full toElaborated(const StoreDirConfig & store, const Full & drv, PathOf staticPathOf)
{
    checkNoUnknownOptions(drv);

    /* Inputs with static store paths are resolved right away, as the
       build machinery does not resolve input-addressed derivations. */
    std::map<SingleDerivedPath, SingleDerivedPath> resolved;
    for (auto & input : drv.inputs.toSet())
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw()))
            if (auto path = staticPathOf(*built))
                resolved.emplace(input, SingleDerivedPath::Opaque{*path});
    auto resolve1 = [&](const SingleDerivedPath & p) {
        auto i = resolved.find(p);
        return i == resolved.end() ? p : i->second;
    };

    auto render = [&](const Interpolated<SingleDerivedPath> & s) {
        return s.render([&](const Reference<SingleDerivedPath> & ref) {
            return std::visit(
                overloaded{
                    [&](const OutputName & name) { return hashPlaceholder(name); },
                    [&](const SingleDerivedPath & p) {
                        return std::visit(
                            overloaded{
                                [&](const SingleDerivedPath::Opaque & o) { return store.printStorePath(o.path); },
                                [&](const SingleDerivedPath::Built & b) {
                                    return DownstreamPlaceholder::fromSingleDerivedPathBuilt(b).render();
                                },
                            },
                            resolve1(p).raw());
                    },
                },
                ref);
        });
    };

    auto outputPaths = staticOutputPaths(store, drv, staticPathOf);

    derivation::Full res{
        .name = drv.name,
        .inputs = drv.inputs.toSet(),
        .platform = drv.platform,
        .builder = render(drv.builder),
        .version = Version::V2,
    };
    for (auto & a : drv.args)
        res.args.push_back(render(a));
    for (auto & [name, var] : drv.env)
        res.env.emplace(name, EnvValue{.value = render(var.value), .passAsFile = var.passAsFile});

    static_cast<CommonTopOptions<SingleDerivedPath> &>(res.options) =
        mapTopOptions<SingleDerivedPath, SingleDerivedPath>(drv.options, resolve1);

    for (auto & [outputName, output] : drv.outputs) {
        auto checks = mapChecks<SingleDerivedPath, SingleDerivedPath>(output.options.checks, resolve1);
        res.outputs.emplace(
            outputName,
            OutputWithOptions<SingleDerivedPath, derivation::Output>{
                .output = std::visit(
                    overloaded{
                        [&](const InputAddressed &) -> derivation::Output {
                            if (outputPaths)
                                return derivation::Output::InputAddressed{.path = outputPaths->at(outputName)};
                            return derivation::Output::Deferred{};
                        },
                        [&](const auto & o) -> derivation::Output { return o; },
                    },
                    output.raw),
                .options =
                    {
                        .checks = checks == OutputChecks<SingleDerivedPath>{} ? std::nullopt : std::optional{checks},
                        .unsafeDiscardReferences = output.options.unsafeDiscardReferences,
                    },
            });
    }

    return res;
}

static StorePath toStorePath(const StorePath & p)
{
    return p;
}

static StorePath toStorePath(const SingleDerivedPath & p)
{
    auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&p.raw());
    if (!opaque)
        throw Error("a basic version 2 derivation cannot have an input that is the output of a derivation");
    return opaque->path;
}

template<typename Input>
Basic fromElaborated(const StoreDirConfig & store, const derivation::Derivation<Input> & drv)
{
    assert(drv.version == Version::V2);

    if (drv.structuredAttrs)
        throw Error("version 2 derivation '%s' cannot have structured attributes", drv.name);
    if (drv.options.allOutputChecks)
        throw Error("version 2 derivation '%s' cannot have output checks for all outputs at once", drv.name);

    /* The only placeholders that can remain in a resolved derivation
       are the (internal) ones `toElaborated` introduced for its own
       outputs. */
    std::vector<std::pair<std::string, OutputName>> placeholders;
    for (auto & [outputName, _] : drv.outputs)
        placeholders.emplace_back(hashPlaceholder(outputName), outputName);

    auto interpolate = [&](std::string_view s) {
        Interpolated<StorePath> res;
        while (!s.empty()) {
            size_t best = std::string_view::npos;
            const std::pair<std::string, OutputName> * which = nullptr;
            for (auto & entry : placeholders) {
                auto i = s.find(entry.first);
                if (i < best) {
                    best = i;
                    which = &entry;
                }
            }
            if (!which) {
                res.append(std::string{s});
                break;
            }
            res.append(std::string{s.substr(0, best)});
            res.append(OutputReference{which->second});
            s.remove_prefix(best + which->first.size());
        }
        return res;
    };

    auto path = [](const Input & p) { return toStorePath(p); };

    Basic res{
        .name = drv.name,
        .platform = drv.platform,
        .builder = interpolate(drv.builder),
    };
    for (auto & input : drv.inputs)
        res.inputs.insert(toStorePath(input));
    for (auto & a : drv.args)
        res.args.push_back(interpolate(a));
    for (auto & [name, var] : drv.env)
        res.env.emplace(name, EnvVar<StorePath>{.value = interpolate(var.value), .passAsFile = var.passAsFile});
    res.options = mapTopOptions<Input, StorePath>(drv.options, path);

    using Raw = decltype(Output<StorePath>::raw);
    for (auto & [outputName, output] : drv.outputs)
        res.outputs.emplace(
            outputName,
            Output<StorePath>{
                .raw = std::visit(
                    overloaded{
                        [&](const derivation::Output::InputAddressed &) -> Raw { return InputAddressed{}; },
                        [&](const derivation::Output::Deferred &) -> Raw { return InputAddressed{}; },
                        [&](const derivation::Output::CAFloating & o) -> Raw { return o; },
                        [&](const derivation::Output::Impure & o) -> Raw { return o; },
                        [&](const derivation::Output::CAFixed &) -> Raw {
                            throw Error(
                                "version 2 derivation '%s' cannot yet have fixed content-addressed outputs", drv.name);
                        },
                    },
                    output.output.raw),
                .options =
                    {
                        .checks = output.options.checks ? mapChecks<Input, StorePath>(*output.options.checks, path)
                                                        : OutputChecks<StorePath>{},
                        .unsafeDiscardReferences = output.options.unsafeDiscardReferences,
                    },
            });

    return res;
}

template Basic fromElaborated(const StoreDirConfig &, const derivation::Full &);
template Basic fromElaborated(const StoreDirConfig &, const derivation::Basic &);

/* --------------------------------------------------------------------------
   Writing to the store
   -------------------------------------------------------------------------- */

static std::tuple<std::string, std::string, StorePathSet, StorePath> infoFor(Store & store, const Full & drv)
{
    auto outputPaths =
        staticOutputPaths(store, drv, staticPathOf(store)).value_or(std::map<OutputName, StorePath, std::less<>>{});
    StorePathSet references = drv.inputs.srcs;
    for (auto & [drvPath, _] : drv.inputs.drvs.map)
        references.insert(drvPath);
    auto suffix = drv.name + std::string{drvExtension};
    auto contents = unparse(drv, store, outputPaths);
    auto ca = TextInfo{.hash = hashString(HashAlgorithm::SHA256, contents), .references = references};
    auto path = store.makeFixedOutputPathFromCA(suffix, ca);
    return {std::move(suffix), std::move(contents), std::move(references), std::move(path)};
}

StorePath computeStorePath(Store & store, const Full & drv)
{
    return std::get<3>(infoFor(store, drv));
}

StorePath writeDerivation(Store & store, const Full & drv, RepairFlag repair)
{
    return store.writeDerivation(drv, repair);
}

} // namespace nix::derivation::v2

namespace nix {

StorePath Store::writeDerivation(const derivation::v2::Full & drv, RepairFlag repair)
{
    auto [suffix, contents, references, path] = derivation::v2::infoFor(*this, drv);

    addTempRoot(path);
    if (isValidPath(path) && !repair)
        return path;

    StringSource s{contents};
    auto path2 = addToStoreFromDump(
        s,
        suffix,
        FileSerialisationMethod::Flat,
        ContentAddressMethod::Raw::Text,
        HashAlgorithm::SHA256,
        references,
        repair);
    assert(path2 == path);
    return path;
}

} // namespace nix
