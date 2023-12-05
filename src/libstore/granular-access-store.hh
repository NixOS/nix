#pragma once
///@file

#include "config.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "acl.hh"
#include "access-status.hh"

namespace nix {

struct StoreObjectDerivationOutput
{
    StorePath drvPath;
    std::string output;

    StoreObjectDerivationOutput(DerivedPath::Built p) : drvPath(p.drvPath)
    {
        if (auto names = std::get_if<OutputsSpec::Names>(&p.outputs))
            if (names->size() == 1) {
                output = *names->begin();
                return;
            }
        throw Error("StoreObjectDerivationOutput requires a DerivedPathBuilt with just one named output");
    }
    StoreObjectDerivationOutput(StorePath drvPath, std::string output = "out") : drvPath(drvPath), output(output) { };

    GENERATE_CMP(StoreObjectDerivationOutput, me->drvPath, me->output);
};

struct StoreObjectDerivationLog
{
    StorePath drvPath;

    GENERATE_CMP(StoreObjectDerivationLog, me->drvPath);
};

typedef std::variant<StorePath, StoreObjectDerivationOutput, StoreObjectDerivationLog> StoreObject;


template<typename AccessControlSubject, typename AccessControlGroup>
struct GranularAccessStore : public virtual Store
{
    inline static std::string operationName = "Granular access";

    /**
     * Subject against which the access should be checked
     */
    std::optional<AccessControlSubject> effectiveUser;
    bool trusted = false;

    typedef std::variant<AccessControlSubject, AccessControlGroup> AccessControlEntity;
    typedef AccessStatusFor<AccessControlEntity> AccessStatus;


    virtual void setFutureAccessStatus(const StoreObject & storeObject, const AccessStatus & status) = 0;
    virtual void setCurrentAccessStatus(const StoreObject & path, const AccessStatus & status) = 0;
    virtual AccessStatus getFutureAccessStatus(const StoreObject & storeObject) = 0;
    virtual AccessStatus getCurrentAccessStatus(const StoreObject & storeObject) = 0;

    virtual std::set<AccessControlGroup> getSubjectGroups(AccessControlSubject subject) = 0;

    /**
     * Whether any of the given @entities@ can access the path
     */
    bool canAccess(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities, bool use_future = false)
    {
        if (! experimentalFeatureSettings.isEnabled(Xp::ACLs) || trusted) return true;
        AccessStatus status;
        if (use_future) {
            status = getFutureAccessStatus(storeObject);
        }
        else {
            status =  getCurrentAccessStatus(storeObject);
        }
        if (! status.isProtected) return true;
        for (auto ent : status.entities) {
          if (entities.contains(ent)) {
            return true;
          }
        };
        return false;
    }
    /**
     * Whether a subject can access the store path
     */
    bool canAccess(const StoreObject & storeObject, AccessControlSubject subject, bool use_future = false)
    {
        std::set<AccessControlEntity> entities;
        auto groups = getSubjectGroups(subject);
        for (auto group : groups) {
            entities.insert(group);
        }
        entities.insert(subject);
        return canAccess(storeObject, entities, use_future);
    }

    /**
     * Whether the effective subject can access the store path
     */
    bool canAccess(const StoreObject & storeObject, bool use_future = false) {
        if (!experimentalFeatureSettings.isEnabled(Xp::ACLs) || trusted) return true;
        if (effectiveUser){
            return canAccess(storeObject, *effectiveUser, use_future);
        }
        else {
            if (use_future) {
              return !getFutureAccessStatus(storeObject).isProtected;
            } else {
              return !getCurrentAccessStatus(storeObject).isProtected;
            }
        }
    }

    void addAllowedEntitiesFuture(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getFutureAccessStatus(storeObject);
        for (auto entity : entities) status.entities.insert(entity);
        setFutureAccessStatus(storeObject, status);
    }

    void addAllowedEntitiesCurrent(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getCurrentAccessStatus(storeObject);
        for (auto entity : entities) status.entities.insert(entity);
        setCurrentAccessStatus(storeObject, status);
    }

    void removeAllowedEntitiesFuture(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getFutureAccessStatus(storeObject);
        for (auto entity : entities) status.entities.erase(entity);
        setFutureAccessStatus(storeObject, status);
    }
    void removeAllowedEntitiesCurrent(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getCurrentAccessStatus(storeObject);
        for (auto entity : entities) status.entities.erase(entity);
        setCurrentAccessStatus(storeObject, status);
    }
};

using LocalGranularAccessStore = GranularAccessStore<ACL::User, ACL::Group>;


}
