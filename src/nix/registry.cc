#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/registry.hh"

namespace nix {

class RegistryCommand : virtual Args
{
    std::string registry_path;

    std::shared_ptr<fetchers::Registry> registry;

public:

    RegistryCommand()
    {
        auto category = "Registry options";

        addFlag({
            .longName = "file",
            .description =
                "The registry file to operate on. This file will be composed with the default registries (global, system, user) in precedence order.",
            .category = category,
            .labels = {"file"},
            .handler = {[this](std::string s) {
                if (!registry_path.empty())
                    throw UsageError("'--file' can only be specified once");
                registry_path = std::move(s);
            }},
            .completer = completePath,
        });

        addFlag({
            .longName = "registry",
            .description = R"(
    Deprecated alias for `--file`.

    > **DEPRECATED**
    >
    > Use `--file` instead.
            )",
            .category = category,
            .labels = {"file"},
            .handler = {[this](std::string s) {
                if (!registry_path.empty())
                    throw UsageError("'--registry' (or '--file') can only be specified once");
                warn("'--registry' is deprecated; use '--file' instead");
                registry_path = std::move(s);
            }},
            .completer = completePath,
        });
    }

    bool hasCustomRegistry() const
    {
        return !registry_path.empty();
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

    std::filesystem::path getRegistryPath()
    {
        if (registry_path.empty()) {
            return fetchers::getUserRegistryPath().string();
        } else {
            return registry_path;
        }
    }
};

struct CmdRegistryList : RegistryCommand, StoreCommand
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

        Registries registries;
        if (hasCustomRegistry())
            registries.push_back(getRegistry());
        else
            registries = getRegistries(fetchSettings, *store);

        for (auto & registry : registries) {
            for (auto & entry : registry->entries) {
                // FIXME: format nicely
                logger->cout(
                    "%s %s %s",
                    registry->type == Registry::Flag     ? "flags "
                    : registry->type == Registry::User   ? "user  "
                    : registry->type == Registry::System ? "system"
                    : registry->type == Registry::Custom ? "custom"
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
        registry->add(fromRef.input, toRef.input, extraAttrs);
        registry->write(getRegistryPath().string());
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
        registry->write(getRegistryPath().string());
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
        registry->add(ref.input, resolved, extraAttrs);
        registry->write(getRegistryPath().string());
    }
};

struct CmdRegistryResolve : RegistryCommand, StoreCommand
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
        auto customRegistry = hasCustomRegistry() ? getRegistry() : std::shared_ptr<fetchers::Registry>{};

        for (auto & url : urls) {
            auto ref = parseFlakeRef(fetchSettings, url);
            auto [input, extraAttrs] = fetchers::lookupInRegistries(
                fetchSettings, *store, ref.input, fetchers::UseRegistries::All, customRegistry);
            auto subdir = fetchers::maybeGetStrAttr(extraAttrs, "dir").value_or(ref.subdir);
            logger->cout("%s", FlakeRef(std::move(input), subdir).to_string());
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

} // namespace nix
