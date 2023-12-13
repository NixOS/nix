#include "ansicolor.hh"
#include "command.hh"
#include "store-api.hh"
#include "local-store.hh"
#include "store-cast.hh"

using namespace nix;

struct CmdStoreAccessInfo : StorePathCommand, MixJSON
{
    std::string description() override
    {
        return "get information about store path access";
    }

    std::string doc() override
    {
        return
          #include "store-access-info.md"
          ;
    }

    void run(ref<Store> store, const StorePath & path) override
    {
        auto & aclStore = require<LocalGranularAccessStore>(*store);
        auto status = aclStore.getCurrentAccessStatus(path);
        bool isValid = aclStore.isValidPath(path);
        std::set<std::string> users;
        std::set<std::string> groups;
        for (auto entity : status.entities) {
            std::visit(overloaded {
                [&](ACL::User user) {
                    struct passwd * pw = getpwuid(user.uid);
                    users.insert(pw->pw_name);
                },
                [&](ACL::Group group) {
                    struct group * gr = getgrgid(group.gid);
                    groups.insert(gr->gr_name);
                }
            }, entity);
        }
        if (json) {
            nlohmann::json j = status.json();
            j["exists"] = isValid;
            logger->cout(j.dump());
        }
        else {
            std::string be, have, has;
            if (isValid) {
                be = "is";
                have = "have";
                has = "has";
            }
            else {
                be = "will be";
                have = "will have";
                has = "will have";

                logger->cout("The path does not exist yet; the permissions will be applied when it is added to the store.\n");
            }

            if (status.isProtected)
                logger->cout("The path " + be + " " ANSI_BOLD ANSI_GREEN "protected" ANSI_NORMAL);
            else
                logger->cout("The path " + be + " " ANSI_BOLD ANSI_RED "not" ANSI_NORMAL " protected");

            if (users.empty() && groups.empty()) {
                if (status.isProtected) { logger->cout(""); logger->cout("Nobody " + has + " access to the path"); };
            } else {
                logger->cout("");
                if (!status.isProtected) {
                    logger->warn("Despite this path not being protected, some users and groups " + have + " additional access to it.");
                    logger->cout("");
                }

                if (!users.empty()) {
                    if (status.isProtected)
                        logger->cout("The following users " + have + " access to the path:");
                    else
                        logger->cout(ANSI_BOLD "If the path was protected" ANSI_NORMAL ", the following users would have access to it:");

                    for (auto user : users)
                        logger->cout(ANSI_MAGENTA "  %s" ANSI_NORMAL, user);
                }
                if (! (users.empty() && groups.empty())) logger->cout("");
                if (!groups.empty()) {
                    if (status.isProtected)
                        logger->cout("Users in the following groups " + have + " access to the path:");
                    else
                        logger->cout(ANSI_BOLD "If the path was protected" ANSI_NORMAL ", users in the following groups would have access to it:");
                    for (auto group : groups)
                        logger->cout(ANSI_CYAN "  %s" ANSI_NORMAL, group);
                }
            }
        }
    }

};

static auto rStoreAccessInfo = registerCommand2<CmdStoreAccessInfo>({"store", "access", "info"});
