#pragma once

#include "nix/util/util.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>
#include <mutex>
#include <sys/types.h>
#include <sys/mount.h>

namespace nix {

struct SupplementaryGroup
{
    std::string group;        // host/outer group name or ID
    std::string name;         // mapped/inner group name
    std::optional<gid_t> gid; // mapped/inner group ID
    bool allowOnly;

    SupplementaryGroup(
        std::string && group = "",
        std::optional<gid_t> gid = std::nullopt,
        std::string && name = "",
        bool allowOnly = false)
        : group(std::move(group))
        , name(std::move(name))
        , gid(std::move(gid))
        , allowOnly(allowOnly)
    {
    }

    SupplementaryGroup(const gid_t id, bool allowOnly = false)
        : SupplementaryGroup(std::to_string(id), id, "", allowOnly)
    {
    }

    bool conflictsWith(const SupplementaryGroup &) const;
    std::string to_string() const;
    friend void to_json(nlohmann::json & j, const SupplementaryGroup & v);
    friend void from_json(const nlohmann::json & j, SupplementaryGroup & v);
};

/**
 * Single contiguous ID map range of UIDs, GIDs or both.
 *
 * { Type, HostId, MappedId, Range }
 *
 * In configs this is typically parsed from something such as:
 *
 *    "Type=ContainerID:HostID:Range"
 *
 * E.g. u=1000:0:1, g=100:0:1, etc.
 */
struct IDMapping
{
    enum class T : char { User = 'u', Group = 'g', Both = 'b' };

    // 0 is as valid ID as any but not the safest default
    inline static constexpr id_t UNSET = static_cast<id_t>(-1);

    T type = T::Both;       // 1
    id_t host_id = UNSET;   // 2
    id_t mapped_id = UNSET; // 3
    unsigned int range = 1; // 4

    static IDMapping parse(const std::string &);
    static T parseType(const std::string &);
    bool matches(const T) const;
    bool overlapsWith(const IDMapping &) const;
    bool overlapsWithAny(const auto & maps) const;
    std::string to_string() const;
    std::string to_map_string(const bool = false) const;
    friend bool operator==(const IDMapping &, const IDMapping &);
    friend std::strong_ordering operator<=>(const IDMapping &, const IDMapping &);
    friend void to_json(nlohmann::json & j, const IDMapping & t);
    friend void from_json(const nlohmann::json & j, IDMapping & t);
    friend std::ostream & operator<<(std::ostream &, const T &);
    friend std::ostream & operator<<(std::ostream &, const IDMapping &);
};

template<class C, std::enable_if_t<std::is_same_v<typename C::value_type, IDMapping>, int> = 0>
std::ostream & operator<<(std::ostream &, const C &);
extern template std::ostream & operator<<(std::ostream &, const std::set<IDMapping> &);
extern template std::ostream & operator<<(std::ostream &, const std::vector<IDMapping> &);

/**
 * Container for sets of ID-mappings. Second set of IDMapping's
 * denotes "fallback" mappings that are created unless any other mappings are
 * defined that would conflict with them.
 *
 * This is to allow some heuristic defaults be applied when more specific
 * settings are not configured.
 */
class IDMap
{
public:
    using T = IDMapping::T;
    using S = std::set<IDMapping>;
    using V = std::vector<IDMapping>;

    /* kernel only takes up to 4k in size or 340 in number of entries written
     * into uid_map/gid_map */
    inline static constexpr size_t MAX_SIZE = 4096;
    inline static constexpr uint64_t LIMIT = 340;

    IDMap(S && = {}, V && = {});

    bool empty() const;
    void addExplicit(IDMapping);

    /**
     * Mount idmap <id-from>:<id-to> only makes sense when there's a
     * ".host_id = <id-to>" mapping in the sandbox user namespace. If in addition
     * .mapped_id = <id-from> the ID's are just exactly the same inside the
     * sandbox, which probably wasn't intended, so by default we add the mapping
     * host_id == mapped_id instead. */
    void addFallback(const IDMapping & map);

    /**
     * Collects ID mappings for the type so that they do not overlap.
     *
     * Returns only mappings that would be valid in a child namespace of a
     * corresponding parent namespace when "filter" is not empty. Mappings of
     * "filter" should correspond to ID mappings of the parent namespace.
     */
    S collect(const T, const V & filter = {}) const;

    /**
     * Calculate both UID and GID maps (no overlapping).
     */
    S collectBoth() const;

    /**
     * Transform maps of given type: any "from" mapped id is remapped to "to"
     * host id(s). Used to override host-side id for the mapped id when the
     * mapped id is auto-allocated/randomly changes. This way the fresh
     * primary uid corresponds to a stationary host id.
     */
    void transform(const T type, id_t from, id_t to);

    /**
     * Parses arbitrary amount of mappings separated by commas. Only vaidates the
     * format and skips exact duplicates! Results from this may not be accepted
     * for gid_map/uid_map depending on other factors (clashing map ranges,
     * missing mappings in caller namespace, missing permissions, ...)
     */
    static S parse(const std::string &);

    friend bool operator==(const IDMap &, const IDMap &) = default;

    friend void to_json(nlohmann::json & j, const IDMap & t);
    friend void from_json(const nlohmann::json & j, IDMap & t);

    friend std::ostream & operator<<(std::ostream &, const IDMap &);

private:
    S explicit_maps;
    V fallback_maps;
};

/**
 * This tracks all of the ID mappings in a chroot/namespace sandbox: the
 * process-level UID/GID maps, user's primary IDs and supplementary
 * groups, as well as any mappings for ID-mapped mounts (binds).
 */
class SandboxIDMap
{
public:
    /**
     * Build user's UID/GID within the sandbox.
     */
    virtual uid_t sandboxUid() const = 0;
    virtual gid_t sandboxGid() const = 0;
    virtual Path sandboxUserHomeDir() const = 0;
    virtual std::vector<SupplementaryGroup> supplementaryGroups() const = 0;

    /**
     * Add and enable supplemantary groups based on given configuration.
     *
     * Keys are host/parent gid's (can't have duplicates obviously). Values
     * are the mapped/target (gid, name) pairs. Each mapped gid (name) may
     * only appear once in the result.
     *
     * Primary GID (sandboxGid) is always mapped and it would make no
     * sense to assign as a supplementary group as well.
     *
     * It would be technically harmless to allow mapping and assigning 0,
     * aside from some potential confusion. It's only disallowed here
     * because the root group is declared always anyway. Allowing it here
     * would result in a duplicate /etc/group entry.
     *
     * Allowing assigning nogroup 65534 would be very bad, especially if
     * root 0 wasn't mapped, as it's the fallback group to which all
     * unmapped GIDs get mapped to (including root).)
     */
    virtual std::tuple<uid_t, gid_t, uint, std::vector<gid_t>> hostIDs() const;

    /**
     * Get the host-side GIDs that should be assigned with setgroups().
     */
    std::vector<gid_t> getSupplementaryHostGIDs() const;

    /**
     * Format minimal /etc/groups for the sandbox
     */
    void writeEtcGroups(const Path &) const;

    /**
     * Write the user database for the sandbox.
     */
    void writeEtcPasswd(const Path &) const;

    /**
     * Writes the process uid_map, gid_map and setgroups files.
     *
     * Either the writing process has the CAP_SETUID (CAP_SETGID) capability,
     * in which case there are no restrictions on which (parent namespace)
     * user or group IDs can be mapped.
     *
     * Otherwise exactly one user (group) ID can be mapped, it must match the
     * effective user (group) ID of the process that created the namespace,
     * and the writing process must have the same effective user ID.
     */
    void writeIDMapFiles(const pid_t, const IDMapping::T = IDMapping::T::Both) const;

    /**
     * Recover or create usernamespace fd for idmapping.
     */
    int getIDMapUserNsFd(IDMap);

    /**
     * IDMapped mounts' ID maps are separate from the build sandbox's
     * usernamespace by default. But for convenience the maps of id-mapped
     * mounts are recreated in the builder process namespace when it does not
     * overlap with any explicitly declared mapping. */
    void recordMountIDMap(IDMap);

private:
    struct MappedID
    {
        const std::string name;
        id_t hostId;
        uint nrIds;

        MappedID(std::string && name, const id_t hostId, const uint nrIds)
            : name(std::move(name))
            , hostId(hostId)
            , nrIds(nrIds)
        {
        }
    };

public:
    struct MappedGID : public MappedID
    {
        std::set<uid_t> members;

        MappedGID(
            std::string && name = "",
            std::set<uid_t> && users = {},
            const gid_t id = IDMapping::UNSET,
            const uint nrids = 1)
            : MappedID(std::move(name), id, nrids)
            , members(std::move(users))
        {
        }
    };

    struct MappedUID : public MappedID
    {
        std::string desc, homeDir;
        gid_t group;
        std::string shell;

        MappedUID(
            std::string && name = "",
            std::string && desc = "",
            std::string && home = "",
            const gid_t group = IDMapping::UNSET,
            const uid_t id = IDMapping::UNSET,
            const uint nrids = 1,
            std::string && shell = "/noshell")
            : MappedID(std::move(name), id, nrids)
            , desc(std::move(desc))
            , homeDir(std::move(home))
            , group(group)
            , shell(std::move(shell))
        {
            if (desc.empty())
                desc = this->name;
        }
    };

    std::map<uid_t, MappedUID> getSandboxUIDs() const;
    std::map<gid_t, MappedGID> getSandboxGIDs() const;

private:
    /**
     * Groups (GIDs and names) that we will define inside the sandbox's group
     * database. Indexed by the mapped GID.
     */
    mutable std::map<uid_t, MappedUID> _effectiveUIDs;
    mutable std::map<gid_t, MappedGID> _effectiveGIDs;
    mutable std::once_flag _effectiveIDsFlag;

    /** Builder namespace ID maps. */
    IDMap::V mountIDMaps;

    /**
     * User namespace FDs for idmapped mounts. We store each unique map
     * definition for re-use (creating an idmapping fd requires us to
     * setup a whole new user namespace).
     */
    std::map<IDMap::S, AutoCloseFD> userNamespaceFDs = {};

    void calcEffectiveIDs() const;
    void addSandboxID(const uid_t, const MappedUID &, const std::optional<MappedGID> = std::nullopt) const;
    void addSandboxID(const gid_t, const MappedGID &) const;
    void setSupplementaryGroups(const std::vector<gid_t> &) const;
};

/**
 * Writes setgroups if necessary/possible for PID (namespace).
 * In case of child ns, setgroups is inherited from parent ns and cannot be changed,
 * so no exception is raised if that seems to be the case.
 */
bool write_setgroups(const pid_t, const bool = true);

/**
 * Given a ProcessID, ID-map and ID-map type, writes the corresponding uid_map
 * and/or gid_map for the process (e.g. namespace).
 *
 * The inverse parameter should be true when writing maps for idmapped mount
 * namespaces: the nsid and host_id are then flipped.
 */
void writeIDMap(
    const pid_t,
    const IDMap &,
    const IDMapping::T,
    const bool inv = false,
    const std::optional<pid_t> parent = std::nullopt);
void writeIDMap(const Path &, const IDMap::S, const bool inv = false);

/** Read the ID map from "/proc/.../?id_map". */
IDMap::V readIDMapFileThis(const IDMapping::T);
IDMap::V readIDMapFile(const pid_t, const IDMapping::T);
IDMap::V readIDMapFile(const Path &, const IDMapping::T);

int createUsernamespaceWithMappings(const IDMap & mapper);

} // namespace nix
