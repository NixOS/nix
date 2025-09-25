#include "nix/cmd/command.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/util/archive.hh"
#include "nix/store/builtins/buildenv.hh"
#include "nix/flake/flakeref.hh"
#include "nix-env/user-env.hh"
#include "nix/store/profiles.hh"
#include "nix/store/names.hh"
#include "nix/util/url.hh"
#include "nix/flake/url-name.hh"

#include <nlohmann/json.hpp>
#include <regex>
#include <iomanip>

#include "nix/util/strings.hh"

using namespace nix;

struct ProfileElementSource
{
    FlakeRef originalRef;
    // FIXME: record original attrpath.
    FlakeRef lockedRef;
    std::string attrPath;
    ExtendedOutputsSpec outputs;

    // TODO libc++ 16 (used by darwin) missing `std::set::operator <=>`, can't do yet.
    // auto operator <=> (const ProfileElementSource & other) const
    auto operator<(const ProfileElementSource & other) const
    {
        return std::tuple(originalRef.to_string(), attrPath, outputs)
               < std::tuple(other.originalRef.to_string(), other.attrPath, other.outputs);
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
        return dropEmptyInitThenConcatStringsSep(", ", names);
    }

    /**
     * Return a string representing an installable corresponding to the current
     * element, either a flakeref or a plain store path
     */
    StringSet toInstallables(Store & store)
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

    void updateStorePaths(ref<Store> evalStore, ref<Store> store, const BuiltPaths & builtPaths)
    {
        storePaths.clear();
        for (auto & buildable : builtPaths) {
            std::visit(
                overloaded{
                    [&](const BuiltPath::Opaque & bo) { storePaths.insert(bo.path); },
                    [&](const BuiltPath::Built & bfd) {
                        for (auto & output : bfd.outputs)
                            storePaths.insert(output.second);
                    },
                },
                buildable.raw());
        }
    }
};

std::string getNameFromElement(const ProfileElement & element)
{
    std::optional<std::string> result = std::nullopt;
    if (element.source) {
        // Seems to be for Flake URLs
        result = getNameFromURL(parseURL(element.source->to_string(), /*lenient=*/true));
    }
    return result.value_or(element.identifier());
}

struct ProfileManifest
{
    using ProfileElementName = std::string;

    std::map<ProfileElementName, ProfileElement> elements;

    ProfileManifest() {}

    ProfileManifest(EvalState & state, const std::filesystem::path & profile)
    {
        auto manifestPath = profile / "manifest.json";

        if (std::filesystem::exists(manifestPath)) {
            auto json = nlohmann::json::parse(readFile(manifestPath.string()));

            auto version = json.value("version", 0);
            std::string sUrl;
            std::string sOriginalUrl;
            switch (version) {
            case 1:
                sUrl = "uri";
                sOriginalUrl = "originalUri";
                break;
            case 2:
            case 3:
                sUrl = "url";
                sOriginalUrl = "originalUrl";
                break;
            default:
                throw Error("profile manifest '%s' has unsupported version %d", manifestPath, version);
            }

            auto elems = json["elements"];
            for (auto & elem : elems.items()) {
                auto & e = elem.value();
                ProfileElement element;
                for (auto & p : e["storePaths"])
                    element.storePaths.insert(state.store->parseStorePath((std::string) p));
                element.active = e["active"];
                if (e.contains("priority")) {
                    element.priority = e["priority"];
                }
                if (e.value(sUrl, "") != "") {
                    element.source = ProfileElementSource{
                        parseFlakeRef(fetchSettings, e[sOriginalUrl]),
                        parseFlakeRef(fetchSettings, e[sUrl]),
                        e["attrPath"],
                        e["outputs"].get<ExtendedOutputsSpec>()};
                }

                std::string name = [&] {
                    if (elems.is_object())
                        return elem.key();
                    if (element.source) {
                        if (auto optName = getNameFromURL(parseURL(element.source->to_string(), /*lenient=*/true)))
                            return *optName;
                    }
                    return element.identifier();
                }();

                addElement(name, std::move(element));
            }
        }

        else if (std::filesystem::exists(profile / "manifest.nix")) {
            // FIXME: needed because of pure mode; ugly.
            state.allowPath(state.store->followLinksToStorePath(profile.string()));
            state.allowPath(state.store->followLinksToStorePath((profile / "manifest.nix").string()));

            auto packageInfos = queryInstalled(state, state.store->followLinksToStore(profile.string()));

            for (auto & packageInfo : packageInfos) {
                ProfileElement element;
                element.storePaths = {packageInfo.queryOutPath()};
                addElement(std::move(element));
            }
        }
    }

    void addElement(std::string_view nameCandidate, ProfileElement element)
    {
        std::string finalName(nameCandidate);
        for (int i = 1; elements.contains(finalName); ++i)
            finalName = nameCandidate + "-" + std::to_string(i);

        elements.insert_or_assign(finalName, std::move(element));
    }

    void addElement(ProfileElement element)
    {
        auto name = getNameFromElement(element);
        addElement(name, std::move(element));
    }

    nlohmann::json toJSON(Store & store) const
    {
        auto es = nlohmann::json::object();
        for (auto & [name, element] : elements) {
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
            es[name] = obj;
        }
        nlohmann::json json;
        // Only upgrade with great care as changing it can break fresh installs
        // like in https://github.com/NixOS/nix/issues/10109
        json["version"] = 3;
        json["elements"] = es;
        return json;
    }

    StorePath build(ref<Store> store)
    {
        auto tempDir = createTempDir();

        StorePathSet references;

        Packages pkgs;
        for (auto & [name, element] : elements) {
            for (auto & path : element.storePaths) {
                if (element.active)
                    pkgs.emplace_back(store->printStorePath(path), true, element.priority);
                references.insert(path);
            }
        }

        buildProfile(tempDir, std::move(pkgs));

        writeFile(tempDir + "/manifest.json", toJSON(*store).dump());

        /* Add the symlink tree to the store. */
        StringSink sink;
        dumpPath(tempDir, sink);

        auto narHash = hashString(HashAlgorithm::SHA256, sink.s);

        auto info = ValidPathInfo::makeFromCA(
            *store,
            "profile",
            FixedOutputInfo{
                .method = FileIngestionMethod::NixArchive,
                .hash = narHash,
                .references =
                    {
                        .others = std::move(references),
                        // profiles never refer to themselves
                        .self = false,
                    },
            },
            narHash);
        info.narSize = sink.s.size();

        StringSource source(sink.s);
        store->addToStore(info, source);

        return std::move(info.path);
    }

    static void printDiff(const ProfileManifest & prev, const ProfileManifest & cur, std::string_view indent)
    {
        auto i = prev.elements.begin();
        auto j = cur.elements.begin();

        bool changes = false;

        while (i != prev.elements.end() || j != cur.elements.end()) {
            if (j != cur.elements.end() && (i == prev.elements.end() || i->first > j->first)) {
                logger->cout("%s%s: ∅ -> %s", indent, j->second.identifier(), j->second.versions());
                changes = true;
                ++j;
            } else if (i != prev.elements.end() && (j == cur.elements.end() || i->first < j->first)) {
                logger->cout("%s%s: %s -> ∅", indent, i->second.identifier(), i->second.versions());
                changes = true;
                ++i;
            } else {
                auto v1 = i->second.versions();
                auto v2 = j->second.versions();
                if (v1 != v2) {
                    logger->cout("%s%s: %s -> %s", indent, i->second.identifier(), v1, v2);
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
builtPathsPerInstallable(const std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> & builtPaths)
{
    std::map<Installable *, std::pair<BuiltPaths, ref<ExtraPathInfo>>> res;
    for (auto & [installable, builtPath] : builtPaths) {
        auto & r = res.insert({&*installable,
                               {
                                   {},
                                   make_ref<ExtraPathInfo>(),
                               }})
                       .first->second;
        /* Note that there could be conflicting info
           (e.g. meta.priority fields) if the installable returned
           multiple derivations. So pick one arbitrarily. FIXME:
           print a warning? */
        r.first.push_back(builtPath.path);
        r.second = builtPath.info;
    }
    return res;
}

struct CmdProfileAdd : InstallablesCommand, MixDefaultProfile
{
    std::optional<int64_t> priority;

    CmdProfileAdd()
    {
        addFlag({
            .longName = "priority",
            .description = "The priority of the package to add.",
            .labels = {"priority"},
            .handler = {&priority},
        });
    };

    std::string description() override
    {
        return "add a package to a profile";
    }

    std::string doc() override
    {
        return
#include "profile-add.md"
            ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        ProfileManifest manifest(*getEvalState(), *profile);

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (auto & installable : installables) {
            ProfileElement element;

            auto iter = builtPaths.find(&*installable);
            if (iter == builtPaths.end())
                continue;
            auto & [res, info] = iter->second;

            if (auto * info2 = dynamic_cast<ExtraPathInfoFlake *>(&*info)) {
                element.source = ProfileElementSource{
                    .originalRef = info2->flake.originalRef,
                    .lockedRef = info2->flake.lockedRef,
                    .attrPath = info2->value.attrPath,
                    .outputs = info2->value.extendedOutputsSpec,
                };
            }

            // If --priority was specified we want to override the
            // priority of the installable.
            element.priority = priority ? *priority : ({
                auto * info2 = dynamic_cast<ExtraPathInfoValue *>(&*info);
                info2 ? info2->value.priority.value_or(defaultPriority) : defaultPriority;
            });

            element.updateStorePaths(getEvalStore(), store, res);

            auto elementName = getNameFromElement(element);

            // Check if the element already exists.
            auto existingPair = manifest.elements.find(elementName);
            if (existingPair != manifest.elements.end()) {
                auto existingElement = existingPair->second;
                auto existingSource = existingElement.source;
                auto elementSource = element.source;
                if (existingSource && elementSource && existingElement.priority == element.priority
                    && existingSource->originalRef == elementSource->originalRef
                    && existingSource->attrPath == elementSource->attrPath) {
                    warn("'%s' is already added", elementName);
                    continue;
                }
            }

            manifest.addElement(elementName, std::move(element));
        }

        try {
            updateProfile(manifest.build(store));
        } catch (BuildEnvFileConflictError & conflictError) {
            // FIXME use C++20 std::ranges once macOS has it
            //       See
            //       https://github.com/NixOS/nix/compare/3efa476c5439f8f6c1968a6ba20a31d1239c2f04..1fe5d172ece51a619e879c4b86f603d9495cc102
            auto findRefByFilePath = [&]<typename Iterator>(Iterator begin, Iterator end) {
                for (auto it = begin; it != end; it++) {
                    auto & [name, profileElement] = *it;
                    for (auto & storePath : profileElement.storePaths) {
                        if (conflictError.fileA.starts_with(store->printStorePath(storePath))) {
                            return std::tuple(conflictError.fileA, name, profileElement.toInstallables(*store));
                        }
                        if (conflictError.fileB.starts_with(store->printStorePath(storePath))) {
                            return std::tuple(conflictError.fileB, name, profileElement.toInstallables(*store));
                        }
                    }
                }
                throw conflictError;
            };
            // There are 2 conflicting files. We need to find out which one is from the already installed package and
            // which one is the package that is the new package that is being installed.
            // The first matching package is the one that was already installed (original).
            auto [originalConflictingFilePath, originalEntryName, originalConflictingRefs] =
                findRefByFilePath(manifest.elements.begin(), manifest.elements.end());
            // The last matching package is the one that was going to be installed (new).
            auto [newConflictingFilePath, newEntryName, newConflictingRefs] =
                findRefByFilePath(manifest.elements.rbegin(), manifest.elements.rend());

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
                "The new package can also be added next to the existing one by assigning a different priority.\n"
                "The conflicting packages have a priority of %5%.\n"
                "To prioritise the new package:\n"
                "\n"
                "  nix profile add %4% --priority %6%\n"
                "\n"
                "To prioritise the existing package:\n"
                "\n"
                "  nix profile add %4% --priority %7%\n",
                originalConflictingFilePath,
                newConflictingFilePath,
                originalEntryName,
                concatStringsSep(" ", newConflictingRefs),
                conflictError.priority,
                conflictError.priority - 1,
                conflictError.priority + 1);
        }
    }
};

struct Matcher
{
    virtual ~Matcher() {}

    virtual std::string getTitle() = 0;
    virtual bool matches(const std::string & name, const ProfileElement & element) = 0;
};

struct RegexMatcher final : public Matcher
{
    std::regex regex;
    std::string pattern;

    RegexMatcher(const std::string & pattern)
        : regex(pattern, std::regex::extended | std::regex::icase)
        , pattern(pattern)
    {
    }

    std::string getTitle() override
    {
        return fmt("Regex '%s'", pattern);
    }

    bool matches(const std::string & name, const ProfileElement & element) override
    {
        return std::regex_match(element.identifier(), regex);
    }
};

struct StorePathMatcher final : public Matcher
{
    nix::StorePath storePath;

    StorePathMatcher(const nix::StorePath & storePath)
        : storePath(storePath)
    {
    }

    std::string getTitle() override
    {
        return fmt("Store path '%s'", storePath.to_string());
    }

    bool matches(const std::string & name, const ProfileElement & element) override
    {
        return element.storePaths.count(storePath);
    }
};

struct NameMatcher final : public Matcher
{
    std::string name;

    NameMatcher(const std::string & name)
        : name(name)
    {
    }

    std::string getTitle() override
    {
        return fmt("Package name '%s'", name);
    }

    bool matches(const std::string & name, const ProfileElement & element) override
    {
        return name == this->name;
    }
};

struct AllMatcher final : public Matcher
{
    std::string getTitle() override
    {
        return "--all";
    }

    bool matches(const std::string & name, const ProfileElement & element) override
    {
        return true;
    }
};

AllMatcher all;

class MixProfileElementMatchers : virtual Args, virtual StoreCommand
{
    std::vector<ref<Matcher>> _matchers;

public:

    MixProfileElementMatchers()
    {
        addFlag({
            .longName = "all",
            .description = "Match all packages in the profile.",
            .handler = {[this]() {
                _matchers.push_back(ref<AllMatcher>(std::shared_ptr<AllMatcher>(&all, [](AllMatcher *) {})));
            }},
        });
        addFlag({
            .longName = "regex",
            .description = "A regular expression to match one or more packages in the profile.",
            .labels = {"pattern"},
            .handler = {[this](std::string arg) { _matchers.push_back(make_ref<RegexMatcher>(arg)); }},
        });
        expectArgs(
            {.label = "elements",
             .optional = true,
             .handler = {[this](std::vector<std::string> args) {
                 for (auto & arg : args) {
                     if (auto n = string2Int<size_t>(arg)) {
                         throw Error("'nix profile' no longer supports indices ('%d')", *n);
                     } else if (getStore()->isStorePath(arg)) {
                         _matchers.push_back(make_ref<StorePathMatcher>(getStore()->parseStorePath(arg)));
                     } else {
                         _matchers.push_back(make_ref<NameMatcher>(arg));
                     }
                 }
             }}});
    }

    StringSet getMatchingElementNames(ProfileManifest & manifest)
    {
        if (_matchers.empty()) {
            throw UsageError("No packages specified.");
        }

        if (std::find_if(
                _matchers.begin(),
                _matchers.end(),
                [](const ref<Matcher> & m) { return m.dynamic_pointer_cast<AllMatcher>(); })
                != _matchers.end()
            && _matchers.size() > 1) {
            throw UsageError("--all cannot be used with package names or regular expressions.");
        }

        if (manifest.elements.empty()) {
            warn("There are no packages in the profile.");
            return {};
        }

        StringSet result;
        for (auto & matcher : _matchers) {
            bool foundMatch = false;
            for (auto & [name, element] : manifest.elements) {
                if (matcher->matches(name, element)) {
                    result.insert(name);
                    foundMatch = true;
                }
            }
            if (!foundMatch) {
                warn("%s does not match any packages in the profile.", matcher->getTitle());
            }
        }
        return result;
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

        ProfileManifest newManifest = oldManifest;

        auto matchingElementNames = getMatchingElementNames(oldManifest);

        if (matchingElementNames.empty()) {
            warn("No packages to remove. Use 'nix profile list' to see the current profile.");
            return;
        }

        for (auto & name : matchingElementNames) {
            auto & element = oldManifest.elements[name];
            notice("removing '%s'", element.identifier());
            newManifest.elements.erase(name);
        }

        auto removedCount = oldManifest.elements.size() - newManifest.elements.size();
        printInfo("removed %d packages, kept %d packages", removedCount, newManifest.elements.size());

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

        Installables installables;
        std::vector<ProfileElement *> elems;

        auto upgradedCount = 0;

        auto matchingElementNames = getMatchingElementNames(manifest);

        if (matchingElementNames.empty()) {
            warn("No packages to upgrade. Use 'nix profile list' to see the current profile.");
            return;
        }

        for (auto & name : matchingElementNames) {
            auto & element = manifest.elements[name];

            if (!element.source) {
                warn(
                    "Found package '%s', but it was not added from a flake, so it can't be checked for upgrades!",
                    element.identifier());
                continue;
            }
            if (element.source->originalRef.input.isLocked()) {
                warn(
                    "Found package '%s', but it was added from a locked flake reference so it can't be upgraded!",
                    element.identifier());
                continue;
            }

            upgradedCount++;

            Activity act(*logger, lvlChatty, actUnknown, fmt("checking '%s' for updates", element.source->attrPath));

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
            if (derivedPaths.empty())
                continue;
            auto * infop = dynamic_cast<ExtraPathInfoFlake *>(&*derivedPaths[0].info);
            // `InstallableFlake` should use `ExtraPathInfoFlake`.
            assert(infop);
            auto & info = *infop;

            if (info.flake.lockedRef.input.isLocked() && element.source->lockedRef == info.flake.lockedRef)
                continue;

            printInfo(
                "upgrading '%s' from flake '%s' to '%s'",
                element.source->attrPath,
                element.source->lockedRef,
                info.flake.lockedRef);

            element.source = ProfileElementSource{
                .originalRef = installable->flakeRef,
                .lockedRef = info.flake.lockedRef,
                .attrPath = info.value.attrPath,
                .outputs = installable->extendedOutputsSpec,
            };

            installables.push_back(installable);
            elems.push_back(&element);
        }

        if (upgradedCount == 0) {
            warn("Found some packages but none of them could be upgraded.");
            return;
        }

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (size_t i = 0; i < installables.size(); ++i) {
            auto & installable = installables.at(i);
            auto & element = *elems.at(i);
            element.updateStorePaths(getEvalStore(), store, builtPaths.find(&*installable)->second.first);
        }

        updateProfile(manifest.build(store));
    }
};

struct CmdProfileList : virtual EvalCommand, virtual StoreCommand, MixDefaultProfile, MixJSON
{
    std::string description() override
    {
        return "list packages in the profile";
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

        if (json) {
            printJSON(manifest.toJSON(*store));
        } else {
            for (const auto & [i, e] : enumerate(manifest.elements)) {
                auto & [name, element] = e;
                if (i)
                    logger->cout("");
                logger->cout(
                    "Name:               " ANSI_BOLD "%s" ANSI_NORMAL "%s",
                    name,
                    element.active ? "" : " " ANSI_RED "(inactive)" ANSI_NORMAL);
                if (element.source) {
                    logger->cout(
                        "Flake attribute:    %s%s", element.source->attrPath, element.source->outputs.to_string());
                    logger->cout("Original flake URL: %s", element.source->originalRef.to_string());
                    logger->cout("Locked flake URL:   %s", element.source->lockedRef.to_string());
                }
                logger->cout(
                    "Store paths:        %s", concatStringsSep(" ", store->printStorePathSet(element.storePaths)));
            }
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
                if (!first)
                    logger->cout("");
                first = false;
                logger->cout("Version %d -> %d:", prevGen->number, gen.number);
                printClosureDiff(
                    store, store->followLinksToStorePath(prevGen->path), store->followLinksToStorePath(gen.path), "  ");
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

            if (!first)
                logger->cout("");
            first = false;

            logger->cout(
                "Version %s%d" ANSI_NORMAL " (%s)%s:",
                gen.number == curGen ? ANSI_GREEN : ANSI_BOLD,
                gen.number,
                std::put_time(std::gmtime(&gen.creationTime), "%Y-%m-%d"),
                prevGen ? fmt(" <- %d", prevGen->first.number) : "");

            ProfileManifest::printDiff(prevGen ? prevGen->second : ProfileManifest(), manifest, "  ");

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
            .description = "Delete versions older than the specified age. *age* "
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
        : NixMultiCommand(
              "profile",
              {
                  {"add", []() { return make_ref<CmdProfileAdd>(); }},
                  {"remove", []() { return make_ref<CmdProfileRemove>(); }},
                  {"upgrade", []() { return make_ref<CmdProfileUpgrade>(); }},
                  {"list", []() { return make_ref<CmdProfileList>(); }},
                  {"diff-closures", []() { return make_ref<CmdProfileDiffClosures>(); }},
                  {"history", []() { return make_ref<CmdProfileHistory>(); }},
                  {"rollback", []() { return make_ref<CmdProfileRollback>(); }},
                  {"wipe-history", []() { return make_ref<CmdProfileWipeHistory>(); }},
              })
    {
        aliases = {
            {"install", {AliasStatus::Deprecated, {"add"}}},
        };
    }

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
};

static auto rCmdProfile = registerCommand<CmdProfile>("profile");
