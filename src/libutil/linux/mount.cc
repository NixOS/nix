#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/mount.hh"
#include "nix/util/strings.hh"

#include <sys/statvfs.h>
#include <sys/vfs.h>

namespace nix {

template<typename I, typename MFunc>
static void mergeOpts(const I & opts, const MFunc merge)
{
    for (const auto opt : opts)
        merge(opt);
}

static void mergeOptsIntoMountAttr(struct mount_attr * attr, struct mount_attr * attrRec, const MountOpt bindopt)
{
    static auto setter = [](const MountFlags opt, auto & at) {
        auto propagationFlags = opt & MOUNT_OPTIONS_PROPAGATION;
        if (propagationFlags) {
            at->propagation = propagationFlags;
            return;
        }
        auto setAttr = 0;
        for (const auto [ms_f, sa_f] : mountAttrs)
            if (ms_f & opt)
                setAttr |= sa_f;
        if (!setAttr) {
            return;
        } else if (opt & MS_REV) {
            at->attr_clr |= setAttr;
            at->attr_set &= ~setAttr;
        } else if (setAttr & MOUNT_OPTIONS_ATIME) {
            at->attr_clr |= MOUNT_ATTR__ATIME;
            at->attr_set &= ~MOUNT_OPTIONS_ATIME;
            at->attr_set |= setAttr;
        } else {
            at->attr_clr &= ~setAttr;
            at->attr_set |= setAttr;
        }
    };

    const MountFlags opt = static_cast<MountFlags>(bindopt);

    setter(opt, attr);
    if ((opt & MS_REC) && attr != attrRec)
        setter(opt, attrRec);
}

//* MountFlags

MountFlags getMountOpts(const Path & mp)
{
    struct statfs buf;
    if (statfs(mp.c_str(), &buf) == -1)
        SysError("statfs: %s", mp);
    // some flags reported by statfs correspond with MS_* flags, others not.
    MountFlags res = buf.f_flags & (ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC | ST_NOATIME | ST_NODIRATIME);
    if (buf.f_flags & ST_RELATIME)
        res |= MS_RELATIME;
    if (buf.f_flags & ST_NOSYMFOLLOW)
        res |= MS_NOSYMFOLLOW;
    return res;
};

bool mountOptsEqual(const MountFlags a, const MountFlags b)
{
    static constexpr MountFlags ignore = MS_BIND | MS_REMOUNT | MS_REC | MS_SOURCE_NOCANON | MS_TARGET_NOCANON;
    static constexpr auto f = [](const auto x) {
        // NODIRATIME is implied by NOATIME
        return ((x & MS_NOATIME) ? (x | MS_NODIRATIME) : x) & ~ignore;
    };
    return f(a) == f(b);
};

std::string mountOptsToString(const MountFlags opts, const bool rec)
{
    std::vector<std::string> ss = {};
    auto add = [&ss](const std::string_view s) {
        if (!s.empty())
            ss.push_back(std::string(s));
    };
    for (const auto & [sv, flag] : mountOptionStrings) {
        if (flag & MS_REV)
            continue;
        if (rec != (flag & MS_REC))
            continue;
        if (opts & (flag & ~MS_REC))
            add(sv);
        add(sv);
    }
    for (const auto & [fs, fs_attr] : mountAttrs) {
        if (opts & fs_attr)
            add(mountOptionToString(static_cast<MountOpt>(fs)));
    }
    return concatStringsSep(",", ss);
};

//* MountOpt

MountFlags mergeIntoMountOpts(const MountFlags opts, const MountOpt opt)
{
    const auto o = static_cast<MountFlags>(opt);
    if (o & MS_SOURCE_NOCANON)
        return opts;
    if (o & MS_TARGET_NOCANON)
        return opts;
    auto res = opts;
    if (o & MS_REV) {
        res &= ~(o & ~MS_REV);
    } else {
        for (const auto mask : MOUNT_OPTIONS_MASKS)
            if (o & mask)
                res &= ~mask;
        res |= o;
    }
    return res;
};

std::string mountOptsToString(const std::vector<MountOpt> & opts)
{
    const auto result = BindMountPathImpl::compactMountOpts(opts) | std::views::transform(mountOptionToString);
    return concatStringsSep(",", std::vector(result.begin(), result.end()));
};

//* BindMountPathImpl

void BindMountPathImpl::prepare()
{
    if (((attr.propagation | attrRec.propagation) & (MS_SHARED | MS_SLAVE)) || !canonSource || canonTarget
        || attrRec.attr_set || attrRec.attr_clr)
        useNewAPI = true;
    if (useNewAPI)
        // Need to open source early?
        if ((attr.propagation | attrRec.propagation) & (MS_SHARED | MS_SLAVE))
            openTree();
    prepared = true;
}

void BindMountPathImpl::setOptions()
{
    attr = {};
    attrRec = {};
    auto merge = [this](const auto & opt) { this->setOption(opt); };
    mergeOpts(getOptions(), merge);
}

void BindMountPathImpl::setOption(const MountOpt opt)
{
    const auto f = static_cast<MountFlags>(opt);
    if (f & MS_SOURCE_NOCANON)
        canonSource = f & MS_REV;
    else if (f & MS_TARGET_NOCANON)
        canonTarget = f & MS_REV;
    else {
        // Aggregate recursive options separately only if the mount itself is
        // recursive, otherwise there's no difference.
        mergeOptsIntoMountAttr(&attr, (sourceIsDir && isRecursive()) ? &attrRec : &attr, opt);
    }
}

std::vector<MountOpt> BindMountPathImpl::compactMountOpts(const std::vector<MountOpt> & opts)
{
    auto to_key = [](auto & o) {
        auto key = static_cast<MountFlags>(o) & ~MS_REV;
        auto rec = key & MS_REC;
        // atime options are mutually exclusive
        if (key & MOUNT_OPTIONS_ATIME)
            return MS_NOATIME | rec;
        else if (key & MOUNT_OPTIONS_PROPAGATION)
            return MS_PRIVATE | rec;
        else
            return key;
    };
    std::vector<MountOpt> res = {};
    std::set<MountFlags> keys = {0};
    for (auto it = opts.rbegin(); it != opts.rend(); ++it) {
        auto key = to_key(*it);
        if (!keys.contains(key)) {
            keys.insert(key);
            if (key & MS_REC) {
                // When adding a recursive option, mark the non-recursive key as
                // well.
                keys.insert(key & ~MS_REC);
                // Check options forward for a non-recursive option which is
                // the same (has no effect) and delete it.
                for (auto i2 = res.begin(); i2 != res.end(); ++i2) {
                    if (static_cast<MountFlags>(*i2) == (static_cast<MountFlags>(*it) & ~MS_REC)) {
                        res.erase(i2);
                        break;
                    }
                }
            }
            res.insert(res.begin(), *it);
        }
    }
    return res;
}

bool BindMountPathImpl::openTree()
{
    assert(mountFD_ == -1);
    Path source = Path(getSource());

    const auto fd = open_tree(
        -EBADF,
        source.c_str(),
        OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH | (isRecursive() ? AT_RECURSIVE : 0)
            | (canonSource ? 0 : AT_SYMLINK_NOFOLLOW));
    if (fd == -1) {
        if (!isOptional() || errno != ENOENT)
            throw SysError("open_tree(): failed: '%s'", source);
        return false;
    }

    struct stat stat;
    if (fstat(fd, &stat) == -1)
        throw SysError("fstat(): bind source: %s", source);
    sourceIsDir = S_ISDIR(stat.st_mode);

    if (!sourceIsDir && isRecursive())
        setOptions();

    mountFD_ = fd;
    return true;
};

void BindMountPathImpl::bindMount(const Path & target)
{
    if (!prepared)
        prepare();

    if (!useNewAPI)
        return mountLegacy(target);

    const auto setattr = [this, &target](struct mount_attr attr, bool rec, auto flags) {
        if (attr.attr_set == 0 && attr.attr_clr == 0 && attr.propagation == 0 && attr.userns_fd == 0)
            return;
        if (mount_setattr(this->mountFD_, "", AT_EMPTY_PATH | (rec ? AT_RECURSIVE : 0) | flags, &attr, sizeof(attr))
            == -1)
            throw SysError(
                "mount_setattr failed for '%s' -> '%s' (rec: %s, set: %s, clear: %s, propagation: %s)",
                this->getSource(),
                target,
                rec ? "Y" : "N",
                mountOptsToString(attr.attr_set, rec),
                mountOptsToString(attr.attr_clr, rec),
                mountOptsToString(attr.propagation, rec));
    };

    debug(
        "bind mounting '%s' using '%s' to '%s (optional: %s, recursive: %s)'",
        getSource(),
        mountOptsToString(getOptions()),
        target,
        isOptional() ? "yes" : "no",
        (sourceIsDir && isRecursive()) ? "yes" : "no");

    // Call open_tree() if it wasn't done yet.
    if (mountFD_ == -1 && !openTree())
        return;

    // Create target directory or file
    if (!pathExists(target)) {
        if (sourceIsDir)
            createDirs(target);
        else {
            createDirs(dirOf(target));
            writeFile(target, "");
        }
    }

    // Apply recursive options first.
    if (sourceIsDir && isRecursive()) {
        if (attr.propagation == attrRec.propagation)
            attr.propagation = 0;
        setattr(attrRec, true, 0);
    }
    setattr(attr, false, 0);

    if (move_mount(
            mountFD_,
            "",
            -EBADF,
            target.c_str(),
            MOVE_MOUNT_F_EMPTY_PATH | (canonSource ? MOVE_MOUNT_F_SYMLINKS : 0)
                | (canonTarget ? MOVE_MOUNT_T_SYMLINKS : 0))
        == -1)
        throw SysError("move_mount failed for %s", target);

    close(mountFD_);
    this->mountFD_ = -1;
};

void BindMountPathImpl::mountLegacy(const Path & target) const
{
    debug("bind mounting '%1%' to '%2%'", getSource(), target);

    auto bindMount = [&]() {
        if (mount(getSource().c_str(), target.c_str(), "", MS_BIND | (isRecursive() ? MS_REC : 0), 0) == -1)
            throw SysError("bind mount from '%1%' to '%2%' failed", getSource(), target);

        // Set extra options if some are wanted. In order to do be able to do
        // this, we need to call mount(2) again with MS_REMOUNT, MS_BIND, the
        // wanted options as well as the options that were enabled in the
        // initial mount (inherited from the source path).
        auto curFlags = getMountOpts(target);
        auto setFlags = combineMountOpts(curFlags, getOptions());
        if (setFlags != curFlags) {
            if (mount("", target.c_str(), "", MS_BIND | MS_REMOUNT | (isRecursive() ? MS_REC : 0) | setFlags, 0) == -1)
                throw SysError(
                    "mount: remount of '%s' to set options %s failed", target, mountOptsToString(getOptions()));
        }
    };

    auto maybeSt = maybeLstat(getSource());
    if (!maybeSt) {
        if (isOptional())
            return;
        else
            throw SysError("getting attributes of path '%1%'", getSource());
    }
    auto st = *maybeSt;

    if (S_ISDIR(st.st_mode)) {
        createDirs(target);
        bindMount();
    } else if (S_ISLNK(st.st_mode)) {
        // Symlinks can (apparently) not be bind-mounted, so just copy it
        createDirs(dirOf(target));
        copyFile(std::filesystem::path(getSource()), std::filesystem::path(target), false);
    } else {
        createDirs(dirOf(target));
        writeFile(target, "");
        bindMount();
    }
}

}
