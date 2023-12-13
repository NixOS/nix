#pragma once
///@file

#include <filesystem>
#include <map>
#include <set>
#include <variant>
#include <pwd.h>
#include <grp.h>
#include <sys/acl.h>
#include "comparator.hh"


/**
 * A C++ API to POSIX ACLs
 */

namespace nix {

    struct UserLock;

namespace ACL {


/*
A template for an Access Control List (ACL); this is instantiated to get both
a "native" interface (which depends on the system for which Nix is built) and
a "generic" interface, which uses the "native" interface to provide a cross-
platform, consistent way to interact with ACLs.
*/
/**
 * ACL_USER
 */
struct User
{
    uid_t uid;

    User(uid_t uid) : uid(uid) {};
    User(struct passwd & pw) : uid(pw.pw_uid) {};
    User(std::string name);
    User(const UserLock & lock);

    GENERATE_CMP(User, me->uid);
};

/**
 * ACL_GROUP
 */
struct Group
{
    gid_t gid;

    Group(gid_t gid) : gid(gid) {};
    Group(struct group & gr) : gid(gr.gr_gid) {};
    Group(std::string name);

    GENERATE_CMP(Group, me->gid);
};

/**
 * An ACL tag; the entity to which the permissions in this particular ACL entry will apply.
 *
 * For groups, all users which are members of this group (either as their
 * primary or secondary group) shall be given the respective permissions.
 */
typedef std::variant<User, Group> Tag;

std::string printTag(Tag t);

namespace Native {
#ifdef __APPLE__

/**
 * ACL type
 */
enum Type {
  /**
   * Unlike Linux, Darwin only has ACL_TYPE_EXTENDED, meaning no default value
   * can be set.
   */
  Extended = acl_type_t::ACL_TYPE_EXTENDED,
};

#define ACL_DEFAULT_TYPE Type::Extended

/**
 * Tag of an ACL entry; tags qualify which entity the given access permission set should be applied to.
 *
 * - @User : Access rights for a user identified by a uuid
 *
 * - @Group : Access rights for a group identified by a uuid
 *
 */
using Tag = Tag;

/**
 * Permission to perform an operation with an object
 */
enum Permission {
    Read_Data           = acl_perm_t::ACL_READ_DATA,
    List_Directory      = acl_perm_t::ACL_LIST_DIRECTORY, // Equivalent to Read_Data
    Read_Attributes     = acl_perm_t::ACL_READ_ATTRIBUTES,
    Read_Extattributes  = acl_perm_t::ACL_READ_EXTATTRIBUTES,
    Read_Security       = acl_perm_t::ACL_READ_SECURITY,

    Write_Data          = acl_perm_t::ACL_WRITE_DATA,
    Add_File            = acl_perm_t::ACL_ADD_FILE, // Equivalent to Write_Data
    Append_Data         = acl_perm_t::ACL_APPEND_DATA,
    Add_Subdirectory    = acl_perm_t::ACL_ADD_SUBDIRECTORY, // Equivalent to Add_Subdirectory
    Delete              = acl_perm_t::ACL_DELETE,
    Delete_Child        = acl_perm_t::ACL_DELETE_CHILD,
    Write_Attributes    = acl_perm_t::ACL_WRITE_ATTRIBUTES,
    Write_Extattributes = acl_perm_t::ACL_WRITE_EXTATTRIBUTES,
    Write_Security      = acl_perm_t::ACL_WRITE_SECURITY,

    Execute             = acl_perm_t::ACL_EXECUTE,
    Search              = acl_perm_t::ACL_SEARCH, // Equivalent to Execute

    // Mostly unused, see comments on what they are for, we do not need nor include these
    // Change_Owner        = acl_perm_t::ACL_CHANGE_OWNER, // Backwards compatibility
    // Synchronize         = acl_perm_t::ACL_SYNCHRONIZE, // Windows interoperability
};

// READ permissions (a set of all of these is equivalent to setting the posix read bit)
#define ACL_PERMISSIONS_READ \
    { \
        nix::ACL::Native::Permission::Read_Data, \
        nix::ACL::Native::Permission::List_Directory, \
        nix::ACL::Native::Permission::Read_Attributes, \
        nix::ACL::Native::Permission::Read_Extattributes, \
        nix::ACL::Native::Permission::Read_Security \
    }

// WRITE permissions (a set of all of these is equivalent to setting the posix write bit)
#define ACL_PERMISSIONS_WRITE \
    { \
        nix::ACL::Native::Permission::Write_Data, \
        nix::ACL::Native::Permission::Add_File, \
        nix::ACL::Native::Permission::Append_Data, \
        nix::ACL::Native::Permission::Add_Subdirectory, \
        nix::ACL::Native::Permission::Delete, \
        nix::ACL::Native::Permission::Delete_Child, \
        nix::ACL::Native::Permission::Write_Attributes, \
        nix::ACL::Native::Permission::Write_Extattributes, \
        nix::ACL::Native::Permission::Write_Security, \
    }

// EXECUTE permissions (a set of all of these is equivalent to setting the posix execute bit)
#define ACL_PERMISSIONS_EXECUTE \
    { \
        nix::ACL::Native::Permission::Execute, \
        nix::ACL::Native::Permission::Search, \
    }

#else

/**
 * The ACL type; 
 */
enum Type {
  /**
   * Access to the object itself
   * (ACL_TYPE_ACCESS)
   */
  Access = ACL_TYPE_ACCESS,
  /**
   * Initial ACL assigned to newly created objects within a directory
   *
   * Note that for sub-directories, this ACL is assigned as both the Access ACL
   * and the Default ACL, meaning it is inherited recursively. There doesn't
   * appear to be a way to prevent this behaviour.
   *
   * (ACL_TYPE_DEFAULT)
   */
  Default = ACL_TYPE_DEFAULT
};

#define ACL_DEFAULT_TYPE Type::Access

/**
 * ACL_USER_OBJ
 */
struct UserObj { GENERATE_CMP(UserObj); };
/**
 * ACL_GROUP_OBJ
 */
struct GroupObj { GENERATE_CMP(GroupObj); };
/**
 * ACL_MASK
 */
struct Mask { GENERATE_CMP(Mask); };
/**
 * ACL_OTHER
 */
struct Other { GENERATE_CMP(Other); };
/**
 * Tag of an ACL entry; tags qualify which entity the given access permission set should be applied to.
 *
 * - @UserObj (ACL_USER_OBJ) : Access rights for the file owner
 *
 * - @User (ACL_USER) : Access rights for a user identified by a uid
 *
 * - @GroupObj (ACL_GROUP_OBJ) : Access rights for the file group
 *
 * - @Group (ACL_GROUP) : Access rights for a group identified by a uid
 *
 * - @Mask (ACL_MASK) : Maximum access rights that can be granted to @User, @GroupObj, or @Group
 *
 * - @Other (ACL_OTHER) : Access rights for processes that don't match any other entry in the ACL
 */
typedef std::variant<UserObj, User, GroupObj, Group, Mask, Other> Tag;

/**
 * Permission to perform an operation with an object
 */
enum Permission {
    /**
     * (ACL_READ)
     */
    Read = ACL_READ,
    /**
     * (ACL_WRITE)
     */
    Write = ACL_WRITE,
    /**
     * (ACL_EXECUTE)
     */
    Execute = ACL_EXECUTE
};

#define ACL_PERMISSIONS_READ {nix::ACL::Native::Permission::Read}
#define ACL_PERMISSIONS_WRITE {nix::ACL::Native::Permission::Write}
#define ACL_PERMISSIONS_EXECUTE {nix::ACL::Native::Permission::Execute}

#endif

/**
 * Access Control List for an object.
 *
 * The access control list for a filesystem object is a map from @Tag types to
 * @Permissions; The @Tag type represents the access control subject (a user
 * or group to which access is granted), and @Permissions represents the set of
 * permissions which are granted to the subject.
 *
 * **Linux notes:**
 *
 * Only one entry of each tag type @UserObj, @GroupObj, @Mask and @Other is
 * possible, and only one entry for each @User and @Group with a unique uid/ gid
 * is possible.
 *
 * For an ACL to be valid, it must have at least @UserObj, @GroupObj and `Other`
 * entries, and, in case at least one @User or @Group entry is present, a @Mask
 * entry (which is optional otherwise); An ACL may have arbitrarily many @User
 * and @Group entries.
 *
 * Refer to `man acl` for an access check algorithm.
 *
 * **Darwin notes:**
 * 
 * The ACL contains @User and @Group tag types. Internally, they are represented
 * by a uuid, and the uid/gid values have to be converted, and as such errors will
 * be thrown if no such uid/gid exists.
 */

class AccessControlList : public std::map<Tag, std::set<Permission>>
{
private:
    /**
     * Construct the C++ wrapper from a C acl struct; Consumes the C struct (frees memory allocated to it)
     */
    AccessControlList(acl_t acl);
    /**
     * Get the C acl struct from the C++ wrapper; The user is expected to call acl_free on the struct when they are done.
     */
    acl_t to_acl();
    // Helper functions that throw instead of returning NULL
    static acl_t _acl_get_fd(int fd);
    static acl_t _acl_get_file(std::filesystem::path file, Type t);
public:
    /**
     * Construct an empty ACL. Note that it may not be valid until you add the necessary entries yourself.
     */
    AccessControlList() { }
    /**
     * Read an ACL of a file pointed to by a file descriptor.
     *
     * Throws a SysError on failure.
     */
    AccessControlList(int fd) : AccessControlList(_acl_get_fd(fd)) {}
    /**
     * Read an ACL from an object at a path.
     *
     * Throws a SysError on failure.
     */
    AccessControlList(std::filesystem::path file, Type t = ACL_DEFAULT_TYPE) : AccessControlList(_acl_get_file(file.c_str(), t)) {};
    /**
     * Write ACL to a file pointed to by a file descriptor.
     *
     * Throws a SysError on failure.
     */
    void set(int fd);
    /**
     * Write ACL to an object pointed to by a path.
     *
     * Throws a SysError on failure.
     */
    void set(std::filesystem::path file, Type t = ACL_DEFAULT_TYPE);
};

}


/**
 * A set of permissions given to a subject.
 *
 * Note that the set of possible permissions differs by platform; however,
 * there are functions provided to translate the per-platform permissions into
 * "traditional" POSIX permissions.
 */
;
class Permissions : std::set<Native::Permission>
{
public:   
    Permissions() {}
    /**
     * Correspondance between this set of permissions and the traditional POSIX permissions
     */
    enum HasPermission {
        /**
         * The subject would NOT be able to perform any operations permitted by traditional permissions
         */
        None = 0,
        /**
         * The subject would be able to perform SOME of the operations permitted by traditional permissions
         */
        Partial = 1,
        /**
         * The subject would be able to perform ANY operation permitted by traditional permissions
         */
        Full = 2
    };

    /**
     * Whether the subject would be able to "read" the object:
     *
     * - Read contents of a file
     * - Read extended attributes
     * - List objects in a directory
     * - Read a symlink's target
     * - Read from a device
     */
    HasPermission canRead();
    /**
     * Whether the subject would be able to "write to" the object:
     *
     * - Write contents of a file
     * - Write extended attributes
     * - Create or delete objects in a directory
     * - Change a symlink's target
     * - Write to a device
     */
    HasPermission canWrite();
    /**
     * Whether the subject would be able to "execute" the object
     *
     * - Execute a file
     * - Change current directory to a directory
     * - Access objects in a directory
     * - Create objects in a directory
     * - Execute a symlink's target
     */
    HasPermission canExecute();

    /**
     * Add (or remove, depending on @allow parameter) the permissions necessary to "read" the object (see @canRead)
     */
    void allowRead(bool allow);
    /**
     * Add (or remove, depending on @allow parameter) the permissions necessary to "write to" the object (see @canWrite)
     */
    void allowWrite(bool allow);
    /**
     * Add (or remove, depending on @allow parameter) the permissions necessary to "execute" the object (see @canExecute)
     */
    void allowExecute(bool allow);

    friend class AccessControlList;

private:
    /**
     * Check this set of permissions against some requirements (used for canRead, canWrite and canExecute)
     */
    HasPermission checkPermission(const std::set<Native::Permission> & requirements);

    Permissions(std::initializer_list<Native::Permission> p);
    Permissions(std::set<Native::Permission> p);

};

/**
 * A generic Access Control List; this is the "lowest common denominator"
 * between the Darwin and Linux ACL interfaces.
 *
 * It allows you to manipulate the Read, Write and Execute permissions,
 * analogous to the traditional Unix permissions (see @Permissions class), of
 * individual @User 's and @Group 's to the given filesystem object.
 */

class AccessControlList : public std::map<Tag, Permissions>
{
public:
    /**
     * Construct an empty ACL; as in, an ACL in which no users or groups have permissions to access the object.
     */
    AccessControlList() { }
    /**
     * Read an ACL from an object at a path.
     *FA
     * Throws a SysError on failure.
     */
    AccessControlList(std::filesystem::path file);
    /**
     * Write ACL to an object pointed to by a path.
     *
     * Throws a SysError on failure.
     */
    void set(std::filesystem::path file);
};

}
}