#pragma once
///@file


#include <set>
#include <tuple>
#include "comparator.hh"
#include "globals.hh"
#include "acl.hh"

namespace nix {
template<typename AccessControlEntity>
struct AccessStatusFor {
    bool isProtected = false;
    std::set<AccessControlEntity> entities;

    GENERATE_CMP(AccessStatusFor<AccessControlEntity>, me->isProtected, me->entities);

    AccessStatusFor() {
        isProtected = settings.protectByDefault.get();
        entities = {};
    };
    AccessStatusFor(bool isProtected, std::set<AccessControlEntity> entities = {}) : isProtected(isProtected), entities(entities) {};

    nlohmann::json json() const {
        std::set<std::string> users, groups;
        for (auto entity : entities) {
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
        nlohmann::json j;
        j["protected"] = isProtected;
        j["users"] = users;
        j["groups"] = groups;
        return j;
    }
};
}

