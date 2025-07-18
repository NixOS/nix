#include "nix/util/mount.hh"
#include "nix/util/file-system.hh"

#include <sys/vfs.h>
#include <sys/statvfs.h>

namespace nix {

static MountFlags getFileMountFlags(const Path & filepath)
{
    struct statfs buf;
    if (statfs(filepath.c_str(), &buf) == -1)
        throw SysError("statfs: '%s'", filepath);
    // some flags reported by statfs correspond with MS_* flags, others not.
    MountFlags res = buf.f_flags & (ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC | ST_NOATIME | ST_NODIRATIME);
    if (buf.f_flags & ST_RELATIME)
        res |= MS_RELATIME;
    if (buf.f_flags & ST_NOSYMFOLLOW)
        res |= MS_NOSYMFOLLOW;
    return res;
}

/* Two options that have the same key are mutually exclusive! */
static MountFlags mountOptKey(const MountOpt o)
{
    const auto key = static_cast<MountFlags>(o) & ~MS_REV;
    const auto rec = key & MS_REC;
    if (key & MOUNTOPTS_ATIME_MASK)
        return MS_NOATIME | rec;
    else if (key & MOUNTOPTS_PROPAGATION_MASK)
        return MS_PRIVATE | rec;
    else
        return key;
}

std::vector<MountOpt> optsFromFlags(const MountFlags flags)
{
    std::vector<MountOpt> result;
    auto rec = (flags & MS_REC) ? true : false;
    auto rev = (flags & MS_REV) ? true : false;
    auto fl = flags & ~(MS_REC | MS_REV);
    for (const auto [fs, fs_attr] : mountAttrFlags)
        if (fl & fs_attr)
            fl |= fs;
    for (const auto [mo, mf] : mountOptItems) {
        if ((mf & MS_REC) ? !rec : rec)
            continue;
        if ((mf & MS_REV) ? !rev : rev)
            continue;
        if (mf & fl)
            result.push_back(mo);
    }
    return result;
}

MountFlags mergeMountOpts(MountFlags res, const MountOpt opt)
{
    const auto o = static_cast<MountFlags>(opt);
    if (o & MS_SOURCE_NOCANON) // These are not of interest here
        return res;
    else if (o & MS_TARGET_NOCANON)
        return res;
    else if (o & MS_REV) // Reversed option
        return res & ~(o & ~MS_REV);
    else if (o & MOUNTOPTS_ATIME_MASK)
        return (res & ~MOUNTOPTS_ATIME_MASK) | o;
    else if (o & MOUNTOPTS_PROPAGATION_MASK)
        return (res & ~MOUNTOPTS_PROPAGATION_MASK) | o;
    else
        return res | o;
}

MountFlags mergeMountOpts(MountFlags res, const std::vector<MountOpt> & opts)
{
    for (auto it = std::begin(opts); it != std::end(opts); ++it)
        res = mergeMountOpts(res, *it);
    return res;
}

std::vector<MountOpt> compactMountOpts(const std::vector<MountOpt> & opts)
{
    // We loop over the options starting from the end and prepend the current
    // option to the result array when it isn't shadowed by some processed
    // option.
    std::vector<MountOpt> res = {};
    std::set<MountFlags> keys = {0};
    for (auto it = opts.rbegin(); it != opts.rend(); ++it) {
        auto key = mountOptKey(*it);
        if (keys.contains(key))
            continue;
        keys.insert(key);
        // When adding a recursive option, mark the non-recursive key as well.
        // Then, check the options for a non-recursive option which is the
        // same and delete it if found (it is redundant).
        if (key & MS_REC) {
            keys.insert(key & ~MS_REC);
            for (auto it2 = res.begin(); it2 != res.end(); ++it2)
                if (static_cast<MountFlags>(*it2) == (static_cast<MountFlags>(*it) & ~MS_REC)) {
                    res.erase(it2);
                    break;
                }
        }
        res.insert(res.begin(), *it);
    }
    return res;
}

MountOpts::MountOpts(std::vector<MountOpt> && opts, const bool rec)
    : opts(std::move(opts))
    , rec_(rec)
{
    opts = compactMountOpts(opts);
    update();
};
MountOpts::MountOpts(const MountFlags flags)
    : MountOpts(optsFromFlags(flags)) {};
MountOpts::MountOpts(const Path & filepath)
    : MountOpts(getFileMountFlags(filepath)) {};

bool operator==(const MountOpts & a, const MountOpts & b)
{
    auto cmp_attr = [](auto x, auto y) {
        return x.propagation == y.propagation && x.attr_set == y.attr_set && x.attr_clr == y.attr_clr
               && x.userns_fd == y.userns_fd;
    };
    return a.canonSource() == b.canonSource() && a.canonTarget() == b.canonTarget()
           && cmp_attr(a.getMountAttr(false), b.getMountAttr(false))
           && cmp_attr(a.getMountAttr(true), b.getMountAttr(true));
};

std::vector<MountOpt> MountOpts::getOpts() const
{
    return opts;
};

bool MountOpts::canonSource(bool def) const
{
    return canonSource_ ? *canonSource_ : def;
};

bool MountOpts::canonTarget(bool def) const
{
    return canonTarget_ ? *canonTarget_ : def;
};

MountFlags MountOpts::getFlags() const
{
    return mergeMountOpts(0, opts);
};

mount_attr MountOpts::getMountAttr(bool rec) const
{
    // avoid setting propagation both recursive and non-recursive if they're
    // the same.
    if (attrRec_.propagation == attr_.propagation)
        attr_.propagation = 0;

    return rec ? attrRec_ : attr_;
};

std::string MountOpts::to_string() const
{
    std::vector<MountOpt> os = getOpts();
    auto result = os | std::views::transform(mountOptToString);
    return concatStringsSep(",", std::vector(result.begin(), result.end()));
};

void MountOpts::append(const std::vector<MountOpt> & newOpts)
{
    opts.insert(opts.end(), newOpts.begin(), newOpts.end());
    opts = compactMountOpts(opts);
    update();
};

void MountOpts::update(std::optional<bool> rec)
{
    if (rec && rec_ != *rec) {
        rec_ = *rec;
        attr_ = {};
        attrRec_ = {};
    }
    for (auto opt : opts)
        setOption(opt);
};

void MountOpts::setOption(MountOpt opt)
{
    const auto flags = static_cast<MountFlags>(opt);
    auto rev = (flags & MS_REV) ? true : false;
    auto rec = (flags & MS_REC) ? rec_ : false;

    auto at = rec ? &attrRec_ : &attr_;

    if (flags & MS_SOURCE_NOCANON)
        canonSource_ = std::optional(!rev);
    else if (flags & MS_TARGET_NOCANON)
        canonTarget_ = std::optional(!rev);
    else if (flags & MOUNTOPTS_PROPAGATION_MASK) {
        auto pfl = flags & MOUNTOPTS_PROPAGATION_MASK;
        at->propagation = pfl;
    } else {
        auto setAttr = 0;
        for (const auto [ms_f, sa_f] : mountAttrFlags)
            if (ms_f & flags)
                setAttr |= sa_f;
        if (!setAttr) {
            return;
        } else if (rev) {
            at->attr_clr |= setAttr;
            at->attr_set &= ~setAttr;
        } else if (setAttr & MOUNTOPTS_ATIME_MASK) {
            at->attr_clr |= MOUNT_ATTR__ATIME;
            at->attr_set &= ~MOUNTOPTS_ATIME_MASK;
            at->attr_set |= setAttr;
        } else {
            at->attr_clr &= ~setAttr;
            at->attr_set |= setAttr;
        }
    }
};

void BindMountPathImpl::prepare()
{
    mountOpts_ = MountOpts(getOptions(), isRecursive());
    auto attr = mountOpts_.getMountAttr(false);
    auto attrRec = mountOpts_.getMountAttr(true);
    auto prfl = (attr.propagation | attrRec.propagation) & (MS_SHARED | MS_SLAVE);
    useNewAPI = useNewAPI || prfl || !mountOpts_.canonSource() || mountOpts_.canonTarget() || attrRec.attr_set
                || attrRec.attr_clr;

    // Need to open source early?
    if (useNewAPI && prfl)
        openTree();

    prepared = true;
}

bool BindMountPathImpl::openTree()
{
    assert(mountFD_ == -1);
    Path source = Path(getSource());
    const auto otFlags = OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH | (isRecursive() ? AT_RECURSIVE : 0)
                         | (mountOpts_.canonSource() ? 0 : AT_SYMLINK_NOFOLLOW);

    const int fd = open_tree(-EBADF, source.c_str(), otFlags);
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
        mountOpts_.update(false);
    mountFD_ = fd;
    return true;
};

void BindMountPathImpl::bindMount(const Path & target)
{
    if (!prepared)
        prepare();
    if (!useNewAPI)
        return mountLegacy(target);

    const auto setattr = [this, &target](struct mount_attr attr, bool rec) {
        if (attr.attr_set == 0 && attr.attr_clr == 0 && attr.propagation == 0 && attr.userns_fd == 0)
            return;
        const auto msFlags = AT_EMPTY_PATH | (rec ? AT_RECURSIVE : 0);
        if (mount_setattr(mountFD_, "", msFlags, &attr, sizeof(attr)) == -1)
            throw SysError(
                "mount_setattr failed for '%s' -> '%s' (rec: %s, set: %s, clear: %s, propagation: %s)",
                getSource(),
                target,
                rec ? "Y" : "N",
                MountOpts(attr.attr_set).to_string(),
                MountOpts(attr.attr_clr).to_string(),
                MountOpts(attr.propagation).to_string());
    };

    debug(
        "bind mounting '%s' using '%s' to '%s (optional: %s, recursive: %s)'",
        getSource(),
        mountOpts_.to_string(),
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
        auto attr = mountOpts_.getMountAttr(true);
        setattr(attr, true);
    }
    auto attr = mountOpts_.getMountAttr(false);
    setattr(attr, false);

    auto mmflags = MOVE_MOUNT_F_EMPTY_PATH | (mountOpts_.canonSource() ? MOVE_MOUNT_F_SYMLINKS : 0)
                   | (mountOpts_.canonTarget() ? MOVE_MOUNT_T_SYMLINKS : 0);

    if (move_mount(mountFD_, "", -EBADF, target.c_str(), mmflags) == -1)
        throw SysError("move_mount failed: '%s'", target);

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
        auto mopts = MountOpts(target);
        const auto curOpts = mopts;
        mopts.append(getOptions());
        if (mopts != curOpts) {
            if (mount("", target.c_str(), "", MS_BIND | MS_REMOUNT | (isRecursive() ? MS_REC : 0) | mopts.getFlags(), 0)
                == -1)
                throw SysError("mount: remount of '%s' to set options %s failed", target, mountOpts_.to_string());
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
