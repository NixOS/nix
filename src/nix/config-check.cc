#include <sstream>

#include "nix/cmd/command.hh"
#include "nix/util/exit.hh"
#include "nix/util/logging.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/util/executable-path.hh"
#include "nix/store/globals.hh"

namespace nix::fs {
using namespace std::filesystem;
}

using namespace nix;

namespace {

std::string formatProtocol(unsigned int proto)
{
    if (proto) {
        auto major = GET_PROTOCOL_MAJOR(proto) >> 8;
        auto minor = GET_PROTOCOL_MINOR(proto);
        return fmt("%1%.%2%", major, minor);
    }
    return "unknown";
}

bool checkPass(std::string_view msg)
{
    notice(ANSI_GREEN "[PASS] " ANSI_NORMAL + msg);
    return true;
}

bool checkFail(std::string_view msg)
{
    notice(ANSI_RED "[FAIL] " ANSI_NORMAL + msg);
    return false;
}

void checkInfo(std::string_view msg)
{
    notice(ANSI_BLUE "[INFO] " ANSI_NORMAL + msg);
}

} // namespace

struct CmdConfigCheck : StoreCommand
{
    bool success = true;

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "check your system for potential problems and print a PASS or FAIL for each check";
    }

    Category category() override
    {
        return catNixInstallation;
    }

    void run(ref<Store> store) override
    {
        logger->log("Running checks against store uri: " + store->config.getHumanReadableURI());

        if (store.dynamic_pointer_cast<LocalFSStore>()) {
            success &= checkNixInPath();
            success &= checkProfileRoots(store);
        }
        success &= checkStoreProtocol(store->getProtocol());
        checkTrustedUser(store);

        if (!success)
            throw Exit(2);
    }

    bool checkNixInPath()
    {
        std::set<std::filesystem::path> dirs;

        for (auto & dir : ExecutablePath::load().directories) {
            auto candidate = dir / "nix-env";
            if (std::filesystem::exists(candidate))
                dirs.insert(std::filesystem::canonical(candidate).parent_path());
        }

        if (dirs.size() != 1) {
            std::ostringstream ss;
            ss << "Multiple versions of nix found in PATH:\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            return checkFail(ss.view());
        }

        return checkPass("PATH contains only one nix version.");
    }

    bool checkProfileRoots(ref<Store> store)
    {
        std::set<std::filesystem::path> dirs;

        for (auto & dir : ExecutablePath::load().directories) {
            auto profileDir = dir.parent_path();
            try {
                auto userEnv = std::filesystem::weakly_canonical(profileDir);

                auto noContainsProfiles = [&] {
                    for (auto && part : profileDir)
                        if (part == "profiles")
                            return false;
                    return true;
                };

                if (store->isStorePath(userEnv.string()) && hasSuffix(userEnv.string(), "user-environment")) {
                    while (noContainsProfiles() && std::filesystem::is_symlink(profileDir))
                        profileDir = std::filesystem::weakly_canonical(
                            profileDir.parent_path() / std::filesystem::read_symlink(profileDir));

                    if (noContainsProfiles())
                        dirs.insert(dir);
                }
            } catch (SystemError &) {
            } catch (std::filesystem::filesystem_error &) {
            }
        }

        if (!dirs.empty()) {
            std::ostringstream ss;
            ss << "Found profiles outside of " << settings.nixStateDir << "/profiles.\n"
               << "The generation this profile points to might not have a gcroot and could be\n"
               << "garbage collected, resulting in broken symlinks.\n\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            ss << "\n";
            return checkFail(ss.view());
        }

        return checkPass("All profiles are gcroots.");
    }

    bool checkStoreProtocol(unsigned int storeProto)
    {
        unsigned int clientProto = GET_PROTOCOL_MAJOR(SERVE_PROTOCOL_VERSION) == GET_PROTOCOL_MAJOR(storeProto)
                                       ? SERVE_PROTOCOL_VERSION
                                       : PROTOCOL_VERSION;

        if (clientProto != storeProto) {
            std::ostringstream ss;
            ss << "Warning: protocol version of this client does not match the store.\n"
               << "While this is not necessarily a problem it's recommended to keep the client in\n"
               << "sync with the daemon.\n\n"
               << "Client protocol: " << formatProtocol(clientProto) << "\n"
               << "Store protocol: " << formatProtocol(storeProto) << "\n\n";
            return checkFail(ss.view());
        }

        return checkPass("Client protocol matches store protocol.");
    }

    void checkTrustedUser(ref<Store> store)
    {
        if (auto trustedMay = store->isTrustedClient()) {
            std::string_view trusted = trustedMay.value() ? "trusted" : "not trusted";
            checkInfo(fmt("You are %s by store uri: %s", trusted, store->config.getHumanReadableURI()));
        } else {
            checkInfo(fmt("Store uri: %s doesn't have a notion of trusted user", store->config.getHumanReadableURI()));
        }
    }
};

static auto rCmdConfigCheck = registerCommand2<CmdConfigCheck>({"config", "check"});
