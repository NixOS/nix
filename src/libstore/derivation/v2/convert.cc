#include "nix/store/derivation/v2/convert.hh"
#include "nix/store/derivation/v2/aterm.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/util.hh"

namespace nix::derivation::v2 {

/* --------------------------------------------------------------------------
   Rewriting references
   -------------------------------------------------------------------------- */

static Interpolated mapReferences(const Interpolated & s, auto f)
{
    Interpolated res;
    for (auto & piece : s.pieces())
        std::visit(
            overloaded{
                [&](const std::string & literal) { res.append(literal); },
                [&](const Reference & ref) { res.append(f(ref)); },
            },
            piece);
    return res;
}

static std::set<Reference> mapReferences(const std::set<Reference> & refs, auto f)
{
    std::set<Reference> res;
    for (auto & r : refs)
        res.insert(f(r));
    return res;
}

/**
 * Rewrite every input reference of `drv` --- in its strings, output
 * checks and export references graph --- with `f`, and its inputs
 * accordingly.
 */
static void mapInputReferences(Derivation & drv, fun<SingleDerivedPath(const SingleDerivedPath &)> f)
{
    auto ref = [&](const Reference & r) -> Reference {
        if (auto * p = std::get_if<SingleDerivedPath>(&r))
            return f(*p);
        return r;
    };

    drv.builder = mapReferences(drv.builder, ref);
    for (auto & a : drv.args)
        a = mapReferences(a, ref);
    for (auto & [_, var] : drv.env)
        var.value = mapReferences(var.value, ref);

    for (auto & [_, paths] : drv.options.exportReferencesGraph) {
        std::set<SingleDerivedPath> res;
        for (auto & p : paths)
            res.insert(f(p));
        paths = std::move(res);
    }

    for (auto & [_, output] : drv.outputs) {
        auto & c = output.options.checks;
        if (c.allowedReferences)
            c.allowedReferences = mapReferences(*c.allowedReferences, ref);
        if (c.allowedRequisites)
            c.allowedRequisites = mapReferences(*c.allowedRequisites, ref);
        c.disallowedReferences = mapReferences(c.disallowedReferences, ref);
        c.disallowedRequisites = mapReferences(c.disallowedRequisites, ref);
    }

    std::set<SingleDerivedPath> inputs;
    for (auto & p : drv.inputs.toSet())
        inputs.insert(f(p));
    drv.inputs = FullInputs::fromSet(inputs);
}

bool resolve(Derivation & drv, StaticPathOf staticPathOf)
{
    bool all = true;
    std::map<SingleDerivedPath, SingleDerivedPath> resolved;
    for (auto & input : drv.inputs.toSet())
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw())) {
            if (auto path = staticPathOf(*built))
                resolved.emplace(input, SingleDerivedPath::Opaque{*path});
            else
                all = false;
        }
    mapInputReferences(drv, [&](const SingleDerivedPath & p) {
        auto i = resolved.find(p);
        return i == resolved.end() ? p : i->second;
    });
    return all;
}

/* --------------------------------------------------------------------------
   Input addressing
   -------------------------------------------------------------------------- */

std::optional<std::map<OutputName, StorePath, std::less<>>>
staticOutputPaths(const StoreDirConfig & store, const Derivation & drv, StaticPathOf staticPathOf)
{
    std::map<OutputName, StorePath, std::less<>> res;

    for (auto & [_, output] : drv.outputs)
        if (!std::holds_alternative<InputAddressed>(output.raw))
            return res;

    auto resolved = drv;
    if (!resolve(resolved, staticPathOf))
        return std::nullopt;

    /* The derivation's own output paths are left empty in the hashed
       form, as they are what is being computed; since version 2
       derivations refer to their own outputs only by reference, that
       is all the masking of outputs there is. */
    auto hash = hashString(HashAlgorithm::SHA256, unparse(resolved, store, {}));

    for (auto & [outputName, _] : drv.outputs)
        res.emplace(outputName, store.makeOutputPath(outputName, hash, drv.name));
    return res;
}

/* --------------------------------------------------------------------------
   Conversion to and from the elaborated form
   -------------------------------------------------------------------------- */

static void checkNoUnknownOptions(const Derivation & drv)
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

Full toFull(const StoreDirConfig & store, const Derivation & drv, StaticPathOf staticPathOf)
{
    checkNoUnknownOptions(drv);

    auto resolved = drv;
    resolve(resolved, staticPathOf);
    auto outputPaths = staticOutputPaths(store, drv, staticPathOf);

    auto render = [&](const Interpolated & s) {
        return s.render([&](const Reference & ref) {
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
                            p.raw());
                    },
                },
                ref);
        });
    };

    Full res{
        .name = drv.name,
        .inputs = drv.inputs.toSet(),
        .platform = drv.platform,
        .builder = render(resolved.builder),
        .version = Version::V2,
    };
    for (auto & a : resolved.args)
        res.args.push_back(render(a));
    for (auto & [name, var] : resolved.env)
        res.env.emplace(name, EnvValue{.value = render(var.value), .passAsFile = var.passAsFile});

    res.options = derivation::TopOptions<SingleDerivedPath>{static_cast<const CommonTopOptions<SingleDerivedPath> &>(resolved.options)};

    for (auto & [outputName, output] : resolved.outputs) {
        auto & checks = output.options.checks;
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

static SingleDerivedPath toDerivingPath(const SingleDerivedPath & p)
{
    return p;
}

static SingleDerivedPath toDerivingPath(const StorePath & p)
{
    return SingleDerivedPath::Opaque{p};
}

template<typename Input>
static std::set<Reference> toReferences(const std::set<DrvRef<Input>> & refs)
{
    std::set<Reference> res;
    for (auto & r : refs)
        res.insert(std::visit(
            overloaded{
                [](const OutputName & name) -> Reference { return name; },
                [](const Input & p) -> Reference { return toDerivingPath(p); },
            },
            r));
    return res;
}

template<typename Input>
static OutputChecks<SingleDerivedPath> toChecks(const OutputChecks<Input> & c)
{
    return {
        .ignoreSelfRefs = c.ignoreSelfRefs,
        .maxSize = c.maxSize,
        .maxClosureSize = c.maxClosureSize,
        .allowedReferences = c.allowedReferences ? std::optional{toReferences(*c.allowedReferences)} : std::nullopt,
        .disallowedReferences = toReferences(c.disallowedReferences),
        .allowedRequisites = c.allowedRequisites ? std::optional{toReferences(*c.allowedRequisites)} : std::nullopt,
        .disallowedRequisites = toReferences(c.disallowedRequisites),
    };
}

template<typename Input>
Derivation fromElaborated(const StoreDirConfig & store, const derivation::Derivation<Input> & drv)
{
    assert(drv.version == Version::V2);

    if (drv.structuredAttrs)
        throw Error("version 2 derivation '%s' cannot have structured attributes", drv.name);
    if (drv.options.allOutputChecks)
        throw Error("version 2 derivation '%s' cannot have output checks for all outputs at once", drv.name);

    std::set<SingleDerivedPath> inputs;
    for (auto & input : drv.inputs)
        inputs.insert(toDerivingPath(input));

    /* The (internal) placeholders `toFull` introduced, and the references
       they stand for. */
    std::map<std::string, Reference> placeholders;
    for (auto & [outputName, _] : drv.outputs)
        placeholders.emplace(hashPlaceholder(outputName), OutputName{outputName});
    for (auto & input : inputs)
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw()))
            placeholders.emplace(DownstreamPlaceholder::fromSingleDerivedPathBuilt(*built).render(), input);

    auto interpolate = [&](std::string_view s) {
        Interpolated res;
        while (!s.empty()) {
            size_t best = std::string_view::npos;
            const std::pair<const std::string, Reference> * which = nullptr;
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
            res.append(which->second);
            s.remove_prefix(best + which->first.size());
        }
        return res;
    };

    Derivation res{
        .name = drv.name,
        .inputs = FullInputs::fromSet(inputs),
        .platform = drv.platform,
        .builder = interpolate(drv.builder),
    };
    for (auto & a : drv.args)
        res.args.push_back(interpolate(a));
    for (auto & [name, var] : drv.env)
        res.env.emplace(name, EnvVar{.value = interpolate(var.value), .passAsFile = var.passAsFile});

    auto & o = drv.options;
    auto & ro = res.options;
    for (auto & [fileName, paths] : o.exportReferencesGraph) {
        std::set<SingleDerivedPath> ps;
        for (auto & p : paths)
            ps.insert(toDerivingPath(p));
        ro.exportReferencesGraph.emplace(fileName, std::move(ps));
    }
    ro.additionalSandboxProfile = o.additionalSandboxProfile;
    ro.noChroot = o.noChroot;
    ro.impureHostDeps = o.impureHostDeps;
    ro.impureEnvVars = o.impureEnvVars;
    ro.allowLocalNetworking = o.allowLocalNetworking;
    ro.requiredSystemFeatures = o.requiredSystemFeatures;
    ro.preferLocalBuild = o.preferLocalBuild;
    ro.allowSubstitutes = o.allowSubstitutes;

    for (auto & [outputName, output] : drv.outputs)
        res.outputs.emplace(
            outputName,
            Output{
                .raw = std::visit(
                    overloaded{
                        [&](const derivation::Output::InputAddressed &) -> decltype(Output::raw) {
                            return InputAddressed{};
                        },
                        [&](const derivation::Output::Deferred &) -> decltype(Output::raw) { return InputAddressed{}; },
                        [&](const derivation::Output::CAFloating & o) -> decltype(Output::raw) { return o; },
                        [&](const derivation::Output::Impure & o) -> decltype(Output::raw) { return o; },
                        [&](const derivation::Output::CAFixed &) -> decltype(Output::raw) {
                            throw Error(
                                "version 2 derivation '%s' cannot yet have fixed content-addressed outputs", drv.name);
                        },
                    },
                    output.output.raw),
                .options =
                    {
                        .checks = output.options.checks ? toChecks(*output.options.checks)
                                                        : OutputChecks<SingleDerivedPath>{},
                        .unsafeDiscardReferences = output.options.unsafeDiscardReferences,
                    },
            });

    return res;
}

template Derivation fromElaborated(const StoreDirConfig &, const Full &);
template Derivation fromElaborated(const StoreDirConfig &, const Basic &);

} // namespace nix::derivation::v2

#include "nix/store/store-api.hh"

namespace nix::derivation::v2 {

StaticPathOf staticPathOfStore(Store & store)
{
    return [&store](const SingleDerivedPath::Built & b) -> std::optional<StorePath> {
        auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&b.drvPath->raw());
        if (!opaque)
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

static std::tuple<std::string, std::string, StorePathSet, StorePath> infoFor(
    const StoreDirConfig & store,
    const Derivation & drv,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths)
{
    StorePathSet references = drv.inputs.srcs;
    for (auto & [drvPath, _] : drv.inputs.drvs.map)
        references.insert(drvPath);
    auto suffix = drv.name + std::string{drvExtension};
    auto contents = unparse(drv, store, outputPaths);
    auto ca = TextInfo{.hash = hashString(HashAlgorithm::SHA256, contents), .references = references};
    auto path = store.makeFixedOutputPathFromCA(suffix, ca);
    return {std::move(suffix), std::move(contents), std::move(references), std::move(path)};
}

StorePath computeStorePath(
    const StoreDirConfig & store,
    const Derivation & drv,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths)
{
    return std::get<3>(infoFor(store, drv, outputPaths));
}

StorePath writeDerivation(Store & store, const Derivation & drv, RepairFlag repair)
{
    auto outputPaths = staticOutputPaths(store, drv, staticPathOfStore(store)).value_or(
        std::map<OutputName, StorePath, std::less<>>{});
    auto [suffix, contents, references, path] = infoFor(store, drv, outputPaths);

    store.addTempRoot(path);
    if (store.isValidPath(path) && !repair)
        return path;

    StringSource s{contents};
    auto path2 = store.addToStoreFromDump(
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

} // namespace nix::derivation::v2
