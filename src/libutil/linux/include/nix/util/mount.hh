#pragma once
///@file

#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

#include <sys/mount.h>

#define MOUNT_OPTION_ITEM_2(name, key, val_n)    \
    MOUNT_OPTION_ITEM(name, key, val_n | MS_REV) \
    MOUNT_OPTION_ITEM(no##name, "no" key, val_n)
#define MOUNT_OPTION_ITEM_3(name, key, val, r_prefix, r_suffix) \
    MOUNT_OPTION_ITEM(name, key, val)                           \
    MOUNT_OPTION_ITEM(name##rec, r_prefix key r_suffix, val | MS_REC)
#define MOUNT_OPTION_ITEM_4(name, key, val, r_prefix, r_suffix) \
    MOUNT_OPTION_ITEM_2(name, key, val)                         \
    MOUNT_OPTION_ITEM_2(name##rec, r_prefix key r_suffix, val | MS_REC)
#define MOUNT_OPTION_LIST                                                       \
    MOUNT_OPTION_ITEM(unknown, "", static_cast<MountFlags>(0))                  \
    MOUNT_OPTION_ITEM_3(ro, "ro", MS_RDONLY, "", "=rec")                        \
    MOUNT_OPTION_ITEM_3(rw, "rw", MS_RDONLY | MS_REV, "", "=rec")               \
    MOUNT_OPTION_ITEM_4(suid, "suid", MS_NOSUID, "", "=rec")                    \
    MOUNT_OPTION_ITEM_4(dev, "dev", MS_NODEV, "", "=rec")                       \
    MOUNT_OPTION_ITEM_4(exec, "exec", MS_NOEXEC, "", "=rec")                    \
    MOUNT_OPTION_ITEM_4(symfollow, "symfollow", MS_NOSYMFOLLOW, "", "=rec")     \
    MOUNT_OPTION_ITEM_4(diratime, "diratime", MS_NODIRATIME, "", "=rec")        \
    MOUNT_OPTION_ITEM_3(noatime, "noatime", MS_NOATIME, "", "=rec")             \
    MOUNT_OPTION_ITEM_3(relatime, "relatime", MS_RELATIME, "", "=rec")          \
    MOUNT_OPTION_ITEM_3(strictatime, "strictatime", MS_STRICTATIME, "", "=rec") \
    MOUNT_OPTION_ITEM_2(canonsrc, "canonsrc", MS_SOURCE_NOCANON)                \
    MOUNT_OPTION_ITEM_2(canondst, "canondst", MS_TARGET_NOCANON)                \
    MOUNT_OPTION_ITEM_3(private_, "private", MS_PRIVATE, "r", "")               \
    MOUNT_OPTION_ITEM_3(slave, "slave", MS_SLAVE, "r", "")                      \
    MOUNT_OPTION_ITEM_3(unbindable, "unbindable", MS_UNBINDABLE, "r", "")

namespace nix {

using MountFlags = uint64_t;

enum class ExtraMountFlags : MountFlags {
    /* Reverse the meaning of option(s) (such as MS_NOSUID). */
    MS_REV = static_cast<MountFlags>(1) << 63,

    /* Whether or not to resolve symlinks in source path. */
    MS_SOURCE_NOCANON = static_cast<MountFlags>(1) << 62,

    /* Whether or not to resolve symlinks in target path. */
    MS_TARGET_NOCANON = static_cast<MountFlags>(1) << 61,
};

using enum ExtraMountFlags;

constexpr MountFlags operator~(ExtraMountFlags val) noexcept
{
    return ~static_cast<MountFlags>(val);
};

template<typename T>
constexpr std::enable_if_t<std::is_same_v<T, ExtraMountFlags> || std::is_convertible_v<T, MountFlags>, MountFlags>
operator|(T lhs, ExtraMountFlags rhs) noexcept
{
    return static_cast<MountFlags>(lhs) | static_cast<MountFlags>(rhs);
};

template<typename T>
constexpr std::enable_if_t<std::is_same_v<T, ExtraMountFlags> || std::is_convertible_v<T, MountFlags>, MountFlags>
operator&(T lhs, ExtraMountFlags rhs) noexcept
{
    return static_cast<MountFlags>(lhs) & static_cast<MountFlags>(rhs);
};

enum class MountOpt : MountFlags {
#define MOUNT_OPTION_ITEM(name, _, value) name = static_cast<MountFlags>(value),
    MOUNT_OPTION_LIST
#undef MOUNT_OPTION_ITEM
};

constexpr inline MountFlags MOUNTOPTS_ATIME_MASK =
    MS_NOATIME | MS_RELATIME | MS_STRICTATIME | MOUNT_ATTR_NOATIME | MOUNT_ATTR_RELATIME | MOUNT_ATTR_STRICTATIME;

constexpr inline MountFlags MOUNTOPTS_PROPAGATION_MASK = MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE;

/** Map options for use with mount_setattr. */
constexpr inline std::pair<MountFlags, MountFlags> mountAttrFlags[] = {
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

constexpr inline std::pair<MountOpt, MountFlags> mountOptItems[] = {
#define MOUNT_OPTION_ITEM(name, _, value) {MountOpt::name, static_cast<MountFlags>(value)},
    MOUNT_OPTION_LIST
#undef MOUNT_OPTION_ITEM
};

constexpr std::string mountOptToString(const MountOpt opt) noexcept
{
#define MOUNT_OPTION_ITEM(name, key, _) \
    case MountOpt::name:                \
        return key;
    switch (opt) {
        MOUNT_OPTION_LIST
    default:
        return "<unknown>";
    }
#undef MOUNT_OPTION_ITEM
};

#define MOUNT_OPTION_ITEM(name, key, _) {MountOpt::name, key},
NLOHMANN_JSON_SERIALIZE_ENUM(MountOpt, {MOUNT_OPTION_LIST})
#undef MOUNT_OPTION_ITEM

std::vector<MountOpt> optsFromFlags(const MountFlags);
MountFlags mergeMountOpts(MountFlags, const MountOpt);
MountFlags mergeMountOpts(MountFlags, const std::vector<MountOpt> &);

std::vector<MountOpt> compactMountOpts(const std::vector<MountOpt> &);

class MountOpts
{
    std::vector<MountOpt> opts; // options from configuration
    bool rec_ = true;           // enable separation of recursive options?
    std::optional<bool> canonSource_, canonTarget_;
    mutable struct mount_attr attr_ = {}, attrRec_ = {};
    void setOption(MountOpt opt);

public:
    MountOpts(std::vector<MountOpt> && = {}, const bool rec = true);
    MountOpts(const MountFlags);
    MountOpts(const Path &); /* Retrieves current mount options for mountpoint. */

    /* query */
    friend bool operator==(const MountOpts &, const MountOpts &);
    std::vector<MountOpt> getOpts() const;
    bool canonSource(bool = true) const;
    bool canonTarget(bool = false) const;
    mount_attr getMountAttr(bool rec) const;
    MountFlags getFlags() const;
    std::string to_string() const;

    /* modify */
    void update(std::optional<bool> rec = std::nullopt);
    void append(const std::vector<MountOpt> & newOpts);
};

class BindMountPathImpl
{
private:
    bool prepared = false;
    bool useNewAPI = false;
    bool sourceIsDir = true;
    MountOpts mountOpts_;

    /* Used with the open_tree/mount_setattr/move_mount API. */
    int mountFD_ = -1;

    bool openTree();
    void mountLegacy(const Path &) const;

protected:
    virtual Path getSource() const = 0;
    virtual bool isOptional() const = 0;
    virtual bool isRecursive() const = 0;
    virtual std::vector<MountOpt> getOptions() const = 0;

public:
    /* Options applied for every sandbox path, except if explictly overridden. */
    static constexpr inline std::array defaultOptions = {
        MountOpt::nosuid,
        MountOpt::private_rec,
    };

    void bindMount(const Path &);
    virtual void prepare();
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
};

}
