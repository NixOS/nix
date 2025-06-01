#include "nix/util/idmaps.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"

#include <fstream>

#include <grp.h>

namespace nix {

template<typename fstream>
static fstream open_fstream(const Path & filepath)
{
    fstream fs(filepath);
    if (!fs)
        throw SysError("could not open file '%s'", filepath);
    return fs;
}

template<typename... Args>
static nlohmann::json::exception jsonValidationError(const std::string & formatString, const Args &... formatArgs)
{
    return nlohmann::json::other_error::create(599, fmt(formatString, formatArgs...), nullptr);
}

static bool hasCapSetGid()
{
    static Path filepath = "/proc/self/status";
    static auto CAP_SETGID = 6;
    auto ifs = open_fstream<std::ifstream>(filepath);
    unsigned long long capEff = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        if (hasPrefix(line, "CapEff:"))
            sscanf(line.substr(8).c_str(), "%llx", &capEff);
    }
    return (capEff & CAP_SETGID) ? true : false;
}

//+ SupplementaryGroup

bool SupplementaryGroup::conflictsWith(const SupplementaryGroup & other) const
{
    return group == other.group || (gid && gid == other.gid);
}

std::string SupplementaryGroup::to_string() const
{
    return nlohmann::json(*this).dump();
}

void to_json(nlohmann::json & j, const SupplementaryGroup & v)
{
    j.emplace("group", v.group);
    if (v.gid.has_value())
        j.emplace("gid", v.gid);
    if (!v.name.empty())
        j.emplace("name", v.name);
    if (v.allowOnly)
        j.emplace("allow-only", v.allowOnly);
};

void from_json(const nlohmann::json & j, SupplementaryGroup & v)
{
    static auto getGroup = [](const auto & j) {
        if (j.is_string()) {
            auto s = j.template get<std::string>();
            if (s.empty())
                throw jsonValidationError("group must not be empty");
            return s;
        } else if (j.is_number()) {
            const int64_t id = j.template get<int64_t>();
            if (id < 0)
                throw jsonValidationError("group ID cannot be negative: %1%", j.dump());
            return std::to_string(id);
        } else
            throw jsonValidationError("expected string or number: %1%", j.dump());
    };
    if (j.is_object()) {
        v.group = getGroup(j.at("group"));
        v.gid = j.value("gid", v.gid);
        v.name = j.value("name", v.name);
        v.allowOnly = j.value("allow-only", v.allowOnly);
    } else
        v.group = getGroup(j);
};

//+ IDMapping::T

IDMapping::T IDMapping::parseType(const std::string & s)
{
    if (s.empty())
        throw UsageError("ID-mapping: type must not be empty");
    else if (s.size() == 1)
        switch (s[0]) {
        case static_cast<char>(T::Both):
            return T::Both;
        case static_cast<char>(T::User):
            return T::User;
        case static_cast<char>(T::Group):
            return T::Group;
        default:
            throw UsageError("Unknown ID-mapping type: '%1%' (%2%)", s[0], s);
        }
    else if (s.compare("both") == 0)
        return T::Both;
    else if (s.compare("user") == 0)
        return T::User;
    else if (s.compare("group") == 0)
        return T::Group;
    else
        throw UsageError("Unknown idmap type: '%s'", s);
}

std::ostream & operator<<(std::ostream & os, const IDMapping::T & t)
{
    return os << static_cast<char>(t);
}

//+ IDMapping

bool operator==(const IDMapping & a, const IDMapping & b)
{
    return (a.type == b.type) && (a.host_id == b.host_id) && (a.mapped_id == b.mapped_id) && (a.range == b.range);
}

std::strong_ordering operator<=>(const IDMapping & a, const IDMapping & b)
{
    if (a.type != b.type)
        return a.type <=> b.type;
    if (a.host_id != b.host_id)
        return a.host_id <=> b.host_id;
    if (a.mapped_id != b.mapped_id)
        return a.mapped_id <=> b.mapped_id;
    return a.range <=> b.range;
}

std::ostream & operator<<(std::ostream & os, const IDMapping & m)
{
    return os << m.to_string();
}

template<class C, std::enable_if_t<std::is_same_v<typename C::value_type, IDMapping>, int>>
std::ostream & operator<<(std::ostream & os, const C & xs)
{
    os << std::string("IDMappings[");
    for (auto it = std::begin(xs); it != std::end(xs);)
        os << it->to_string() << (++it != std::end(xs) ? ", " : "");
    return os << "]";
};
template std::ostream & operator<<(std::ostream & os, const std::set<IDMapping> & xs);
template std::ostream & operator<<(std::ostream & os, const std::vector<IDMapping> & xs);

std::string IDMapping::to_map_string(const bool inverse) const
{
    assert(range > 0);
    return inverse ? fmt("%d %d %d", host_id, mapped_id, range) : fmt("%d %d %d", mapped_id, host_id, range);
}

std::string IDMapping::to_string() const
{
    return fmt("%c:%d:%d:%d", static_cast<char>(type), mapped_id, host_id, range);
}

IDMapping IDMapping::parse(const std::string & str)
{
    auto parts = splitString<Strings>(str, "=-:/");

    if (parts.size() < 1 || parts.size() > 4)
        UsageError("Invalid ID-mapping format: '%s'", str);

    IDMapping res;

    if (!parts.front().empty() && !std::isdigit(parts.front()[0])) {
        res.type = parseType(parts.front());
        parts.pop_front();
    } else
        res.type = T::Both;

    if (parts.empty()) {
        throw UsageError("Invalid ID-mapping: '%s'", str);
    }
    res.mapped_id = *string2Int<id_t>(parts.front());
    parts.pop_front();

    if (parts.empty()) {
        res.host_id = res.mapped_id;
        return res;
    }
    res.host_id = *string2Int<id_t>(parts.front());
    parts.pop_front();

    if (!parts.empty())
        res.range = *string2Int<uint>(parts.front());

    return res;
}

bool IDMapping::matches(const T ot) const
{
    return type == ot || type == T::Both || ot == T::Both;
}

bool IDMapping::overlapsWith(const IDMapping & m) const
{
    return matches(m.type)
           && (((mapped_id < m.mapped_id + m.range) && (mapped_id + range > m.mapped_id))
               || ((host_id < m.host_id + m.range) && (host_id + range > m.host_id)));
}

bool IDMapping::overlapsWithAny(const auto & maps) const
{
    return std::find_if(maps.begin(), maps.end(), [&](const IDMapping & m) { return this->overlapsWith(m); })
           != maps.end();
}

void to_json(nlohmann::json & j, const IDMapping & t)
{
    j.emplace("type", std::string(1, static_cast<char>(t.type)));
    j.emplace("mount", t.mapped_id);
    if (t.mapped_id != t.host_id)
        j.emplace("host", t.host_id);
    if (t.range != 1)
        j.emplace("count", t.range);
}

void from_json(const nlohmann::json & j, IDMapping & t)
{
    using nlohmann::json;
    if (j.is_string())
        t = IDMapping::parse(j);
    else if (j.is_object()) {
        try {
            t.type = IDMapping::parseType(j.value("type", "b"));
        } catch (const json::out_of_range & e) {
            throw jsonValidationError("ID mapping with no type field: %s", e.what());
        }
        try {
            t.mapped_id = j.at("mount");
        } catch (const json::out_of_range & e) {
            throw jsonValidationError("ID mapping without value for from: %s", e.what());
        }
        t.host_id = j.value("host", t.mapped_id);
        t.range = j.value("count", 1);
    } else
        throw jsonValidationError("ID map was not a string or object.");
}

//+ IDMap

// IDMap::IDMap(const S & expl, const V & fallback)
// for (const auto & m : expl)
//     addExplicit(m);

IDMap::IDMap(S && expl, V && fallback)
{
    explicit_maps = std::move(expl);
    fallback_maps = std::move(fallback);
}

bool IDMap::empty() const
{
    return explicit_maps.empty() && fallback_maps.empty();
};

void IDMap::addExplicit(IDMapping m)
{
    if (m.overlapsWithAny(explicit_maps))
        throw Error("ID-mapping '%s' overlaps with another mapping", m);
    explicit_maps.insert(m);
}

void IDMap::addFallback(const IDMapping & m)
{
    fallback_maps.push_back({m.type, m.mapped_id, m.mapped_id, m.range});
}

void IDMap::transform(const IDMapping::T type, id_t from, id_t to)
{
    debug("idmap transform: type:%s mapped:[%d -> %d]", type, from, to);
    for (auto m : explicit_maps)
        if (m.matches(type) && m.mapped_id == from) {
            explicit_maps.erase(m);
            m.mapped_id = to;
            addExplicit(m);
        }
    for (auto * m = fallback_maps.data(); m != fallback_maps.data() + fallback_maps.size(); ++m)
        if (m->matches(type) && m->mapped_id == from)
            m->mapped_id = to;
}

IDMap::S IDMap::collect(const IDMapping::T type, const IDMap::V & filter) const
{
    auto matches = [](auto fis, auto q) {
        for (auto & fi : fis) {
            if (!fi.matches(q.type))
                continue;
            if ((fi.mapped_id <= q.host_id) && (q.host_id + q.range <= fi.mapped_id + fi.range))
                return true;
        }
        return false;
    };
    S res;
    for (auto m : explicit_maps)
        if (m.matches(type) && (filter.empty() || matches(filter, m))) {
            m.type = type;
            res.insert(m);
        }
    for (auto m : fallback_maps)
        if (m.matches(type) && (filter.empty() || matches(filter, m)) && !m.overlapsWithAny(res)) {
            m.type = type;
            res.insert(m);
        }
    if (res.empty()) { // min. 1 map has to be defined
        res.insert({type, 0, 0, 1});
        warn("Empty ID map - defaulting to 0:0:1 [%s] (filter: %s)", *this, filter);
    }
    if (res.size() > LIMIT)
        throw Error("Too many mappings (>%d)", LIMIT);
    return res;
}

IDMap::S IDMap::collectBoth() const
{
    S res = collect(IDMapping::T::User);
    S ms = collect(IDMapping::T::Group);
    res.insert(ms.begin(), ms.end());
    return res;
}

IDMap::S IDMap::parse(const std::string & str)
{
    S res;
    for (auto items : splitString<Strings>(str, ",\n\t\r"))
        if (!items.empty())
            res.insert(IDMapping::parse(std::move(items)));
    return res;
}

std::ostream & operator<<(std::ostream & os, const IDMap & map)
{
    auto go = [](const auto & xs) {
        return concatMapStringsSep(",", xs, [](const auto & x) { return x.to_string(); });
    };
    os << fmt("IDMap(explicit: %s; fallback: %s)", go(map.explicit_maps), go(map.fallback_maps));
    return os;
}

void to_json(nlohmann::json & j, const IDMap & t)
{
    to_json(j, t.explicit_maps);
}

// Parsing a set of ID maps from either a string (separated by ",") or array.
void from_json(const nlohmann::json & j, IDMap & t)
{
    if (j.is_string())
        t = IDMap(IDMap::parse(j));
    else if (j.is_array())
        for (const auto & j2 : j) {
            IDMapping m;
            from_json(j2, m);
            t.addExplicit(std::move(m));
        }
    else
        throw jsonValidationError("ID map was not a string or array");
}

//+ SandboxIDMap

void SandboxIDMap::calcEffectiveIDs() const
{
    std::call_once(_effectiveIDsFlag, [this]() {
        auto [hostUid, hostGid, nrIds, supplGIDs] = hostIDs();
        addSandboxID(0, MappedUID("root", "Nix build user", sandboxUserHomeDir()), MappedGID("root"));
        addSandboxID(65534, MappedUID("nobody", "Noboby", "/"), MappedGID("nogroup"));
        addSandboxID(
            sandboxUid(),
            MappedUID("nixbld", "Nix build user", sandboxUserHomeDir(), sandboxGid(), hostUid, nrIds),
            MappedGID("nixbld", {}, hostGid, nrIds));
        setSupplementaryGroups(supplGIDs);
    });
}

std::map<uid_t, SandboxIDMap::MappedUID> SandboxIDMap::getSandboxUIDs() const
{
    calcEffectiveIDs();
    return _effectiveUIDs;
}

std::map<gid_t, SandboxIDMap::MappedGID> SandboxIDMap::getSandboxGIDs() const
{
    calcEffectiveIDs();
    return _effectiveGIDs;
}

void SandboxIDMap::addSandboxID(const uid_t id, const MappedUID & val, const std::optional<MappedGID> mgroup) const
{
    auto user = val;
    if (user.homeDir.empty())
        user.homeDir = sandboxUserHomeDir();
    if (user.group == IDMapping::UNSET)
        user.group = id;
    _effectiveUIDs.insert({id, user});
    if (mgroup)
        addSandboxID(user.group, *mgroup);
}

void SandboxIDMap::addSandboxID(const gid_t id, const MappedGID & val) const
{
    for (auto uid : val.members)
        if (!_effectiveUIDs.contains(uid))
            throw Error("Group %1%: declared member with user ID %2% does not exist", id, uid);
    _effectiveGIDs.insert({id, val});
}

std::tuple<uid_t, gid_t, uint, std::vector<gid_t>> SandboxIDMap::hostIDs() const
{
    return {geteuid(), getegid(), 1, {}};
}

void SandboxIDMap::setSupplementaryGroups(const std::vector<gid_t> & builderGIDs) const
{
    auto supGroups = supplementaryGroups();
    if (supGroups.empty())
        return;

    if (!hasCapSetGid()) {
        warn("supplementary groups are disabled (CAP_SETGID required)");
        return;
    }

    // Host GID sanity checks
    const auto validateHostGID = [this, &builderGIDs](const auto gid, const auto & sg) {
        if (gid == 0)
            throw Error("Group '%1%': mapping the root group (GID 0) is not a good idea", sg.group);
        else if (sg.allowOnly && std::none_of(builderGIDs.begin(), builderGIDs.end(), [gid](auto id) {
                     return id == gid;
                 })) {
            debug("Group '%1%': ignored (group is allow-only and the build user is not member)", sg.group);
            return;
        } else if (std::any_of(
                       _effectiveGIDs.begin(), _effectiveGIDs.end(), [gid](auto v) { return v.second.hostId == gid; }))
            throw Error("Group '%1%': host GID %2% is already mapped", sg.group, gid);
    };

    /* Mapped GID sanity checks

       65535 is the special invalid/overflow GID distinct from
       nogroup. We don't want to allow that.

       2^16 and above are not allowed because it would seem impossible
       to assign them. In a quick test higher GIDs got truncated to
       65534. Might have something to do with how we're forced to call
       setgroups before setting up the namespace. */
    const auto validateNamespaceGID = [this](const gid_t gid, const auto & sg) -> gid_t {
        if (gid > 65534)
            throw Error("Group '%1%': mapped GID %2% is too large (>65534)", sg.group, gid);
        else if (_effectiveGIDs.contains(gid))
            throw Error("Group '%1%': mapped GID %2% conflicts with reserved GID", sg.group, gid);
        return gid;
    };

    // Ensure the mapped name is unique.
    const auto validateNamespaceGroupName = [this](const std::string & def, const auto & sg) {
        std::string name = sg.name.empty() ? def : sg.name;
        int counter = 1;
        while (std::any_of(
            _effectiveGIDs.begin(), _effectiveGIDs.end(), ([name](auto v) { return v.second.name == name; }))) {
            if (!sg.name.empty())
                throw Error("Group '%1%': requested name '%2%' conflicts with another group", sg.group, name);
            if (counter++ == 1)
                name += "-host";
            else
                name = fmt("%s-%i", def, counter);
            debug("Group '%1%': name conflicts with reserved name; attempting rename to '%2%'...", sg.group, name);
        }
        return name;
    };

    struct group gr;
    struct group * grPtr = nullptr;
    std::vector<char> buf;
    static long bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
    if (bufsize == -1)
        bufsize = 16384;
    buf.resize(bufsize);

    const auto getgr = [&](const auto group) {
        int ret;
        while (true) {
            if constexpr (std::is_integral<decltype(group)>::value)
                ret = getgrgid_r(group, &gr, buf.data(), buf.size(), &grPtr);
            else
                ret = getgrnam_r(group.c_str(), &gr, buf.data(), buf.size(), &grPtr);
            if (ret == 0) {
                break;
            } else if (ret == ERANGE) { // buffer too small
                bufsize *= 2;
                buf.resize(bufsize);
            } else
                throw Error("Getting group '%1%' failed: %2%", group, strerror(ret));
        }
    };

    debug("Resolving requested supplementary groups (%d)", supGroups.size());

    for (const auto & sg : supGroups) {
        gid_t hostGID;
        std::string nameDef;

        if (!sg.group.empty() && std::isdigit(sg.group[0])) {
            hostGID = *string2Int<gid_t>(sg.group);
            getgr(hostGID);
        } else {
            getgr(sg.group);
        }
        if (grPtr) {
            hostGID = grPtr->gr_gid;
            nameDef = grPtr->gr_name;
        } else if (sg.gid) {
            hostGID = *sg.gid;
            nameDef = fmt("group%i", hostGID);
        } else {
            debug("No such group: %1%", sg.group);
            return;
        }
        validateHostGID(hostGID, sg);
        auto nsGID = validateNamespaceGID(sg.gid ? *sg.gid : hostGID, sg);
        auto name = validateNamespaceGroupName(nameDef, sg);
        addSandboxID(nsGID, MappedGID(std::move(name), {sandboxUid()}, hostGID));
    }
}

std::vector<gid_t> SandboxIDMap::getSupplementaryHostGIDs() const
{
    std::vector<gid_t> res = {};
    for (const auto & [nsGid, g] : getSandboxGIDs())
        if (g.hostId != IDMapping::UNSET && nsGid != sandboxGid())
            for (auto gid = g.hostId; gid < g.hostId + g.nrIds; ++gid)
                res.push_back(gid);
    return res;
}

void SandboxIDMap::writeEtcGroups(const Path & file) const
{
    auto ofs = open_fstream<std::ofstream>(file);
    for (const auto & [gid, gr] : getSandboxGIDs())
        ofs << fmt(
            "%s:x:%d:%s\n",
            gr.name,
            gid,
            concatMapStringsSep(",", std::vector(gr.members.begin(), gr.members.end()), [&](auto x) {
                return getSandboxUIDs().at(x).name;
            }));
}

void SandboxIDMap::writeEtcPasswd(const Path & file) const
{
    auto ofs = open_fstream<std::ofstream>(file);
    for (auto [uid, u] : getSandboxUIDs())
        ofs << fmt("%1%:x:%2%:%3%:%4%:%5%:%6%\n", u.name, uid, u.group, u.desc, u.homeDir, u.shell);
}

void SandboxIDMap::writeIDMapFiles(const pid_t pid, const IDMapping::T type) const
{
    IDMap idmap;
    for (auto [uid, u] : getSandboxUIDs())
        if (u.hostId != IDMapping::UNSET)
            idmap.addExplicit({IDMapping::T::User, u.hostId, uid, u.nrIds});
    for (auto [gid, g] : getSandboxGIDs())
        if (g.hostId != IDMapping::UNSET)
            idmap.addExplicit({IDMapping::T::Group, g.hostId, gid, g.nrIds});
    for (auto m : mountIDMaps)
        idmap.addFallback(m);
    debug("Writing IDMaps for UIDs and GIDs for PID %i using %s", pid, idmap);
    if (type != IDMapping::T::User)
        write_setgroups(pid, true);
    writeIDMap(pid, idmap, type);
}

void SandboxIDMap::recordMountIDMap(IDMap idmap)
{
    for (auto m : idmap.collectBoth())
        mountIDMaps.push_back(m);
}

int SandboxIDMap::getIDMapUserNsFd(IDMap idmap)
{
    if (idmap.empty())
        return -1;

    /* Little convenience: if the target 1000:100 (either) is mapped in
       the mount, then modify it to match with the build user's host map
       instead. So that what mount's id-map maps into targets 1000:100
       becomes the mapping into the builder's *mapped* (=sandbox) IDs. (So
       you can have builders with a mapped UID 1000 and randomly
       changing host UID and filesystem) */
    idmap.transform(IDMapping::T::User, sandboxUid(), getSandboxUIDs().at(sandboxUid()).hostId);
    idmap.transform(IDMapping::T::Group, sandboxGid(), getSandboxGIDs().at(sandboxGid()).hostId);

    // Create new user ns and get its fd, unless the same mapping already
    // has a stored fd in which case copy that.
    auto [fds, _] =
        userNamespaceFDs.try_emplace(idmap.collectBoth(), [&] { return createUsernamespaceWithMappings(idmap); }());
    return fds->second.get();
}

//+ Functions

IDMap::V readIDMapFileThis(const IDMapping::T type)
{
    return readIDMapFile(fmt("/proc/self/%cid_map", (char) type), type);
}

IDMap::V readIDMapFile(const pid_t pid, const IDMapping::T type)
{
    return readIDMapFile(fmt("/proc/%i/%cid_map", pid, type), type);
}

IDMap::V readIDMapFile(const Path & filepath, const IDMapping::T type)
{
    std::ifstream file(filepath);
    if (!file)
        throw SysError("Opening file for reading: %s", filepath);
    IDMap::V result;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        auto items = tokenizeString<std::vector<std::string>>(line, " \t\n");
        result.push_back({
            .type = type,
            .host_id = *string2Int<id_t>(items[1]),
            .mapped_id = *string2Int<id_t>(items[0]),
            .range = *string2Int<uint>(items[2]),
        });
    }
    if (result.empty())
        warn("Read an empty ID map from: '%s'", filepath);
    return result;
}

bool write_setgroups(const pid_t pid, const bool deny)
{
    auto filepath = fmt("/proc/%i/setgroups", pid);
    try {
        writeFile(filepath, deny ? "deny" : "allow");
        return true;
    } catch (SysError & e) {
        if (e.errNo != EACCES)
            throw;
    }
    warn("could not write to setgroups file: '%s'", filepath);
    return false;
}

void writeIDMap(
    const pid_t pid,
    const IDMap & idmap,
    const IDMapping::T type,
    const bool inverse,
    const std::optional<pid_t> parent)
{
    if (type == IDMapping::T::Both) {
        writeIDMap(pid, idmap, IDMapping::T::User, inverse, parent);
        writeIDMap(pid, idmap, IDMapping::T::Group, inverse, parent);
        return;
    }
    auto filter = parent ? readIDMapFile(*parent, type) : readIDMapFileThis(type);
    Path filepath = fmt("/proc/%d/%cid_map", pid, (char) type);
    writeIDMap(filepath, idmap.collect(type, filter), inverse);
}

void writeIDMap(const Path & filepath, const IDMap::S ids, const bool inverse)
{
    std::ostringstream oss;
    for (const auto & m : ids)
        oss << m.to_map_string(inverse) << "\n";
    std::string content = oss.str();
    if (content.size() > IDMap::MAX_SIZE)
        throw Error("Size of ID map exceeds the 4K length limit: '%s'", ids);
    debug("Writing ID map [%s] to file: '%s'", ids, filepath);
    writeFile(filepath, content); // this should be a single write()
}

int createUsernamespaceWithMappings(const IDMap & mapper)
{
    static const char SYNC_PARENT_NAMESPACE_READY = '1';
    static const char SYNC_PARENT_ERREXIT = '0';
    static const char SYNC_CHILD_EXIT = 'X';

    debug("new user namespace for ID-mapping: '%s'", mapper);

    // child-to-parent / other way around
    Pipe pipeC2P, pipeP2C;
    pipeC2P.create();
    pipeP2C.create();

    auto syncProcWrite = [](Pipe & pipe, char tkn, std::string_view msg = "", bool close = false) {
        auto fd = pipe.writeSide.get();
        writeLine(fd, fmt("%c", tkn));
        if (!msg.empty())
            writeFull(fd, fmt("%s\n", msg));
        if (close)
            pipe.writeSide.close();
    };

    auto syncProcRead = [](const Pipe & pipe, char tkn) {
        auto fd = pipe.readSide.get();
        auto ln = readLine(fd, true);
        if (ln.empty() || ln[0] != tkn)
            throw Error("Unexpected response from process: %s (%s)", ln, readFile(fd));
    };

    Pid pid(startProcess(
        [&]() {
            pipeC2P.readSide.close();
            pipeP2C.writeSide.close();
            try {
                if (unshare(CLONE_NEWUSER) == -1)
                    throw SysError("new user ns for idmap (is UID:GID 0:0 mapped in caller namespace?)");
                syncProcWrite(pipeC2P, SYNC_PARENT_NAMESPACE_READY);
                syncProcRead(pipeP2C, SYNC_CHILD_EXIT);
            } catch (Error & e) {
                syncProcWrite(pipeC2P, SYNC_PARENT_ERREXIT, e.message(), true);
                _exit(1);
            }
            _exit(0);
        },
        {.cloneFlags = SIGCHLD}));
    pipeC2P.writeSide.close();
    pipeP2C.readSide = -1;

    syncProcRead(pipeC2P, SYNC_PARENT_NAMESPACE_READY);

    // Write setgroups, uid_map & gid_map
    write_setgroups(pid);
    writeIDMap(pid, mapper, IDMapping::T::Both, true);

    // Open namespace fd
    int userFd = open(fmt("/proc/%d/ns/user", (pid_t) pid).c_str(), O_RDONLY /*| O_CLOEXEC*/ | O_NOCTTY);
    if (userFd < 0)
        throw SysError("open(userFd)");

    syncProcWrite(pipeP2C, SYNC_CHILD_EXIT, "", true);

    if (pid.wait() != 0)
        throw Error("idmap: process did not exit gracefully");

    return userFd;
}

}
