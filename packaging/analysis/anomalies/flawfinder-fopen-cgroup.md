# Anomaly: flawfinder-fopen-cgroup

## Finding
- **Tool**: flawfinder
- **Check**: misc.fopen
- **File**: src/libutil/linux/cgroup.cc:21
- **Severity**: style (CWE-362)
- **Status**: needs-review

## Code
```cpp
std::optional<std::filesystem::path> getCgroupFS()
{
    static auto res = [&]() -> std::optional<std::filesystem::path> {
        auto fp = fopen("/proc/mounts", "r");
        if (!fp)
            return std::nullopt;
        Finally delFP = [&]() { fclose(fp); };
        while (auto ent = getmntent(fp))
            if (std::string_view(ent->mnt_type) == "cgroup2")
                return ent->mnt_dir;
        return std::nullopt;
    }();
    return res;
}
```

## Analysis
Flawfinder flags `fopen()` for potential TOCTOU (time-of-check-time-of-use)
race conditions. In this case:

1. The file is `/proc/mounts`, a kernel pseudo-filesystem that can't be
   symlink-attacked in the traditional sense.
2. The result is cached in a `static` variable, so the file is only read once.
3. The `Finally` RAII guard ensures `fclose()` is called on all paths.

The real concern is minor: mount state could change between reading and using
the result, but this is inherent to any procfs access and acceptable here.

## Proposed Remediations

### Option A: Use std::ifstream
Replace C-style `fopen`/`fclose` with C++ RAII streams. However, `getmntent()`
requires a `FILE*`, so this would require parsing `/proc/mounts` manually.

### Option B: Accept as inherent limitation
Document that procfs reads are inherently racy and the static caching is intentional.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: needs-review — low risk, procfs TOCTOU is inherent
