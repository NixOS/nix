#include "acl.hh"
#include "error.hh"
#include "finally.hh"
#include "util.hh"

#include <initializer_list>
#include <iostream>

#if __linux__
#include <acl/libacl.h>
#endif

#ifdef __APPLE__
#include <membership.h>
#endif

/*
 * The acl_get_entry function returns a 0 on success on Darwin, but a 1 Linux
 */
#if __APPLE__
#define GET_ENTRY_SUCCESS 0
#else
#define GET_ENTRY_SUCCESS 1
#endif

namespace nix::ACL
{

User::User(std::string name)
{
    if (passwd * pw = getpwnam(name.c_str()))
        uid = pw->pw_uid;
    else
        if (errno == 0 || errno == ENOENT || errno == ESRCH || errno == EBADF || errno == EPERM)
            throw Error("user '%s' does not exist", name);
        else
            throw SysError("unable to get the passwd entry for user '%s'", name);
}

Group::Group(std::string name)
{
    if (group * gr = getgrnam(name.c_str()))
        gid = gr->gr_gid;
    else
        if (errno == 0 || errno == ENOENT || errno == ESRCH || errno == EBADF || errno == EPERM)
            throw Error("group '%s' does not exist", name);
        else
            throw SysError("unable to get group information for group '%s'", name);
}

namespace Native
{
acl_t AccessControlList::_acl_get_fd(int fd)
{
    acl_t acl = acl_get_fd(fd);
    if (!acl) throw SysError("getting ACL of a file pointed to by fd %d", fd);
    return acl;
}

acl_t AccessControlList::_acl_get_file(std::filesystem::path path, Type t)
{
#ifdef __APPLE__
    // On Linux, a file with an empty ACL returns just that, an empty ACL.
    // On Darwin, NULL is returned instead with errno set to ENOENT (No such
    // file or directory), even though the file/directory does exist.
    acl_t acl = acl_get_file(path.c_str(), (acl_type_t) t);
    if (!acl && std::filesystem::exists(path)) {
        // False error, path does exists, create empty acl
        acl = acl_init(0);
    }
#else
    acl_t acl = acl_get_file(path.c_str(), t);
#endif
    if (!acl) throw SysError("getting ACL of an object %s", path);
    return acl;
}

void _acl_get_permset(acl_entry_t entry, acl_permset_t * permset)
{
    if (acl_get_permset(entry, permset) != 0) throw SysError("getting a permission set of an ACL");
}

void * _acl_get_qualifier(acl_entry_t entry, const std::string & qualifier_type)
{
    void * qualifier = acl_get_qualifier(entry);
    if (!qualifier) throw SysError("getting an ACL %s qualifier", qualifier_type);
    return qualifier;
}

void _acl_free(acl_t acl)
{
    if (acl_free(acl) != 0) throw SysError("freeing memory allocated by an ACL");
}

bool _acl_get_perm(acl_permset_t perms, acl_perm_t perm)
{
#if __APPLE__
    return acl_get_perm_np(perms, perm);
#else
    return acl_get_perm(perms, perm);
#endif
}

AccessControlList::AccessControlList(acl_t acl)
{
    Finally free {[&](){ _acl_free(acl); }};

    int entry_id = ACL_FIRST_ENTRY;
    acl_entry_t entry;
    while (acl_get_entry(acl, entry_id, &entry) == GET_ENTRY_SUCCESS) {
        entry_id = ACL_NEXT_ENTRY;
        acl_tag_t tag;
        if (acl_get_tag_type(entry, &tag) != 0) throw SysError("getting ACL tag type");
        // Placed in optional, because creating a default user on MacOS seems dangerous
        std::optional<Tag> entity = {};
        switch (tag) {
#if __APPLE__
            case ACL_UNDEFINED_TAG:
                warn("encountered an undefined ACL Tag");
                break;
            case ACL_EXTENDED_ALLOW: {
                void * guid = _acl_get_qualifier(entry, "guid");
                uid_t ugid;
                int idtype;

                if (mbr_uuid_to_id((const unsigned char *) guid, &ugid, &idtype) != 0) {
                    throw Error("converting a guid_t to a uid/gid");
                }

                switch (idtype) {
                    case ID_TYPE_UID:
                        entity = User(ugid);
                        break;
                    case ID_TYPE_GID:
                        entity = Group((gid_t) ugid);
                        break;
                    default:
                        throw Error("unknown ACL qualifier type %d", idtype);
                }
                acl_free(guid);
                break;
            }
            case ACL_EXTENDED_DENY:
                // TODO(ACLs) our model currently does not model DENY ACLs
                throw Error("ACLS: TODO");
#else
            case ACL_USER_OBJ:
                entity = UserObj {};
                break;

            case ACL_USER:
                entity = User {* (uid_t*) _acl_get_qualifier(entry, "uid")};
                break;

            case ACL_GROUP_OBJ:
                entity = GroupObj {};
                break;

            case ACL_GROUP:
                entity = Group {* (gid_t*) _acl_get_qualifier(entry, "gid")};
                break;

            case ACL_MASK:
                entity = Mask {};
                break;

            case ACL_OTHER:
                entity = Other {};
                break;
#endif

            default:
                throw Error("unknown ACL tag type %d", tag);
        }

        std::set<Permission> p;
        acl_permset_t permset;
        _acl_get_permset(entry, &permset);
#ifdef __APPLE__
        if (_acl_get_perm(permset, acl_perm_t::ACL_READ_DATA)) p.insert(Permission::Read_Data);
        if (_acl_get_perm(permset, acl_perm_t::ACL_LIST_DIRECTORY)) p.insert(Permission::List_Directory);
        if (_acl_get_perm(permset, acl_perm_t::ACL_WRITE_DATA)) p.insert(Permission::Write_Data);
        if (_acl_get_perm(permset, acl_perm_t::ACL_ADD_FILE)) p.insert(Permission::Add_File);
        if (_acl_get_perm(permset, acl_perm_t::ACL_EXECUTE)) p.insert(Permission::Execute);
        if (_acl_get_perm(permset, acl_perm_t::ACL_SEARCH)) p.insert(Permission::Search);
        if (_acl_get_perm(permset, acl_perm_t::ACL_DELETE)) p.insert(Permission::Delete);
        if (_acl_get_perm(permset, acl_perm_t::ACL_APPEND_DATA)) p.insert(Permission::Append_Data);
        if (_acl_get_perm(permset, acl_perm_t::ACL_ADD_SUBDIRECTORY)) p.insert(Permission::Add_Subdirectory);
        if (_acl_get_perm(permset, acl_perm_t::ACL_DELETE_CHILD)) p.insert(Permission::Delete_Child);
        if (_acl_get_perm(permset, acl_perm_t::ACL_READ_ATTRIBUTES)) p.insert(Permission::Read_Attributes);
        if (_acl_get_perm(permset, acl_perm_t::ACL_WRITE_ATTRIBUTES)) p.insert(Permission::Write_Attributes);
        if (_acl_get_perm(permset, acl_perm_t::ACL_READ_EXTATTRIBUTES)) p.insert(Permission::Read_Extattributes);
        if (_acl_get_perm(permset, acl_perm_t::ACL_WRITE_EXTATTRIBUTES)) p.insert(Permission::Write_Extattributes);
        if (_acl_get_perm(permset, acl_perm_t::ACL_READ_SECURITY)) p.insert(Permission::Read_Security);
        if (_acl_get_perm(permset, acl_perm_t::ACL_WRITE_SECURITY)) p.insert(Permission::Write_Security);
        // if (_acl_get_perm(permset, acl_perm_t::ACL_CHANGE_OWNER)) p.insert(Permission::Change_Owner);
        // if (_acl_get_perm(permset, acl_perm_t::ACL_SYNCHRONIZE)) p.insert(Permission::Synchronize);
#else
        if (_acl_get_perm(permset, ACL_READ)) p.insert(Permission::Read);
        if (_acl_get_perm(permset, ACL_WRITE)) p.insert(Permission::Write);
        if (_acl_get_perm(permset, ACL_EXECUTE)) p.insert(Permission::Execute);
#endif
        if (entity.has_value())
            insert({entity.value(), p});
        else
            throw Error("adding the entity of the acl");
    }
}

void _acl_set_tag_type(acl_entry_t entry, acl_tag_t tag)
{
    if (acl_set_tag_type(entry, tag) != 0) throw SysError("setting an ACL tag type");
}

void _acl_set_qualifier(acl_entry_t entry, void* qualifier, const std::string & qualifier_type)
{
    if (acl_set_qualifier(entry, qualifier) != 0) throw SysError("setting an ACL %s qualifier", qualifier_type);
}

acl_t AccessControlList::to_acl()
{
    acl_t acl = acl_init(size());
    if (!acl) throw SysError("initializing an ACL");
    for (auto [tag, perms] : *this) {
        acl_entry_t entry;
        if (acl_create_entry(&acl, &entry) != 0) throw SysError("creating an ACL entry");
#ifdef __APPLE__
        std::visit(overloaded {
            [&](User u){
                _acl_set_tag_type(entry, acl_tag_t::ACL_EXTENDED_ALLOW);
                uuid_t uu;
                if (mbr_uid_to_uuid(u.uid, uu) != 0) {
                    throw SysError("converting a uid to a uuid");
                }
                _acl_set_qualifier(entry, (void*) &uu, "uid");
            },
            [&](Group g){
                _acl_set_tag_type(entry, acl_tag_t::ACL_EXTENDED_ALLOW);
                uuid_t uu;
                if (mbr_uid_to_uuid(g.gid, uu) != 0) {
                    throw SysError("converting a gid to a uuid");
                }
                _acl_set_qualifier(entry, (void*) &uu, "gid");
            },
        }, tag);
#else
        std::visit(overloaded {
            [&](UserObj _){ _acl_set_tag_type(entry, ACL_USER_OBJ); },
            [&](User u){ _acl_set_tag_type(entry, ACL_USER); _acl_set_qualifier(entry, (void*) &u.uid, "uid"); },
            [&](GroupObj _){ _acl_set_tag_type(entry, ACL_GROUP_OBJ); },
            [&](Group g){ _acl_set_tag_type(entry, ACL_GROUP); _acl_set_qualifier(entry, (void*) &g.gid, "gid"); },
            [&](Mask _){ _acl_set_tag_type(entry, ACL_MASK); },
            [&](Other _){ _acl_set_tag_type(entry, ACL_OTHER); },
        }, tag);
#endif
        acl_permset_t permset;
        _acl_get_permset(entry, &permset);
        for (auto perm : perms) {
#ifdef __APPLE__
            if (acl_add_perm(permset, (acl_perm_t) perm) != 0)
#else
            if (acl_add_perm(permset, perm) != 0)
#endif
                throw SysError("adding permissions to an ACL permission set");
        }
    }
    return acl;
}

void AccessControlList::set(int fd)
{
    acl_t acl = to_acl();
    Finally free {[&](){ _acl_free(acl); }};
    if (acl_set_fd(fd, acl) != 0) throw SysError("setting ACL on a file pointed to by fd %d", fd);
}

void AccessControlList::set(std::filesystem::path file, Type t)
{
    acl_t acl = to_acl();
    Finally free {[&](){ _acl_free(acl); }};

#ifdef __APPLE__
    if (acl_set_file(file.c_str(), (acl_type_t) t, acl) != 0)
#else
    if (acl_set_file(file.c_str(), t, acl) != 0)
#endif
        throw SysError("setting ACL of an object %s", file);
}
}

/* Generic interface */

Permissions::Permissions(std::initializer_list<Native::Permission> perms)
{
    insert(perms);
}
Permissions::Permissions(std::set<Native::Permission> perms)
{
    insert(perms.begin(), perms.end());
}

bool intersects(const std::set<Native::Permission> & a, const std::set<Native::Permission> & b)
{
    for (auto & el : a) if (b.contains(el)) return true;
    return false;
}
bool matches(const std::set<Native::Permission> & a, const std::set<Native::Permission> & b)
{
    for (auto & el : a) if (!b.contains(el)) return false;
    return true;
}

Permissions::HasPermission Permissions::checkPermission(const std::set<Native::Permission> & reqs)
{
    if (matches(*this, reqs)) return Full;
    else if (intersects(*this, reqs)) return Partial;
    else return None;
}

Permissions::HasPermission Permissions::canRead()
{
    return checkPermission(ACL_PERMISSIONS_READ);
}
Permissions::HasPermission Permissions::canWrite()
{
    return checkPermission(ACL_PERMISSIONS_WRITE);
}
Permissions::HasPermission Permissions::canExecute()
{
    return checkPermission(ACL_PERMISSIONS_EXECUTE);
}

void Permissions::allowRead(bool allow)
{
    std::set perms ACL_PERMISSIONS_READ;
    if (allow)
        insert(perms.begin(), perms.end());
    else
        erase(perms.begin(), perms.end());
}
void Permissions::allowWrite(bool allow)
{
    std::set perms ACL_PERMISSIONS_WRITE;
    if (allow)
        insert(perms.begin(), perms.end());
    else
        erase(perms.begin(), perms.end());
}
void Permissions::allowExecute(bool allow)
{
    std::set perms ACL_PERMISSIONS_EXECUTE;
    if (allow)
        insert(perms.begin(), perms.end());
    else
        erase(perms.begin(), perms.end());
}

AccessControlList::AccessControlList(std::filesystem::path p)
{
    auto native = Native::AccessControlList(p);
    for (auto & [k, v] : native) {
        if (auto tag = std::get_if<User>(&k))
            insert({*tag, v});
        else if (auto tag = std::get_if<Group>(&k))
            insert({*tag, v});
    }
}

void AccessControlList::set(std::filesystem::path p)
{
    using namespace Native;
    Native::AccessControlList native;
    for (auto [k, v] : *this) {
        if (auto tag = std::get_if<User>(&k))
            native.insert({*tag, v});
        else if (auto tag = std::get_if<Group>(&k))
            native.insert({*tag, v});
    }
#ifndef __APPLE__
    // On Linux, preserve non-extended ACL entries
    Native::AccessControlList current(p);
    native[UserObj {}] = current[UserObj {}];
    native[GroupObj {}] = current[GroupObj {}];
    native[Other {}] = current[Other {}];
    if (!empty())
        native[Mask {}] = {Permission::Read, Permission::Write, Permission::Execute};
#endif
    native.set(p);
}

}
