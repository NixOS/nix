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

    StoreObjectDerivationOutput(DerivedPath::Built p) : drvPath(p.drvPath->getBaseStorePath())
    {
        if (auto names = std::get_if<OutputsSpec::Names>(&p.outputs.raw))
            if (names->size() == 1) {
                output = *names->begin();
                return;
            }
        throw Error("StoreObjectDerivationOutput requires a DerivedPathBuilt with just one named output");
    };
    StoreObjectDerivationOutput(SingleDerivedPathBuilt p) : drvPath(p.drvPath->getBaseStorePath()), output(p.output) { };
    StoreObjectDerivationOutput(StorePath drvPath, std::string output) : drvPath(drvPath), output(output) { };

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

    /** Get an access status of a path */
    virtual AccessStatus getAccessStatus(const StoreObject & storeObject) = 0;

    /** Set an access status on a set of paths, in a single "transaction" that gets rolled back in case of an error, and is self-consistent */
    virtual void setAccessStatus(const std::map<StoreObject, AccessStatus> & pathMap, const bool & ensureAccessCheck = true) = 0;

    virtual void setAccessStatus(StoreObject o, AccessStatus a, const bool & ensureAccessCheck = true)
    {
        setAccessStatus({{o, a}}, ensureAccessCheck);
    }

    virtual std::set<AccessControlGroup> getSubjectGroupsUncached(AccessControlSubject subject) = 0;

    std::set<AccessControlGroup> getSubjectGroups(AccessControlSubject subject)
    {
        if (!settings.cacheUserGroups) return getSubjectGroupsUncached(subject);
        if (subjectGroupCache.contains(subject)) return subjectGroupCache[subject];
        auto groups = getSubjectGroupsUncached(subject);
        subjectGroupCache[subject] = groups;
        return groups;
    }

    /**
     * Whether any of the given @entities@ can access the path
     */
    bool canAccess(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities)
    {
        if (! experimentalFeatureSettings.isEnabled(Xp::ACLs) || trusted) return true;
        AccessStatus status = getAccessStatus(storeObject);
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
    bool canAccess(const StoreObject & storeObject, AccessControlSubject subject)
    {
        std::set<AccessControlEntity> entities;
        auto groups = getSubjectGroups(subject);
        for (auto group : groups) {
            entities.insert(group);
        }
        entities.insert(subject);
        return canAccess(storeObject, entities);
    }

    /**
     * Whether the effective subject can access the store path
     */
    bool canAccess(const StoreObject & storeObject) {
        if (!experimentalFeatureSettings.isEnabled(Xp::ACLs) || trusted) return true;
        if (effectiveUser){
            return canAccess(storeObject, *effectiveUser);
        }
        else {
            return !getAccessStatus(storeObject).isProtected;
        }
    }

    void addAllowedEntities(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getAccessStatus(storeObject);
        for (auto entity : entities) status.entities.insert(entity);
        setAccessStatus(storeObject, status, false);
    }

    void removeAllowedEntities(const StoreObject & storeObject, const std::set<AccessControlEntity> & entities) {
        auto status = getAccessStatus(storeObject);
        for (auto entity : entities) status.entities.erase(entity);
        setAccessStatus(storeObject, status, false);
    }

private:
    std::map<AccessControlSubject, std::set<AccessControlGroup>> subjectGroupCache;
};

using LocalGranularAccessStore = GranularAccessStore<ACL::User, ACL::Group>;


}
