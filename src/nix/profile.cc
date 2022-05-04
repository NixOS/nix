#include "command.hh"
#include "installable-flake.hh"
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
#include <iomanip>

using namespace nix;

struct ProfileElementSource
{
    FlakeRef originalRef;
    // FIXME: record original attrpath.
    FlakeRef lockedRef;
    std::string attrPath;
    ExtendedOutputsSpec outputs;

    bool operator < (const ProfileElementSource & other) const
    {
        return
            std::tuple(originalRef.to_string(), attrPath, outputs) <
            std::tuple(other.originalRef.to_string(), other.attrPath, other.outputs);
    }

    std::string to_string() const
    {
        return fmt("%s#%s%s", originalRef, attrPath, outputs.to_string());
    }
};

const int defaultPriority = 5;

struct ProfileElement
{
    StorePathSet storePaths;
    std::optional<ProfileElementSource> source;
    bool active = true;
    int priority = defaultPriority;

    std::string identifier() const
    {
        if (source)
            return source->to_string();
        StringSet names;
        for (auto & path : storePaths)
            names.insert(DrvName(path.name()).name);
        return concatStringsSep(", ", names);
    }

    /**
     * Return a string representing an installable corresponding to the current
     * element, either a flakeref or a plain store path
     */
    std::set<std::string> toInstallables(Store & store)
    {
        if (source)
            return {source->to_string()};
        StringSet rawPaths;
        for (auto & path : storePaths)
            rawPaths.insert(store.printStorePath(path));
        return rawPaths;
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
        return std::tuple(identifier(), storePaths) < std::tuple(other.identifier(), other.storePaths);
    }

    void updateStorePaths(
        ref<Store> evalStore,
        ref<Store> store,
        const BuiltPaths & builtPaths)
    {
        storePaths.clear();
        for (auto & buildable : builtPaths) {
            std::visit(overloaded {
                [&](const BuiltPath::Opaque & bo) {
                    storePaths.insert(bo.path);
                },
                [&](const BuiltPath::Built & bfd) {
                    for (auto & output : bfd.outputs)
                        storePaths.insert(output.second);
                },
            }, buildable.raw());
        }
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
            std::string sUrl;
            std::string sOriginalUrl;
            switch (version) {
                case 1:
                    sUrl = "uri";
                    sOriginalUrl = "originalUri";
                    break;
                case 2:
                    sUrl = "url";
                    sOriginalUrl = "originalUrl";
                    break;
                default:
                    throw Error("profile manifest '%s' has unsupported version %d", manifestPath, version);
            }

            for (auto & e : json["elements"]) {
                ProfileElement element;
                for (auto & p : e["storePaths"])
                    element.storePaths.insert(state.store->parseStorePath((std::string) p));
                element.active = e["active"];
                if(e.contains("priority")) {
                    element.priority = e["priority"];
                }
                if (e.value(sUrl, "") != "") {
                    element.source = ProfileElementSource {
                        parseFlakeRef(e[sOriginalUrl]),
                        parseFlakeRef(e[sUrl]),
                        e["attrPath"],
                        e["outputs"].get<ExtendedOutputsSpec>()
                    };
                }
                elements.emplace_back(std::move(element));
            }
        }

        else if (pathExists(profile + "/manifest.nix")) {
            // FIXME: needed because of pure mode; ugly.
            state.allowPath(state.store->followLinksToStore(profile));
            state.allowPath(state.store->followLinksToStore(profile + "/manifest.nix"));

            auto drvInfos = queryInstalled(state, state.store->followLinksToStore(profile));

            for (auto & drvInfo : drvInfos) {
                ProfileElement element;
                element.storePaths = {drvInfo.queryOutPath()};
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
            obj["priority"] = element.priority;
            if (element.source) {
                obj["originalUrl"] = element.source->originalRef.to_string();
                obj["url"] = element.source->lockedRef.to_string();
                obj["attrPath"] = element.source->attrPath;
                obj["outputs"] = element.source->outputs;
            }
            array.push_back(obj);
        }
        nlohmann::json json;
        json["version"] = 2;
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
                    pkgs.emplace_back(store->printStorePath(path), true, element.priority);
                references.insert(path);
            }
        }

        buildProfile(tempDir, std::move(pkgs));

        writeFile(tempDir + "/manifest.json", toJSON(*store));

        /* Add the symlink tree to the store. */
        StringSink sink;
        dumpPath(tempDir, sink);

        auto narHash = hashString(htSHA256, sink.s);

        ValidPathInfo info {
            *store,
            "profile",
            FixedOutputInfo {
                .hash = {
                    .method = FileIngestionMethod::Recursive,
                    .hash = narHash,
                },
                .references = {
                    .others = std::move(references),
                    // profiles never refer to themselves
                    .self = false,
                },
            },
            narHash,
        };
        info.narSize = sink.s.size();

        StringSource source(sink.s);
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
            if (j != curElems.end() && (i == prevElems.end() || i->identifier() > j->identifier())) {
                logger->cout("%s%s: ∅ -> %s", indent, j->identifier(), j->versions());
                changes = true;
                ++j;
            }
            else if (i != prevElems.end() && (j == curElems.end() || i->identifier() < j->identifier())) {
                logger->cout("%s%s: %s -> ∅", indent, i->identifier(), i->versions());
                changes = true;
                ++i;
            }
            else {
                auto v1 = i->versions();
                auto v2 = j->versions();
                if (v1 != v2) {
                    logger->cout("%s%s: %s -> %s", indent, i->identifier(), v1, v2);
                    changes = true;
                }
                ++i;
                ++j;
            }
        }

        if (!changes)
            logger->cout("%sNo changes.", indent);
    }
};

static std::map<Installable *, std::pair<BuiltPaths, ref<ExtraPathInfo>>>
builtPathsPerInstallable(
    const std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> & builtPaths)
{
    std::map<Installable *, std::pair<BuiltPaths, ref<ExtraPathInfo>>> res;
    for (auto & [installable, builtPath] : builtPaths) {
        auto & r = res.insert({
            &*installable,
            {
                {},
                make_ref<ExtraPathInfo>(),
            }
        }).first->second;
        /* Note that there could be conflicting info
           (e.g. meta.priority fields) if the installable returned
           multiple derivations. So pick one arbitrarily. FIXME:
           print a warning? */
        r.first.push_back(builtPath.path);
        r.second = builtPath.info;
    }
    return res;
}

struct CmdProfileInstall : InstallablesCommand, MixDefaultProfile
{
    std::optional<int64_t> priority;

    CmdProfileInstall() {
        addFlag({
            .longName = "priority",
            .description = "The priority of the package to install.",
            .labels = {"priority"},
            .handler = {&priority},
        });
    };

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

    void run(ref<Store> store, Installables && installables) override
    {
        ProfileManifest manifest(*getEvalState(), *profile);

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(
                getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (auto & installable : installables) {
            ProfileElement element;

            auto iter = builtPaths.find(&*installable);
            if (iter == builtPaths.end()) continue;
            auto & [res, info] = iter->second;

            if (auto * info2 = dynamic_cast<ExtraPathInfoFlake *>(&*info)) {
                element.source = ProfileElementSource {
                    .originalRef = info2->flake.originalRef,
                    .lockedRef = info2->flake.lockedRef,
                    .attrPath = info2->value.attrPath,
                    .outputs = info2->value.extendedOutputsSpec,
                };
            }

            // If --priority was specified we want to override the
            // priority of the installable.
            element.priority =
                priority
                ? *priority
                : ({
                    auto * info2 = dynamic_cast<ExtraPathInfoValue *>(&*info);
                    info2
                        ? info2->value.priority.value_or(defaultPriority)
                        : defaultPriority;
                });

            element.updateStorePaths(getEvalStore(), store, res);

            manifest.elements.push_back(std::move(element));
        }

        try {
            updateProfile(manifest.build(store));
        } catch (BuildEnvFileConflictError & conflictError) {
            // FIXME use C++20 std::ranges once macOS has it
            //       See https://github.com/NixOS/nix/compare/3efa476c5439f8f6c1968a6ba20a31d1239c2f04..1fe5d172ece51a619e879c4b86f603d9495cc102
            auto findRefByFilePath = [&]<typename Iterator>(Iterator begin, Iterator end) {
                for (auto it = begin; it != end; it++) {
                    auto profileElement = *it;
                    for (auto & storePath : profileElement.storePaths) {
                        if (conflictError.fileA.starts_with(store->printStorePath(storePath))) {
                            return std::pair(conflictError.fileA, profileElement.toInstallables(*store));
                        }
                        if (conflictError.fileB.starts_with(store->printStorePath(storePath))) {
                            return std::pair(conflictError.fileB, profileElement.toInstallables(*store));
                        }
                    }
                }
                throw conflictError;
            };
            // There are 2 conflicting files. We need to find out which one is from the already installed package and
            // which one is the package that is the new package that is being installed.
            // The first matching package is the one that was already installed (original).
            auto [originalConflictingFilePath, originalConflictingRefs] = findRefByFilePath(manifest.elements.begin(), manifest.elements.end());
            // The last matching package is the one that was going to be installed (new).
            auto [newConflictingFilePath, newConflictingRefs] = findRefByFilePath(manifest.elements.rbegin(), manifest.elements.rend());

            throw Error(
                "An existing package already provides the following file:\n"
                "\n"
                "  %1%\n"
                "\n"
                "This is the conflicting file from the new package:\n"
                "\n"
                "  %2%\n"
                "\n"
                "To remove the existing package:\n"
                "\n"
                "  nix profile remove %3%\n"
                "\n"
                "The new package can also be installed next to the existing one by assigning a different priority.\n"
                "The conflicting packages have a priority of %5%.\n"
                "To prioritise the new package:\n"
                "\n"
                "  nix profile install %4% --priority %6%\n"
                "\n"
                "To prioritise the existing package:\n"
                "\n"
                "  nix profile install %4% --priority %7%\n",
                originalConflictingFilePath,
                newConflictingFilePath,
                concatStringsSep(" ", originalConflictingRefs),
                concatStringsSep(" ", newConflictingRefs),
                conflictError.priority,
                conflictError.priority - 1,
                conflictError.priority + 1
            );
        }
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

    struct RegexPattern {
        std::string pattern;
        std::regex  reg;
    };
    typedef std::variant<size_t, Path, RegexPattern> Matcher;

    std::vector<Matcher> getMatchers(ref<Store> store)
    {
        std::vector<Matcher> res;

        for (auto & s : _matchers) {
            if (auto n = string2Int<size_t>(s))
                res.push_back(*n);
            else if (store->isStorePath(s))
                res.push_back(s);
            else
                res.push_back(RegexPattern{s,std::regex(s, std::regex::extended | std::regex::icase)});
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
            } else if (auto regex = std::get_if<RegexPattern>(&matcher)) {
                if (element.source
                    && std::regex_match(element.source->attrPath, regex->reg))
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
            if (!matches(*store, element, i, matchers)) {
                newManifest.elements.push_back(std::move(element));
            } else {
                notice("removing '%s'", element.identifier());
            }
        }

        auto removedCount = oldManifest.elements.size() - newManifest.elements.size();
        printInfo("removed %d packages, kept %d packages",
            removedCount,
            newManifest.elements.size());

        if (removedCount == 0) {
            for (auto matcher: matchers) {
                if (const size_t * index = std::get_if<size_t>(&matcher)){
                    warn("'%d' is not a valid index", *index);
                } else if (const Path * path = std::get_if<Path>(&matcher)){
                    warn("'%s' does not match any paths", *path);
                } else if (const RegexPattern * regex = std::get_if<RegexPattern>(&matcher)){
                    warn("'%s' does not match any packages", regex->pattern);
                }
            }
            warn ("Use 'nix profile list' to see the current profile.");
        }
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

        Installables installables;
        std::vector<size_t> indices;

        auto upgradedCount = 0;

        for (size_t i = 0; i < manifest.elements.size(); ++i) {
            auto & element(manifest.elements[i]);
            if (element.source
                && !element.source->originalRef.input.isLocked()
                && matches(*store, element, i, matchers))
            {
                upgradedCount++;

                Activity act(*logger, lvlChatty, actUnknown,
                    fmt("checking '%s' for updates", element.source->attrPath));

                auto installable = make_ref<InstallableFlake>(
                    this,
                    getEvalState(),
                    FlakeRef(element.source->originalRef),
                    "",
                    element.source->outputs,
                    Strings{element.source->attrPath},
                    Strings{},
                    lockFlags);

                auto derivedPaths = installable->toDerivedPaths();
                if (derivedPaths.empty()) continue;
                auto * infop = dynamic_cast<ExtraPathInfoFlake *>(&*derivedPaths[0].info);
                // `InstallableFlake` should use `ExtraPathInfoFlake`.
                assert(infop);
                auto & info = *infop;

                if (element.source->lockedRef == info.flake.lockedRef) continue;

                printInfo("upgrading '%s' from flake '%s' to '%s'",
                    element.source->attrPath, element.source->lockedRef, info.flake.lockedRef);

                element.source = ProfileElementSource {
                    .originalRef = installable->flakeRef,
                    .lockedRef = info.flake.lockedRef,
                    .attrPath = info.value.attrPath,
                    .outputs = installable->extendedOutputsSpec,
                };

                installables.push_back(installable);
                indices.push_back(i);
            }
        }

        if (upgradedCount == 0) {
            for (auto & matcher : matchers) {
                if (const size_t * index = std::get_if<size_t>(&matcher)){
                    warn("'%d' is not a valid index", *index);
                } else if (const Path * path = std::get_if<Path>(&matcher)){
                    warn("'%s' does not match any paths", *path);
                } else if (const RegexPattern * regex = std::get_if<RegexPattern>(&matcher)){
                    warn("'%s' does not match any packages", regex->pattern);
                }
            }
            warn ("Use 'nix profile list' to see the current profile.");
        }

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(
                getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (size_t i = 0; i < installables.size(); ++i) {
            auto & installable = installables.at(i);
            auto & element = manifest.elements[indices.at(i)];
            element.updateStorePaths(
                getEvalStore(),
                store,
                builtPaths.find(&*installable)->second.first);
        }

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
            if (i) logger->cout("");
            logger->cout("Index:              " ANSI_BOLD "%s" ANSI_NORMAL "%s",
                i,
                element.active ? "" : " " ANSI_RED "(inactive)" ANSI_NORMAL);
            if (element.source) {
                logger->cout("Flake attribute:    %s%s", element.source->attrPath, element.source->outputs.to_string());
                logger->cout("Original flake URL: %s", element.source->originalRef.to_string());
                logger->cout("Locked flake URL:   %s", element.source->lockedRef.to_string());
            }
            logger->cout("Store paths:        %s", concatStringsSep(" ", store->printStorePathSet(element.storePaths)));
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
                if (!first) logger->cout("");
                first = false;
                logger->cout("Version %d -> %d:", prevGen->number, gen.number);
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

            if (!first) logger->cout("");
            first = false;

            logger->cout("Version %s%d" ANSI_NORMAL " (%s)%s:",
                gen.number == curGen ? ANSI_GREEN : ANSI_BOLD,
                gen.number,
                std::put_time(std::gmtime(&gen.creationTime), "%Y-%m-%d"),
                prevGen ? fmt(" <- %d", prevGen->first.number) : "");

            ProfileManifest::printDiff(
                prevGen ? prevGen->second : ProfileManifest(),
                manifest,
                "  ");

            prevGen = {gen, std::move(manifest)};
        }
    }
};

struct CmdProfileRollback : virtual StoreCommand, MixDefaultProfile, MixDryRun
{
    std::optional<GenerationNumber> version;

    CmdProfileRollback()
    {
        addFlag({
            .longName = "to",
            .description = "The profile version to roll back to.",
            .labels = {"version"},
            .handler = {&version},
        });
    }

    std::string description() override
    {
        return "roll back to the previous version or a specified version of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-rollback.md"
          ;
    }

    void run(ref<Store> store) override
    {
        switchGeneration(*profile, version, dryRun);
    }
};

struct CmdProfileWipeHistory : virtual StoreCommand, MixDefaultProfile, MixDryRun
{
    std::optional<std::string> minAge;

    CmdProfileWipeHistory()
    {
        addFlag({
            .longName = "older-than",
            .description =
                "Delete versions older than the specified age. *age* "
                "must be in the format *N*`d`, where *N* denotes a number "
                "of days.",
            .labels = {"age"},
            .handler = {&minAge},
        });
    }

    std::string description() override
    {
        return "delete non-current versions of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-wipe-history.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (minAge) {
            auto t = parseOlderThanTimeSpec(*minAge);
            deleteGenerationsOlderThan(*profile, t, dryRun);
        } else
            deleteOldGenerations(*profile, dryRun);
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
              {"rollback", []() { return make_ref<CmdProfileRollback>(); }},
              {"wipe-history", []() { return make_ref<CmdProfileWipeHistory>(); }},
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
        command->second->run();
    }
};

static auto rCmdProfile = registerCommand<CmdProfile>("profile");
