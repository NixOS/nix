#pragma once

#include "types.hh"
#include "util.hh"
#include "local-derivation-goal.hh" // LocalDerivationGoal::DirsInChroot

namespace nix {

struct UserLock;
class Store;
struct DerivationType;
struct Derivation;
class StorePath;
struct CgroupStats;

class Sandbox {
public:
    virtual void createCGroups(const UserLock*);
    virtual void prepareChroot(const Store&, LocalDerivationGoal& goal);
    virtual Pid runInNamespaces(DerivationType& derivationType, LocalDerivationGoal& goal);
    virtual void moveOutOfChroot(Path& p) {};
    virtual void deleteChroot() {};
    virtual void enterChroot(const Store&, LocalDerivationGoal& goal) = 0;
    virtual std::pair<std::string, Strings> getSandboxArgs(const Derivation& drv, bool, LocalDerivationGoal::DirsInChroot&, const Store&, const LocalDerivationGoal&);
    virtual void spawn(const std::string& builder, const Strings& args, const Strings& envStrs, std::string_view platform);
    virtual void addToSandbox(const StorePath& path, const Store& store);
    virtual void cleanupPreChildKill() {};
    virtual ~Sandbox() {};
    virtual Strings getPrebuildHookArgs(const Store& store, const StorePath& drvPath);
    virtual Path toRealPath(const Path& p) const { return p; };

    UserLock* buildUser = nullptr;
    virtual std::optional<CgroupStats> killSandbox();
};

std::unique_ptr<Sandbox> createSandboxLinux();
std::unique_ptr<Sandbox> createSandboxDarwin();

} // namespace nix
