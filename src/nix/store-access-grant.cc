#include "command.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "store-cast.hh"
#include "granular-access-store.hh"

using namespace nix;

struct CmdStoreAccessGrant : StorePathsCommand
{
    std::set<std::string> users;
    std::set<std::string> groups;
    CmdStoreAccessGrant()
    {
        addFlag({
            .longName = "user",
            .shortName = 'u',
            .description = "User to whom access should be granted",
            .labels = {"user"},
            .handler = {[&](std::string _user){
                users.insert(_user);
            }}
        });
        addFlag({
            .longName = "group",
            .shortName = 'g',
            .description = "Group to which access should be granted",
            .labels = {"group"},
            .handler = {[&](std::string _group){
                groups.insert(_group);
            }}
        });
    }
    std::string description() override
    {
        return "grant a user access to store paths";
    }

    std::string doc() override
    {
        return
          #include "store-access-grant.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        if (users.empty() && groups.empty()) {
            throw Error("At least one of either --user/-u or --group/-g is required");
        } else {
            auto & localStore = require<LocalGranularAccessStore>(*store);
            for (auto & path : storePaths) {
                auto status = localStore.getAccessStatus(path);
                if (!status.isProtected) warn("Path '%s' is not protected; all users can access it regardless of permissions", store->printStorePath(path));
                if (!localStore.isValidPath(path)) warn("Path %s does not exist yet; permissions will be applied as soon as it is added to the store", localStore.printStorePath(path));

                for (auto user : users) status.entities.insert(nix::ACL::User(user));
                for (auto group : groups) status.entities.insert(nix::ACL::Group(group));
                localStore.setAccessStatus(path, status, false);
            }
        }
    }
};

static auto rStoreAccessGrant = registerCommand2<CmdStoreAccessGrant>({"store", "access", "grant"});
