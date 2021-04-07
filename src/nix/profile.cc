#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "archive.hh"
#include "builtins/buildenv.hh"
#include "flake/flakeref.hh"
#include "../nix-env/user-env.hh"
#include "profiles.hh"
#include "names.hh"

#include <nlohmann/json.hpp>
#include <regex>

using namespace nix;

struct ProfileElementSource
{
    FlakeRef originalRef;
    // FIXME: record original attrpath.
    FlakeRef resolvedRef;
    std::string attrPath;
    // FIXME: output names

    bool operator < (const ProfileElementSource & other) const
    {
        return
            std::pair(originalRef.to_string(), attrPath) <
            std::pair(other.originalRef.to_string(), other.attrPath);
    }
};

struct ProfileElement
{
    StorePathSet storePaths;
    std::optional<ProfileElementSource> source;
    bool active = true;
    // FIXME: priority

    std::string describe() const
    {
        if (source)
            return fmt("%s#%s", source->originalRef, source->attrPath);
        StringSet names;
        for (auto & path : storePaths)
            names.insert(DrvName(path.name()).name);
        return concatStringsSep(", ", names);
    }

    std::string versions() const
    {
        StringSet versions;
        for (auto & path : storePaths)
            versions.insert(DrvName(path.name()).version);
        return showVersions(versions);
    }

    bool operator < (const ProfileElement & other) const
    {
        return std::tuple(describe(), storePaths) < std::tuple(other.describe(), other.storePaths);
    }
};

struct ProfileManifest
{
    std::vector<ProfileElement> elements;

    ProfileManifest() { }

    ProfileManifest(EvalState & state, const Path & profile)
    {
        auto manifestPath = profile + "/manifest.json";

        if (pathExists(manifestPath)) {
            auto json = nlohmann::json::parse(readFile(manifestPath));

            auto version = json.value("version", 0);
            if (version != 1)
                throw Error("profile manifest '%s' has unsupported version %d", manifestPath, version);

            for (auto & e : json["elements"]) {
                ProfileElement element;
                for (auto & p : e["storePaths"])
                    element.storePaths.insert(state.store->parseStorePath((std::string) p));
                element.active = e["active"];
                if (e.value("uri", "") != "") {
                    element.source = ProfileElementSource{
                        parseFlakeRef(e["originalUri"]),
                        parseFlakeRef(e["uri"]),
                        e["attrPath"]
                    };
                }
                elements.emplace_back(std::move(element));
            }
        }

        else if (pathExists(profile + "/manifest.nix")) {
            // FIXME: needed because of pure mode; ugly.
            if (state.allowedPaths) {
                state.allowedPaths->insert(state.store->followLinksToStore(profile));
                state.allowedPaths->insert(state.store->followLinksToStore(profile + "/manifest.nix"));
            }

            auto drvInfos = queryInstalled(state, state.store->followLinksToStore(profile));

            for (auto & drvInfo : drvInfos) {
                ProfileElement element;
                element.storePaths = {state.store->parseStorePath(drvInfo.queryOutPath())};
                elements.emplace_back(std::move(element));
            }
        }
    }

    std::string toJSON(Store & store) const
    {
        auto array = nlohmann::json::array();
        for (auto & element : elements) {
            auto paths = nlohmann::json::array();
            for (auto & path : element.storePaths)
                paths.push_back(store.printStorePath(path));
            nlohmann::json obj;
            obj["storePaths"] = paths;
            obj["active"] = element.active;
            if (element.source) {
                obj["originalUri"] = element.source->originalRef.to_string();
                obj["uri"] = element.source->resolvedRef.to_string();
                obj["attrPath"] = element.source->attrPath;
            }
            array.push_back(obj);
        }
        nlohmann::json json;
        json["version"] = 1;
        json["elements"] = array;
        return json.dump();
    }

    StorePath build(ref<Store> store)
    {
        auto tempDir = createTempDir();

        StorePathSet references;

        Packages pkgs;
        for (auto & element : elements) {
            for (auto & path : element.storePaths) {
                if (element.active)
                    pkgs.emplace_back(store->printStorePath(path), true, 5);
                references.insert(path);
            }
        }

        buildProfile(tempDir, std::move(pkgs));

        writeFile(tempDir + "/manifest.json", toJSON(*store));

        /* Add the symlink tree to the store. */
        StringSink sink;
        dumpPath(tempDir, sink);

        auto narHash = hashString(htSHA256, *sink.s);

        ValidPathInfo info {
            *store,
            StorePathDescriptor {
                "profile",
                FixedOutputInfo {
                    {
                        .method = FileIngestionMethod::Recursive,
                        .hash = narHash,
                    },
                    { references },
                },
            },
            narHash,
        };
        info.references = std::move(references);
        info.narSize = sink.s->size();

        auto source = StringSource { *sink.s };
        store->addToStore(info, source);

        return std::move(info.path);
    }

    static void printDiff(const ProfileManifest & prev, const ProfileManifest & cur, std::string_view indent)
    {
        auto prevElems = prev.elements;
        std::sort(prevElems.begin(), prevElems.end());

        auto curElems = cur.elements;
        std::sort(curElems.begin(), curElems.end());

        auto i = prevElems.begin();
        auto j = curElems.begin();

        bool changes = false;

        while (i != prevElems.end() || j != curElems.end()) {
            if (j != curElems.end() && (i == prevElems.end() || i->describe() > j->describe())) {
                std::cout << fmt("%s%s: ∅ -> %s\n", indent, j->describe(), j->versions());
                changes = true;
                ++j;
            }
            else if (i != prevElems.end() && (j == curElems.end() || i->describe() < j->describe())) {
                std::cout << fmt("%s%s: %s -> ∅\n", indent, i->describe(), i->versions());
                changes = true;
                ++i;
            }
            else {
                auto v1 = i->versions();
                auto v2 = j->versions();
                if (v1 != v2) {
                    std::cout << fmt("%s%s: %s -> %s\n", indent, i->describe(), v1, v2);
                    changes = true;
                }
                ++i;
                ++j;
            }
        }

        if (!changes)
            std::cout << fmt("%sNo changes.\n", indent);
    }
};

struct CmdProfileInstall : InstallablesCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "install a package into a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-install.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*getEvalState(), *profile);

        std::vector<DerivedPath> pathsToBuild;

        for (auto & installable : installables) {
            if (auto installable2 = std::dynamic_pointer_cast<InstallableFlake>(installable)) {
                auto [attrPath, resolvedRef, drv] = installable2->toDerivation();

                ProfileElement element;
                if (!drv.outPath)
                    throw UnimplementedError("CA derivations are not yet supported by 'nix profile'");
                element.storePaths = {*drv.outPath}; // FIXME
                element.source = ProfileElementSource{
                    installable2->flakeRef,
                    resolvedRef,
                    attrPath,
                };

                pathsToBuild.push_back(DerivedPath::Built {
                    staticDrvReq(drv.drvPath),
                    StringSet{drv.outputName},
                });

                manifest.elements.emplace_back(std::move(element));
            } else {
                auto buildables = build(store, Realise::Outputs, {installable}, bmNormal);

                for (auto & buildable : buildables) {
                    ProfileElement element;

                    std::visit(overloaded {
                        [&](DerivedPathWithHints::Opaque bo) {
                            pathsToBuild.push_back(bo);
                            element.storePaths.insert(bo.path);
                        },
                        [&](DerivedPathWithHints::Built bfd) {
                            auto drvPath = resolveDerivedPathWithHints(*store, *bfd.drvPath);
                            // TODO: Why are we querying if we know the output
                            // names already? Is it just to figure out what the
                            // default one is?
                            for (auto & output : resolveDerivedPathWithHints(*store, bfd)) {
                                pathsToBuild.push_back(DerivedPath::Built {
                                    staticDrvReq(drvPath),
                                    {output.first},
                                });
                                element.storePaths.insert(output.second);
                            }
                        },
                    }, buildable.raw());

                    manifest.elements.emplace_back(std::move(element));
                }
            }
        }

        store->buildPaths(pathsToBuild);

        updateProfile(manifest.build(store));
    }
};

class MixProfileElementMatchers : virtual Args
{
    std::vector<std::string> _matchers;

public:

    MixProfileElementMatchers()
    {
        expectArgs("elements", &_matchers);
    }

    typedef std::variant<size_t, Path, std::regex> Matcher;

    std::vector<Matcher> getMatchers(ref<Store> store)
    {
        std::vector<Matcher> res;

        for (auto & s : _matchers) {
            if (auto n = string2Int<size_t>(s))
                res.push_back(*n);
            else if (store->isStorePath(s))
                res.push_back(s);
            else
                res.push_back(std::regex(s, std::regex::extended | std::regex::icase));
        }

        return res;
    }

    bool matches(const Store & store, const ProfileElement & element, size_t pos, const std::vector<Matcher> & matchers)
    {
        for (auto & matcher : matchers) {
            if (auto n = std::get_if<size_t>(&matcher)) {
                if (*n == pos) return true;
            } else if (auto path = std::get_if<Path>(&matcher)) {
                if (element.storePaths.count(store.parseStorePath(*path))) return true;
            } else if (auto regex = std::get_if<std::regex>(&matcher)) {
                if (element.source
                    && std::regex_match(element.source->attrPath, *regex))
                    return true;
            }
        }

        return false;
    }
};

struct CmdProfileRemove : virtual EvalCommand, MixDefaultProfile, MixProfileElementMatchers
{
    std::string description() override
    {
        return "remove packages from a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-remove.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest oldManifest(*getEvalState(), *profile);

        auto matchers = getMatchers(store);

        ProfileManifest newManifest;

        for (size_t i = 0; i < oldManifest.elements.size(); ++i) {
            auto & element(oldManifest.elements[i]);
            if (!matches(*store, element, i, matchers))
                newManifest.elements.push_back(std::move(element));
        }

        // FIXME: warn about unused matchers?

        printInfo("removed %d packages, kept %d packages",
            oldManifest.elements.size() - newManifest.elements.size(),
            newManifest.elements.size());

        updateProfile(newManifest.build(store));
    }
};

struct CmdProfileUpgrade : virtual SourceExprCommand, MixDefaultProfile, MixProfileElementMatchers
{
    std::string description() override
    {
        return "upgrade packages using their most recent flake";
    }

    std::string doc() override
    {
        return
          #include "profile-upgrade.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*getEvalState(), *profile);

        auto matchers = getMatchers(store);

        // FIXME: code duplication
        std::vector<DerivedPath> pathsToBuild;

        for (size_t i = 0; i < manifest.elements.size(); ++i) {
            auto & element(manifest.elements[i]);
            if (element.source
                && !element.source->originalRef.input.isImmutable()
                && matches(*store, element, i, matchers))
            {
                Activity act(*logger, lvlChatty, actUnknown,
                    fmt("checking '%s' for updates", element.source->attrPath));

                InstallableFlake installable(
                    this,
                    getEvalState(),
                    FlakeRef(element.source->originalRef),
                    {element.source->attrPath},
                    {},
                    lockFlags);

                auto [attrPath, resolvedRef, drv] = installable.toDerivation();

                if (element.source->resolvedRef == resolvedRef) continue;

                printInfo("upgrading '%s' from flake '%s' to '%s'",
                    element.source->attrPath, element.source->resolvedRef, resolvedRef);

                if (!drv.outPath)
                    throw UnimplementedError("CA derivations are not yet supported by 'nix profile'");
                element.storePaths = {*drv.outPath}; // FIXME
                element.source = ProfileElementSource{
                    installable.flakeRef,
                    resolvedRef,
                    attrPath,
                };

                pathsToBuild.push_back(DerivedPath::Built {
                    staticDrvReq(drv.drvPath),
                    {"out"}
                }); // FIXME
            }
        }

        store->buildPaths(pathsToBuild);

        updateProfile(manifest.build(store));
    }
};

struct CmdProfileList : virtual EvalCommand, virtual StoreCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "list installed packages";
    }

    std::string doc() override
    {
        return
          #include "profile-list.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*getEvalState(), *profile);

        for (size_t i = 0; i < manifest.elements.size(); ++i) {
            auto & element(manifest.elements[i]);
            logger->cout("%d %s %s %s", i,
                element.source ? element.source->originalRef.to_string() + "#" + element.source->attrPath : "-",
                element.source ? element.source->resolvedRef.to_string() + "#" + element.source->attrPath : "-",
                concatStringsSep(" ", store->printStorePathSet(element.storePaths)));
        }
    }
};

struct CmdProfileDiffClosures : virtual StoreCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "show the closure difference between each version of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-diff-closures.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto [gens, curGen] = findGenerations(*profile);

        std::optional<Generation> prevGen;
        bool first = true;

        for (auto & gen : gens) {
            if (prevGen) {
                if (!first) std::cout << "\n";
                first = false;
                std::cout << fmt("Version %d -> %d:\n", prevGen->number, gen.number);
                printClosureDiff(store,
                    store->followLinksToStorePath(prevGen->path),
                    store->followLinksToStorePath(gen.path),
                    "  ");
            }

            prevGen = gen;
        }
    }
};

struct CmdProfileHistory : virtual StoreCommand, EvalCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "show all versions of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-history.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto [gens, curGen] = findGenerations(*profile);

        std::optional<std::pair<Generation, ProfileManifest>> prevGen;
        bool first = true;

        for (auto & gen : gens) {
            ProfileManifest manifest(*getEvalState(), gen.path);

            if (!first) std::cout << "\n";
            first = false;

            if (prevGen)
                std::cout << fmt("Version %d -> %d:\n", prevGen->first.number, gen.number);
            else
                std::cout << fmt("Version %d:\n", gen.number);

            ProfileManifest::printDiff(
                prevGen ? prevGen->second : ProfileManifest(),
                manifest,
                "  ");

            prevGen = {gen, std::move(manifest)};
        }
    }
};

struct CmdProfile : NixMultiCommand
{
    CmdProfile()
        : MultiCommand({
              {"install", []() { return make_ref<CmdProfileInstall>(); }},
              {"remove", []() { return make_ref<CmdProfileRemove>(); }},
              {"upgrade", []() { return make_ref<CmdProfileUpgrade>(); }},
              {"list", []() { return make_ref<CmdProfileList>(); }},
              {"diff-closures", []() { return make_ref<CmdProfileDiffClosures>(); }},
              {"history", []() { return make_ref<CmdProfileHistory>(); }},
          })
    { }

    std::string description() override
    {
        return "manage Nix profiles";
    }

    std::string doc() override
    {
        return
          #include "profile.md"
          ;
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix profile' requires a sub-command.");
        command->second->prepare();
        command->second->run();
    }
};

static auto rCmdProfile = registerCommand<CmdProfile>("profile");
