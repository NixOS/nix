#include "command.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "store-cast.hh"
#include "granular-access-store.hh"

using namespace nix;

struct CmdStoreAccessRevoke : StorePathsCommand
{
    std::set<std::string> users;
    std::set<std::string> groups;
    bool all = false;
    CmdStoreAccessRevoke()
    {
        addFlag({
            .longName = "user",
            .shortName = 'u',
            .description = "User from whom access should be revoked",
            .labels = {"user"},
            .handler = {[&](std::string _user){
                users.insert(_user);
            }}
        });
        addFlag({
            .longName = "group",
            .shortName = 'g',
            .description = "Group from which access should be revoked",
            .labels = {"group"},
            .handler = {[&](std::string _group){
                groups.insert(_group);
            }}
        });
        addFlag({
            .longName = "all-entities",
            .shortName = 'a',
            .description = "Revoke access from all entities",
            .handler = {&all, true}
        });
    }
    std::string description() override
    {
        return "revoke user's access to store paths";
    }

    std::string doc() override
    {
        return
          #include "store-repair.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (! all && users.empty() && groups.empty()) {
            throw Error("At least one of either --all-entities/-a, --user/-u or --group/-g is required");
        } else if (all && ! (users.empty() && groups.empty())) {
            warn("--all-entities/-a implies removal of all users and groups from the access control list; ignoring --user/-u and --group/-g");
        } else {
            auto & localStore = require<LocalGranularAccessStore>(*store);
            for (auto & path : storePaths) {
                auto status = localStore.getAccessStatus(path);
                if (!status.isProtected) warn("Path '%s' is not protected; all users can access it regardless of permissions", store->printStorePath(path));
                if (!localStore.isValidPath(path)) warn("Path %s does not exist yet; permissions will be applied as soon as it is added to the store", localStore.printStorePath(path));

                if (all) {
                    status.entities = {};
                } else {
                    for (auto user : users) status.entities.erase(nix::ACL::User(user));
                    for (auto group : groups) status.entities.erase(nix::ACL::Group(group));
                }
                localStore.setAccessStatus(path, status, false);
            }
        }
    }
};

static auto rStoreAccessRevoke = registerCommand2<CmdStoreAccessRevoke>({"store", "access", "revoke"});
