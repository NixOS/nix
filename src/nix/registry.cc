#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/flake/flake.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/registry.hh"

using namespace nix;
using namespace nix::flake;

class RegistryCommand : virtual Args
{
    std::string registry_path;

    std::shared_ptr<fetchers::Registry> registry;

public:

    RegistryCommand()
    {
        addFlag({
            .longName = "registry",
            .description = "The registry to operate on.",
            .labels = {"registry"},
            .handler = {&registry_path},
        });
    }

    std::shared_ptr<fetchers::Registry> getRegistry()
    {
        if (registry)
            return registry;
        if (registry_path.empty()) {
            registry = fetchers::getUserRegistry(fetchSettings);
        } else {
            registry = fetchers::getCustomRegistry(fetchSettings, registry_path);
        }
        return registry;
    }

    Path getRegistryPath()
    {
        if (registry_path.empty()) {
            return fetchers::getUserRegistryPath().string();
        } else {
            return registry_path;
        }
    }
};

struct CmdRegistryList : StoreCommand
{
    std::string description() override
    {
        return "list available Nix flakes";
    }

    std::string doc() override
    {
        return
#include "registry-list.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        using namespace fetchers;

        auto registries = getRegistries(fetchSettings, *store);

        for (auto & registry : registries) {
            for (auto & entry : registry->entries) {
                // FIXME: format nicely
                logger->cout(
                    "%s %s %s",
                    registry->type == Registry::Flag     ? "flags "
                    : registry->type == Registry::User   ? "user  "
                    : registry->type == Registry::System ? "system"
                                                         : "global",
                    entry.from.toURLString(),
                    entry.to.toURLString(attrsToQuery(entry.extraAttrs)));
            }
        }
    }
};

struct CmdRegistryAdd : MixEvalArgs, Command, RegistryCommand
{
    std::string fromUrl, toUrl;

    std::string description() override
    {
        return "add/replace flake in user flake registry";
    }

    std::string doc() override
    {
        return
#include "registry-add.md"
            ;
    }

    CmdRegistryAdd()
    {
        expectArg("from-url", &fromUrl);
        expectArg("to-url", &toUrl);
    }

    void run() override
    {
        auto fromRef = parseFlakeRef(fetchSettings, fromUrl);
        auto toRef = parseFlakeRef(fetchSettings, toUrl);
        auto registry = getRegistry();
        fetchers::Attrs extraAttrs;
        if (toRef.subdir != "")
            extraAttrs["dir"] = toRef.subdir;
        registry->remove(fromRef.input);
        registry->add(fromRef.input, toRef.input, extraAttrs, false);
        registry->write(getRegistryPath());
    }
};

struct CmdRegistryRemove : RegistryCommand, Command
{
    std::string url;

    std::string description() override
    {
        return "remove flake from user flake registry";
    }

    std::string doc() override
    {
        return
#include "registry-remove.md"
            ;
    }

    CmdRegistryRemove()
    {
        expectArg("url", &url);
    }

    void run() override
    {
        auto registry = getRegistry();
        registry->remove(parseFlakeRef(fetchSettings, url).input);
        registry->write(getRegistryPath());
    }
};

struct CmdRegistryPin : RegistryCommand, EvalCommand
{
    std::string url;

    std::string locked;

    std::string description() override
    {
        return "pin a flake to its current version or to the current version of a flake URL";
    }

    std::string doc() override
    {
        return
#include "registry-pin.md"
            ;
    }

    CmdRegistryPin()
    {
        expectArg("url", &url);

        expectArgs(
            {.label = "locked",
             .optional = true,
             .handler = {&locked},
             .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                 completeFlakeRef(completions, getStore(), prefix);
             }}});
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (locked.empty())
            locked = url;
        auto registry = getRegistry();
        auto ref = parseFlakeRef(fetchSettings, url);
        auto lockedRef = parseFlakeRef(fetchSettings, locked);
        auto resolvedInput = lockedRef.resolve(fetchSettings, *store).input;
        auto resolved = resolvedInput.getAccessor(fetchSettings, *store).second;
        if (!resolved.isLocked(fetchSettings))
            warn("flake '%s' is not locked", resolved.to_string());
        fetchers::Attrs extraAttrs;
        if (ref.subdir != "")
            extraAttrs["dir"] = ref.subdir;
        registry->remove(ref.input);
        registry->add(ref.input, resolved, extraAttrs, true);
        registry->write(getRegistryPath());
    }
};

struct CmdRegistryResolve : StoreCommand
{
    std::vector<std::string> urls;

    std::string description() override
    {
        return "resolve flake references using the registry";
    }

    std::string doc() override
    {
        return
#include "registry-resolve.md"
            ;
    }

    CmdRegistryResolve()
    {
        expectArgs({
            .label = "flake-refs",
            .handler = {&urls},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        for (auto & url : urls) {
            auto ref = parseFlakeRef(fetchSettings, url);
            auto resolved = ref.resolve(fetchSettings, *store);
            logger->cout("%s", resolved.to_string());
        }
    }
};

struct CmdRegistry : NixMultiCommand
{
    CmdRegistry()
        : NixMultiCommand(
              "registry",
              {
                  {"list", []() { return make_ref<CmdRegistryList>(); }},
                  {"add", []() { return make_ref<CmdRegistryAdd>(); }},
                  {"remove", []() { return make_ref<CmdRegistryRemove>(); }},
                  {"pin", []() { return make_ref<CmdRegistryPin>(); }},
                  {"resolve", []() { return make_ref<CmdRegistryResolve>(); }},
              })
    {
    }

    std::string description() override
    {
        return "manage the flake registry";
    }

    std::string doc() override
    {
        return
#include "registry.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }
};

static auto rCmdRegistry = registerCommand<CmdRegistry>("registry");
