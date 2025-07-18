#pragma once
///@file

#include <nlohmann/json.hpp>

#include "nix/util/types.hh"

#include <sys/mount.h>

namespace nix {

typedef uint64_t MountFlags;

enum ExtraMountFlags : MountFlags {
    /* Reverse the meaning of option(s) (such as MS_NOSUID). */
    MS_REV = static_cast<MountFlags>(1) << 63,

    /* Whether or not to resolve symlinks in source path. */
    MS_SOURCE_NOCANON = static_cast<MountFlags>(1) << 62,

    /* Whether or not to resolve symlinks in target path. */
    MS_TARGET_NOCANON = static_cast<MountFlags>(1) << 61,
};

template<typename E>
constexpr std::enable_if_t<!std::is_same_v<E, ExtraMountFlags>, MountFlags>
operator|(E lhs, ExtraMountFlags rhs) noexcept
{
    return static_cast<MountFlags>(lhs) | static_cast<MountFlags>(rhs);
}

#define MOUNT_OPTION_ITEM_1(name, key, val_n)    \
    MOUNT_OPTION_ITEM(name, key, val_n | MS_REV) \
    MOUNT_OPTION_ITEM(no##name, "no" key, val_n)

#define MOUNT_OPTION_ITEM_2(name, key, val, r_prefix, r_suffix) \
    MOUNT_OPTION_ITEM(name, key, val)                           \
    MOUNT_OPTION_ITEM(name##rec, r_prefix key r_suffix, val | MS_REC)

#define MOUNT_OPTION_ITEM_3(name, key, val, r_prefix, r_suffix) \
    MOUNT_OPTION_ITEM_1(name, key, val)                         \
    MOUNT_OPTION_ITEM_1(name##rec, r_prefix key r_suffix, val | MS_REC)

/* (r)private: this is the default, no propagation in either direction.
 * slave: one-way propagation from host -> container of top-level mounts only.
 * rslave: one-way propagation from host -> container of all mounts recursively.
 * (r)unbindable: like (r)private but further restricts bind-mounting of the target path. */
#define MOUNT_OPTION_LIST                                                       \
    MOUNT_OPTION_ITEM(unknown, "", static_cast<MountFlags>(0))                  \
    MOUNT_OPTION_ITEM_2(ro, "ro", MS_RDONLY, "", "=rec")                        \
    MOUNT_OPTION_ITEM_2(rw, "rw", MS_RDONLY | MS_REV, "", "=rec")               \
    MOUNT_OPTION_ITEM_3(suid, "suid", MS_NOSUID, "", "=rec")                    \
    MOUNT_OPTION_ITEM_3(dev, "dev", MS_NODEV, "", "=rec")                       \
    MOUNT_OPTION_ITEM_3(exec, "exec", MS_NOEXEC, "", "=rec")                    \
    MOUNT_OPTION_ITEM_3(symfollow, "symfollow", MS_NOSYMFOLLOW, "", "=rec")     \
    MOUNT_OPTION_ITEM_3(diratime, "diratime", MS_NODIRATIME, "", "=rec")        \
    MOUNT_OPTION_ITEM_2(noatime, "noatime", MS_NOATIME, "", "=rec")             \
    MOUNT_OPTION_ITEM_2(relatime, "relatime", MS_RELATIME, "", "=rec")          \
    MOUNT_OPTION_ITEM_2(strictatime, "strictatime", MS_STRICTATIME, "", "=rec") \
    MOUNT_OPTION_ITEM_1(canonsrc, "canonsrc", MS_SOURCE_NOCANON)                \
    MOUNT_OPTION_ITEM_1(canondst, "canondst", MS_TARGET_NOCANON)                \
    MOUNT_OPTION_ITEM_2(private_, "private", MS_PRIVATE, "r", "")               \
    MOUNT_OPTION_ITEM_2(slave, "slave", MS_SLAVE, "r", "")                      \
    MOUNT_OPTION_ITEM_2(unbindable, "unbindable", MS_UNBINDABLE, "r", "")

inline constexpr MountFlags MOUNT_OPTIONS_ATIME =
    MS_NOATIME | MS_RELATIME | MS_STRICTATIME | MOUNT_ATTR_NOATIME | MOUNT_ATTR_RELATIME | MOUNT_ATTR_STRICTATIME;

inline constexpr MountFlags MOUNT_OPTIONS_PROPAGATION = MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE;

inline constexpr MountFlags MOUNT_OPTIONS_MASKS[] = {
    MOUNT_OPTIONS_ATIME,
    MOUNT_OPTIONS_PROPAGATION,
};

/**
 * Map options for use with mount_setattr.
 */
inline constexpr std::pair<MountFlags, MountFlags> mountAttrs[] = {
    {MS_RDONLY, MOUNT_ATTR_RDONLY},
    {MS_NOSUID, MOUNT_ATTR_NOSUID},
    {MS_NODEV, MOUNT_ATTR_NODEV},
    {MS_NOEXEC, MOUNT_ATTR_NOEXEC},
    {MS_NOSYMFOLLOW, MOUNT_ATTR_NOSYMFOLLOW},
    {MS_NODIRATIME, MOUNT_ATTR_NODIRATIME},   // implied by NOATIME
    {MS_NOATIME, MOUNT_ATTR_NOATIME},         // needs MOUNT_ATTR_ATIME in attr_clr
    {MS_RELATIME, MOUNT_ATTR_RELATIME},       // needs MOUNT_ATTR_ATIME in attr_clr
    {MS_STRICTATIME, MOUNT_ATTR_STRICTATIME}, // needs MOUNT_ATTR_ATIME in attr_clr
};

inline constexpr std::pair<std::string_view, MountFlags> mountOptionStrings[] = {
#define MOUNT_OPTION_ITEM(name, key, value) {key, value},
    MOUNT_OPTION_LIST
#undef MOUNT_OPTION_ITEM
};

/**
 * Retrieves current mount options for mountpoint.
 */
MountFlags getMountOpts(const Path &);

/**
 * Returns true if the two sets of options are effectively the same.
 * That is, differences in flags that do not affect options are ignored.
 */
bool mountOptsEqual(const MountFlags, const MountFlags);

/**
 * Show mount option bits as a human-readable string.
 */
std::string mountOptsToString(const MountFlags, const bool rec);

//* MountOpt

enum class MountOpt : MountFlags {
#define MOUNT_OPTION_ITEM(name, k, value) name = value,
    MOUNT_OPTION_LIST
#undef MOUNT_OPTION_ITEM
};

#define MOUNT_OPTION_ITEM(name, key, value) {MountOpt::name, key},
NLOHMANN_JSON_SERIALIZE_ENUM(MountOpt, {MOUNT_OPTION_LIST})
#undef MOUNT_OPTION_ITEM

constexpr std::string mountOptionToString(const MountOpt opt) noexcept
{
#define MOUNT_OPTION_ITEM(name, key, value) \
    case MountOpt::name:                    \
        return key;
    switch (opt) {
        MOUNT_OPTION_LIST
    default:
        return "";
    }
#undef MOUNT_OPTION_ITEM
};

/**
 * Merge mount option into the option set.
 */
MountFlags mergeIntoMountOpts(const MountFlags, const MountOpt);

/**
 * Merge an arbitrary number of mount options.
 */
template<typename I, typename... Args>
MountFlags combineMountOpts(MountFlags res, const I & opts, const Args... rest)
{
    if constexpr (std::is_same_v<I, MountOpt>)
        res = mergeIntoMountOpts(res, opts);
    else
        for (auto it = std::begin(opts); it != std::end(opts); ++it) {
            res = mergeIntoMountOpts(res, *it);
        }
    if constexpr (sizeof...(Args) > 0)
        return combineMountOpts(res, rest...);
    else
        return res;
};

std::string mountOptsToString(const std::vector<MountOpt> &);

//* BindMountPath

class BindMountPathImpl
{
public:
    /* Options applied for every sandbox path, except if explictly overridden. */
    constexpr static std::array defaultOptions = {
        MountOpt::nosuid,
        MountOpt::private_rec,
    };
    virtual void prepare();
    virtual void bindMount(const Path &);
    static std::vector<MountOpt> compactMountOpts(const std::vector<MountOpt> &);
protected:
    bool useNewAPI = false;
    virtual Path getSource() const = 0;
    virtual std::vector<MountOpt> getOptions() const = 0;
    virtual bool isOptional() const = 0;
    virtual bool isRecursive() const = 0;
    void setOptions();
    void mountLegacy(const Path &) const;
    bool openTree();
private:
    bool prepared = false;
    bool sourceIsDir = true;
    /* Used with the open_tree/mount_setattr/move_mount API. */
    int mountFD_ = -1;
    struct mount_attr attr = {}, attrRec = {};
    bool canonSource = true, canonTarget = false;
    void setOption(const MountOpt);
};

template<typename T>
class BindMountPath : public BindMountPathImpl
{
    Path getSource() const override
    {
        return static_cast<const T *>(this)->source;
    };

    bool isOptional() const override
    {
        return static_cast<const T *>(this)->optional;
    };

    bool isRecursive() const override
    {
        return static_cast<const T *>(this)->recursive;
    };

    std::vector<MountOpt> getOptions() const override
    {
        std::vector<MountOpt> res;
        for (auto o : defaultOptions)
            res.push_back(o);
        if (static_cast<const T *>(this)->readOnly)
            res.push_back(MountOpt::ro);
        for (auto o : static_cast<const T *>(this)->options)
            res.push_back(o);
        return res;
    };

public:
    void prepare() override
    {
        setOptions();
        BindMountPathImpl::prepare();
    };
};

}
