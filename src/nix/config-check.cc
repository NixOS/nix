#include <sstream>

#include "command.hh"
#include "exit.hh"
#include "logging.hh"
#include "serve-protocol.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "worker-protocol.hh"
#include "executable-path.hh"

namespace nix::fs { using namespace std::filesystem; }

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

bool checkPass(const std::string & msg) {
    notice(ANSI_GREEN "[PASS] " ANSI_NORMAL + msg);
    return true;
}

bool checkFail(const std::string & msg) {
    notice(ANSI_RED "[FAIL] " ANSI_NORMAL + msg);
    return false;
}

void checkInfo(const std::string & msg) {
    notice(ANSI_BLUE "[INFO] " ANSI_NORMAL + msg);
}

}

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

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        logger->log("Running checks against store uri: " + store->getUri());

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
        std::set<fs::path> dirs;

        for (auto & dir : ExecutablePath::load().directories) {
            auto candidate = dir / "nix-env";
            if (fs::exists(candidate))
                dirs.insert(fs::canonical(candidate).parent_path() );
        }

        if (dirs.size() != 1) {
            std::stringstream ss;
            ss << "Multiple versions of nix found in PATH:\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            return checkFail(ss.str());
        }

        return checkPass("PATH contains only one nix version.");
    }

    bool checkProfileRoots(ref<Store> store)
    {
        std::set<fs::path> dirs;

        for (auto & dir : ExecutablePath::load().directories) {
            auto profileDir = dir.parent_path();
            try {
                auto userEnv = fs::weakly_canonical(profileDir);

                auto noContainsProfiles = [&]{
                    for (auto && part : profileDir)
                        if (part == "profiles") return false;
                    return true;
                };

                if (store->isStorePath(userEnv.string()) && hasSuffix(userEnv.string(), "user-environment")) {
                    while (noContainsProfiles() && std::filesystem::is_symlink(profileDir))
                        profileDir = fs::weakly_canonical(
                            profileDir.parent_path() / fs::read_symlink(profileDir));

                    if (noContainsProfiles())
                        dirs.insert(dir);
                }
            } catch (SystemError &) {
            } catch (std::filesystem::filesystem_error &) {}
        }

        if (!dirs.empty()) {
            std::stringstream ss;
            ss << "Found profiles outside of " << settings.nixStateDir << "/profiles.\n"
               << "The generation this profile points to might not have a gcroot and could be\n"
               << "garbage collected, resulting in broken symlinks.\n\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            ss << "\n";
            return checkFail(ss.str());
        }

        return checkPass("All profiles are gcroots.");
    }

    bool checkStoreProtocol(unsigned int storeProto)
    {
        unsigned int clientProto = GET_PROTOCOL_MAJOR(SERVE_PROTOCOL_VERSION) == GET_PROTOCOL_MAJOR(storeProto)
            ? SERVE_PROTOCOL_VERSION
            : PROTOCOL_VERSION;

        if (clientProto != storeProto) {
            std::stringstream ss;
            ss << "Warning: protocol version of this client does not match the store.\n"
               << "While this is not necessarily a problem it's recommended to keep the client in\n"
               << "sync with the daemon.\n\n"
               << "Client protocol: " << formatProtocol(clientProto) << "\n"
               << "Store protocol: " << formatProtocol(storeProto) << "\n\n";
            return checkFail(ss.str());
        }

        return checkPass("Client protocol matches store protocol.");
    }

    void checkTrustedUser(ref<Store> store)
    {
        if (auto trustedMay = store->isTrustedClient()) {
            std::string_view trusted = trustedMay.value()
                ? "trusted"
                : "not trusted";
            checkInfo(fmt("You are %s by store uri: %s", trusted, store->getUri()));
        } else {
            checkInfo(fmt("Store uri: %s doesn't have a notion of trusted user", store->getUri()));
        }
    }
};

static auto rCmdConfigCheck = registerCommand2<CmdConfigCheck>({ "config", "check" });
