#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "archive.hh"
#include "builtins/buildenv.hh"
#include "flake/flakeref.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct ProfileElementSource
{
    FlakeRef originalRef;
    // FIXME: record original attrpath.
    FlakeRef resolvedRef;
    std::string attrPath;
    // FIXME: output names
};

struct ProfileElement
{
    PathSet storePaths;
    std::optional<ProfileElementSource> source;
    bool active = true;
    // FIXME: priority
};

struct ProfileManifest
{
    std::vector<ProfileElement> elements;

    ProfileManifest(const Path & profile)
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
                    element.storePaths.insert((std::string) p);
                element.active = e["active"];
                if (e.value("uri", "") != "") {
                    element.source = ProfileElementSource{
                        FlakeRef(e["originalUri"]),
                        FlakeRef(e["uri"]),
                        e["attrPath"]
                    };
                }
                elements.emplace_back(std::move(element));
            }
        }
    }

    std::string toJSON() const
    {
        auto array = nlohmann::json::array();
        for (auto & element : elements) {
            auto paths = nlohmann::json::array();
            for (auto & path : element.storePaths)
                paths.push_back(path);
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

    Path build(ref<Store> store)
    {
        auto tempDir = createTempDir();

        ValidPathInfo info;

        Packages pkgs;
        for (auto & element : elements) {
            for (auto & path : element.storePaths) {
                if (element.active)
                    pkgs.emplace_back(path, true, 5);
                info.references.insert(path);
            }
        }

        buildProfile(tempDir, std::move(pkgs));

        writeFile(tempDir + "/manifest.json", toJSON());

        /* Add the symlink tree to the store. */
        StringSink sink;
        dumpPath(tempDir, sink);

        info.narHash = hashString(htSHA256, *sink.s);
        info.narSize = sink.s->size();
        info.path = store->makeFixedOutputPath(true, info.narHash, "profile", info.references);
        info.ca = makeFixedOutputCA(true, info.narHash);

        store->addToStore(info, sink.s);

        return info.path;
    }
};

struct CmdProfileInstall : InstallablesCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "install a package into a profile";
    }

    Examples examples() override
    {
        return {
            Example{
                "To install a package from Nixpkgs:",
                "nix profile install nixpkgs#hello"
            },
            Example{
                "To install a package from a specific branch of Nixpkgs:",
                "nix profile install nixpkgs/release-19.09#hello"
            },
            Example{
                "To install a package from a specific revision of Nixpkgs:",
                "nix profile install nixpkgs/1028bb33859f8dfad7f98e1c8d185f3d1aaa7340#hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*profile);

        PathSet pathsToBuild;

        for (auto & installable : installables) {
            if (auto installable2 = std::dynamic_pointer_cast<InstallableFlake>(installable)) {
                auto [attrPath, resolvedRef, drv] = installable2->toDerivation();

                ProfileElement element;
                element.storePaths = {drv.outPath}; // FIXME
                element.source = ProfileElementSource{
                    installable2->flakeRef,
                    resolvedRef,
                    attrPath,
                };

                pathsToBuild.insert(makeDrvPathWithOutputs(drv.drvPath, {"out"})); // FIXME

                manifest.elements.emplace_back(std::move(element));
            } else
                throw Error("'nix profile install' does not support argument '%s'", installable->what());
        }

        store->buildPaths(pathsToBuild);

        updateProfile(manifest.build(store));
    }
};

struct CmdProfileInfo : virtual StoreCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "info";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show what packages are installed in the default profile:",
                "nix profile info"
            },
        };
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*profile);

        for (size_t i = 0; i < manifest.elements.size(); ++i) {
            auto & element(manifest.elements[i]);
            std::cout << fmt("%d %s %s\n", i,
                element.source ? element.source->originalRef.to_string() + "#" + element.source->attrPath : "-",
                element.source ? element.source->resolvedRef.to_string() + "#" + element.source->attrPath : "-",
                concatStringsSep(" ", element.storePaths));
        }
    }
};

struct CmdProfile : virtual MultiCommand, virtual Command
{
    CmdProfile()
        : MultiCommand({
              {"install", []() { return make_ref<CmdProfileInstall>(); }},
              {"info", []() { return make_ref<CmdProfileInfo>(); }},
          })
    { }

    std::string description() override
    {
        return "manage Nix profiles";
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix profile' requires a sub-command.");
        command->prepare();
        command->run();
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        MultiCommand::printHelp(programName, out);
    }
};

static auto r1 = registerCommand<CmdProfile>("profile");

