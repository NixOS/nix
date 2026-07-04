#include "nix/cmd/legacy.hh"
#include "nix/store/macho-signature.hh"
#include "nix/util/error.hh"
#include "nix/util/exit.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

/**
 * `nix __fixup-macho [--check] <path>...` — the default payload of
 * the `macho-signature-repair-hook` setting.
 *
 * Recomputes Mach-O `LC_CODE_SIGNATURE` page hashes for any pages
 * whose stored hash disagrees with the on-disk contents — the state a
 * store path hash rewrite leaves a signed binary in. Only the stale
 * hash slots are rewritten; every other byte, including the
 * `linker-signed` flag and the original page size, is preserved, so
 * the same input bytes always yield the same output bytes
 * (deterministic, as `--check` and content-addressing require —
 * a `codesign` re-sign cannot provide that).
 *
 * With `--check`, nothing is written; the exit status is 2 when at
 * least one file has stale page hashes or a signature that cannot be
 * verified, 0 when all signatures are valid.
 *
 * The daemon execs this as the build user wherever untrusted bytes
 * must be parsed for repair (the `diff-hook` privilege pattern);
 * this process is the privilege boundary. The parsing and hashing
 * engine is `fixupMachOSignature` in libstore, shared with the
 * daemon's read-only detector so both are covered by one test suite.
 */

namespace nix {

namespace {

/* Smallest file that could be a thin Mach-O (sizeof(mach_header)).
   The upper bound `maxMachOFileSize` is shared with the daemon-side
   scan via the header. */
constexpr size_t minFileSize = 28;

/**
 * Process one regular file. Returns 1 if it was (or, in check mode,
 * would be) modified.
 */
size_t fixupFile(const std::filesystem::path & path, bool checkOnly)
{
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(st))
        return 0;

    auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < minFileSize)
        return 0;

    /* Peek the magic before loading the file — most files in a store
       path are not Mach-O. `readOffset` uses `pread`, leaving the
       descriptor's offset at 0 for the full `readFile(fd)` below. */
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        return 0;
    std::array<std::byte, 4> peek;
    if (readOffset(fd.get(), 0, peek) != peek.size())
        return 0;
    if (!hasMachOMagic(reinterpret_cast<const unsigned char *>(peek.data())))
        return 0;

    /* The size gate applies only to Mach-O files (checked after the
       magic peek, so an ordinary large file — libtorch weights, a
       dataset — is ignored, not treated as an error). A Mach-O this
       large is left as-is with a warning rather than a throw, which
       would abort a whole-path run over its other files. In check
       mode it counts as a failure: the file may carry a signature
       nothing has looked at, and exit 0 promises all signatures are
       valid — the same fail-closed reading the daemon-side scan
       gives such files (`Unchecked`). */
    if (sz > maxMachOFileSize) {
        warn("%s is too large to inspect (limit %d MiB); skipping", PathFmt(path), 512);
        return checkOnly ? 1 : 0;
    }

    std::string data = readFile(fd.get());

    if (!fixupMachOSignature(data, path, checkOnly))
        return 0;
    if (checkOnly)
        return 1;

    /* Length-preserving in-place write. The daemon canonicalises
       permissions right after the hook, so 0600 is a safe transient. */
    writeFile(path, std::string_view{data}, 0600);
    return 1;
}

/**
 * Process a path: a regular file directly, a directory recursively
 * (symlinks never followed).
 */
size_t fixupPath(const std::filesystem::path & path, bool checkOnly)
{
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(st))
        return 0;

    if (std::filesystem::is_regular_file(st))
        return fixupFile(path, checkOnly);

    if (!std::filesystem::is_directory(st))
        return 0;

    size_t count = 0;
    auto it = std::filesystem::recursive_directory_iterator(
        path, std::filesystem::directory_options::skip_permission_denied, ec);
    auto end = std::filesystem::recursive_directory_iterator();
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code sec;
        auto est = it->symlink_status(sec);
        if (sec || std::filesystem::is_symlink(est))
            continue;
        if (std::filesystem::is_regular_file(est))
            count += fixupFile(it->path(), checkOnly);
    }
    return count;
}

int main_fixup_macho(int argc, char ** argv)
{
    bool checkOnly = false;
    int argi = 1;
    if (argi < argc && std::string_view(argv[argi]) == "--check") {
        checkOnly = true;
        argi++;
    }
    if (argi >= argc)
        throw UsageError("usage: nix __fixup-macho [--check] <path>...");

    size_t fixed = 0;
    for (int i = argi; i < argc; i++)
        fixed += fixupPath(std::filesystem::path(argv[i]), checkOnly);

    /* This tool runs as the check/repair child of every door, so a
       clean result stays quiet — anything printed here lands in the
       daemon log of every verified substitution. */
    if (checkOnly) {
        /* Exit 0 = all signatures valid; 2 = at least one file has
           stale page hashes or a signature that cannot be verified.
           (1 is the generic error exit.) */
        if (fixed) {
            printError("fixup-macho: %d file(s) with stale or unverifiable signatures", fixed);
            throw Exit(2);
        }
        return 0;
    }

    if (fixed)
        printError("fixup-macho: rewrote %d file(s)", fixed);
    return 0;
}

} // namespace

static RegisterLegacyCommand r_fixup_macho("fixup-macho", main_fixup_macho);

} // namespace nix
