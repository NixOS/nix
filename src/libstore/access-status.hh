#pragma once
///@file


#include <set>
#include <tuple>
#include "comparator.hh"
#include "globals.hh"
#include "acl.hh"
#include "util.hh"

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
                    users.insert(getUserName(user.uid));
                },
                [&](ACL::Group group) {
                    groups.insert(getGroupName(group.gid));
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

