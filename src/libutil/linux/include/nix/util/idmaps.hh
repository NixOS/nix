#pragma once

#include "nix/util/util.hh"
#include "nix/util/linux-namespaces.hh"

#include <sys/mount.h>

namespace nix {

/* kernel only takes up to 4k in size or 340 in number of entries written
 * into uid_map/gid_map */
inline constexpr size_t IDMAP_MAX_SIZE = 4096;
inline constexpr uint64_t IDMAP_LIMIT = 340;

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

    T type;                 // 1
    id_t host_id = UNSET;   // 2
    id_t mapped_id = UNSET; // 3
    unsigned int range = 1; // 4

    friend bool operator==(const IDMapping &, const IDMapping &);
    friend std::strong_ordering operator<=>(const IDMapping &, const IDMapping &);

    bool overlaps_with(const IDMapping & other) const;
    bool overlaps_with_any(auto maps) const;
    bool contains(const T) const;
    std::string to_string() const;
    std::string to_map_string(const bool = false) const;
    friend std::ostream & operator<<(std::ostream &, const T &);
    friend std::ostream & operator<<(std::ostream &, const IDMapping &);

    static IDMapping parse(const std::string &);
    static T parse_type(const char);

    // IDMapping JSON serialisation (object | string)
    template<typename BasicJsonType>
    friend void to_json(BasicJsonType & j, const IDMapping & t)
    {
        // serialise as object
        j["type"] = fmt("%s", t.type);
        j["mount"] = t.mapped_id;
        if (t.mapped_id != t.host_id)
            j["host"] = t.host_id;
        if (t.range != 1)
            j["count"] = t.range;
        // serialise as string
        // j = t.to_string();
    }

    /* IDMapping JSON parse (object | string) */
    template<typename BasicJsonType>
    friend void from_json(const BasicJsonType & j, IDMapping & t)
    {
        using nlohmann::json;
        if (j.is_string())
            t = parse(j);
        else if (j.is_object()) {
            try {
                std::string type_s = j.value("type", "b");
                t.type = parse_type(type_s[0]);
            } catch (const json::out_of_range & e) {
                throw json::parse_error::create(101, 0, fmt("ID mapping with no type field: %s", e.what()), nullptr);
            }
            try {
                t.mapped_id = j.at("mount");
            } catch (const json::out_of_range & e) {
                throw json::parse_error::create(
                    101, 0, fmt("ID mapping without value for from: %s", e.what()), nullptr);
            }
            t.host_id = j.value("host", t.mapped_id);
            t.range = j.value("count", 1);
        } else
            throw json::parse_error::create(101, 0, "ID map was not a string or object.", nullptr);
    }
};

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
    using Vec = std::set<IDMapping>;

private:
    Vec explicit_maps, fallback_maps;

public:
    IDMap(const Vec & = {}, const Vec & = {});

    void add_explicit(IDMapping map);

    bool empty() const
    {
        return explicit_maps.empty();
    };

    /**
     * Mount idmap <id-from>:<id-to> only makes sense when there's a
     * ".host_id = <id-to>" mapping in the sandbox user namespace. If in addition
     * .mapped_id = <id-from> the ID's are just exactly the same inside the
     * sandbox, which probably wasn't intended, so by default we add the mapping
     * host_id == mapped_id instead. */
    void add_fallback(const IDMapping & map);

    /**
     * Calculate both UID and GID maps (no overlapping).
     */
    Vec collectBoth() const;
    Vec collect(const IDMapping::T, const Vec & = {}) const;

    /**
     * Transform maps of given type: any "from" mapped id is remapped to "to"
     * host id(s). Used to override host-side id for the mapped id when the
     * mapped id is auto-allocated/randomly changes. This way the fresh
     * primary uid corresponds to a stationary host id.
     */
    void transform(const IDMapping::T type, id_t from, id_t to);

    /**
     * Parses arbitrary amount of mappings separated by commas. Only vaidates the
     * format and skips exact duplicates!! Results from this may not be accepted
     * for gid_map/uid_map depending on other factors (clashing map ranges,
     * missing mappings in caller namespace, missing permissions, ...)
     */
    static Vec parse(const std::string &);

    friend bool operator==(const IDMap &, const IDMap &) = default;

    friend std::ostream & operator<<(std::ostream &, const IDMap &);

    template<typename BasicJsonType>
    friend void to_json(BasicJsonType & j, const IDMap & t)
    {
        to_json(j, t.explicit_maps);
    };

    // Parsing a set of ID maps from either a string (separated by ",") or array.
    template<typename BasicJsonType>
    friend void from_json(const BasicJsonType & j, IDMap & t)
    {
        if (j.is_string())
            t = IDMap(IDMap::parse(j));
        else if (j.is_array())
            for (const auto & j2 : j) {
                IDMapping m;
                from_json(j2, m);
                t.add_explicit(std::move(m));
            }
        else
            throw nlohmann::json::parse_error::create(101, 0, "ID map was not a string or array", nullptr);
    };
};

/**
 * This tracks all of the ID mappings in a chroot/namespace sandbox: the
 * process-level UID/GID maps, user's primary IDs and supplementary
 * groups, as well as any mappings for ID-mapped mounts (binds).
 */
class SandboxIDMap
{
    struct SandboxGroup
    {
        std::string name;
        std::set<std::string> members = {};
    };

private:
    /**
     * Builder namespace ID maps.
     */
    IDMap primaryIDMap;

    /**
     * Groups (GIDs and names) that we will define inside the sandbox's group
     * database. Indexed by the mapped GID.
     */
    std::map<gid_t, SandboxGroup> sandboxGroups = {{0, {"root"}}, {65534, {"nogroup"}}};

    /**
     * Host GID -> Sandbox GID.
     */
    std::map<gid_t, gid_t> supplementaryGIDs;

    /**
     * User namespace FDs for idmapped mounts. We store each unique map
     * definition for re-use (creating an idmapping fd requires us to
     * setup a whole new user namespace).
     */
    std::map<IDMap::Vec, AutoCloseFD> userNamespaceFDs = {};

    std::optional<bool> _useSupplementaryGroups;

    bool useSupplementaryGroupsInner()
    {
        if (!_useSupplementaryGroups.has_value())
            _useSupplementaryGroups = std::optional(useSupplementaryGroups());
        return *_useSupplementaryGroups;
    };

public:
    /**
     * Build user's UID/GID within the sandbox.
     */
    virtual uid_t sandboxUid() const = 0;
    virtual gid_t sandboxGid() const = 0;

    /**
     * Mapped username.
     */
    virtual std::string sandboxUser() const
    {
        return "nixbld";
    };

    /**
     * Mapped primary group's name.
     */
    virtual std::string sandboxGroup() const
    {
        return "nixbld";
    };

    gid_t hostGid = (gid_t) -1;
    uid_t hostUid = (uid_t) -1;

    /* Host primary UID and GID. */
    void setPrimaryID(uid_t uid, gid_t gid, uint nrids)
    {
        hostUid = uid;
        hostGid = gid;
        primaryIDMap.add_explicit({IDMapping::T::User, uid, sandboxUid(), nrids});
        primaryIDMap.add_explicit({IDMapping::T::Group, gid, sandboxGid(), nrids});
    };

    /* Whether or not supplementary groups should be set. If false sup groups
     * are emptied. */
    virtual bool useSupplementaryGroups() const
    {
        return false;
    };

    /**
     * Define groups for the sandbox.
     */
    void addSandboxGroup(const gid_t, std::string, const std::set<std::string> & members = {});

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
     * unmapped GIDs get mapped to (including root).) */
    void addSupplementaryGroups(const SupplementaryGroups &, const std::vector<gid_t> &);

    /**
     * Get the host-side GIDs that should be assigned with setgroups().
     */
    std::vector<gid_t> supplementaryHostGIDs();

    /**
     * Format minimal /etc/groups for the sandbox
     */
    void writeGroupsFile(const Path &);

    /**
     * Writes the process uid_map, gid_map and setgroups files.
     */
    void writeIDMapFiles(const pid_t, const IDMapping::T = IDMapping::T::Both);

    /**
     * Write the user database for the sandbox.
     */
    void writePasswdFile(const Path &, Path & homeDir) const;

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
};

void write_setgroups(const pid_t, const bool = true);
int createUsernamespaceWithMappings(const IDMap & mapper);
void writeIDMap(Path, const IDMap::Vec idmap, const bool inverse = false);
}
