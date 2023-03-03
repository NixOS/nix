#include "sandbox.hh"
#include "cgroup.hh"

namespace nix {

void Sandbox::createCGroups(const UserLock*) {
    throw Error("cgroups are not supported on this platform");
};

void Sandbox::prepareChroot(const Store&, LocalDerivationGoal& goal) {
    throw Error("sandboxing builds is not supported on this platform");
};

Pid Sandbox::runInNamespaces(DerivationType& derivationType, LocalDerivationGoal& goal) {
    return startProcess([&]() {
        goal.runChild();
    });
};

std::pair<std::string, Strings> Sandbox::getSandboxArgs(const Derivation& drv, bool, LocalDerivationGoal::DirsInChroot&, const nix::Store&, const LocalDerivationGoal&) {
    Strings args;
    auto builder = drv.builder;
    args.push_back(std::string(baseNameOf(drv.builder)));
    return {builder, args};
};

void Sandbox::spawn(const std::string& builder, const nix::Strings& args, const nix::Strings& envStrs, std::string_view platform) {
    execve(builder.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
};

void Sandbox::addToSandbox(const StorePath& path, const Store& store) {
    throw Error("don't know how to make path '%s' (produced by a recursive Nix call) appear in the sandbox",
        store.printStorePath(path));
};

Strings Sandbox::getPrebuildHookArgs(const Store& store, const StorePath& drvPath) {
    return { store.printStorePath(drvPath) };
};

std::optional<CgroupStats> Sandbox::killSandbox() {
    if (buildUser) {
        auto uid = buildUser->getUID();
        assert(uid != 0);
        killUser(uid);
    }
    return {};
};

} // namespace nix

