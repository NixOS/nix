#include "nix/cmd/command.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-cast.hh"
#include "nix/util/archive.hh"
#include "nix/util/file-content-address.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/processes.hh"
#include "nix/util/source-accessor.hh"

#ifndef _WIN32
#  include "nix/store/macho-signature.hh"
#endif

#include "self-exe.hh"

namespace nix {

struct CmdStoreFixupMachO : StorePathsCommand
{
    bool dryRun = false;

    CmdStoreFixupMachO()
    {
        addFlag({
            .longName = "dry-run",
            .description = "Only report paths that would be repaired; do not modify anything.",
            .handler = {&dryRun, true},
        });
    }

    std::string description() override
    {
        return "repair invalid Mach-O code signatures in store paths";
    }

    std::string doc() override
    {
        return
#include "store-fixup-macho.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override;
};

#ifdef _WIN32

void CmdStoreFixupMachO::run(ref<Store> store, StorePaths && storePaths)
{
    throw UsageError("'nix store fixup-macho' is not supported on this platform");
}

#else

void CmdStoreFixupMachO::run(ref<Store> store, StorePaths && storePaths)
{
    auto & localStore = require<LocalStore>(*store);

    auto fixupTool = getNixBin({}).string();

    size_t repaired = 0, stale = 0, failed = 0;

    /* One unrepairable path (a CMS-signed binary, a permission
       failure) must not abort the rest of the sweep. */
    auto processPath = [&](const StorePath & path) {
        auto info = localStore.queryPathInfo(path);
        auto realPath = localStore.toRealPath(path);

        /* A content-addressed path cannot be repaired: its `ca` field
           would no longer match the contents. */
        if (info->ca) {
            debug("skipping content-addressed path '%s'", localStore.printStorePath(path));
            return;
        }

        /* Find signed Mach-O files, then verify their page hashes via
           the fixup tool. A path carrying a CMS signature is skipped
           whole with a warning: only the original signing identity
           can regenerate it. */
        auto hits = scanForMachOSignatures(realPath);
        if (hits.empty())
            return;
        for (auto & hit : hits)
            if (hit.kind != MachOSignatureKind::AdHoc) {
                warn(
                    "not repairing '%s': %s %s",
                    localStore.printStorePath(path),
                    PathFmt(hit.path),
                    hit.kind == MachOSignatureKind::Cms
                        ? "carries a CMS signature (Developer ID), which only the original signing identity can regenerate"
                        : "is too large to inspect");
                return;
            }

        {
            auto status = runProgram(
                              RunOptions{
                                  .program = fixupTool,
                                  .args = {OS_STR("__fixup-macho"), OS_STR("--check"), realPath.native()},
                              })
                              .first;
            switch (classifyMachOCheck(status)) {
            case MachOCheckOutcome::Valid:
                /* Nothing to do. A NAR-hash mismatch against valid
                   signatures is deliberately not "healed" by
                   re-registering the on-disk content: valid signatures
                   don't distinguish this tool's own interrupted swap
                   from unrelated corruption, so re-registering would
                   hide that corruption from `nix store verify`. Such a
                   path is recovered with `nix store verify --repair`. */
                return;
            case MachOCheckOutcome::Stale:
                break;
            case MachOCheckOutcome::Error:
                throw Error(
                    "verifying Mach-O signatures of '%s': %s", localStore.printStorePath(path), statusToString(status));
            }
        }

        stale++;
        if (dryRun) {
            printInfo("would repair '%s'", localStore.printStorePath(path));
            return;
        }

        /* Never repair in place: `auto-optimise-store` hard-links
           identical files across store paths, so an in-place write
           would corrupt every sharing path. Copy the whole path,
           repair the copy, swap it in, and update the database. */
        auto tempDir = createTempDir();
        AutoDelete delTempDir(tempDir);
        auto tempPath = std::filesystem::path(tempDir) / "x";

        {
            auto accessor = makeFSSourceAccessor(realPath);
            RestoreSink sink{false};
            sink.dstPath = tempPath;
            copyRecursive(*accessor, CanonPath::root, sink, CanonPath::root);
        }

        {
            auto status = runProgram(
                              RunOptions{
                                  .program = fixupTool,
                                  .args = {OS_STR("__fixup-macho"), tempPath.native()},
                              })
                              .first;
            if (!statusOk(status))
                throw Error(
                    "repairing Mach-O signatures of '%s': %s", localStore.printStorePath(path), statusToString(status));
        }

        /* The tool skips what it cannot process (an unsupported hash
           type, a malformed CodeDirectory), exiting successfully.
           Swapping in a copy that still fails the check would report
           the path repaired when it is not — verify before swapping. */
        {
            auto status = runProgram(
                              RunOptions{
                                  .program = fixupTool,
                                  .args = {OS_STR("__fixup-macho"), OS_STR("--check"), tempPath.native()},
                              })
                              .first;
            if (classifyMachOCheck(status) != MachOCheckOutcome::Valid) {
                warn(
                    "not repairing '%s': its signatures are still invalid after repair "
                    "(a signature the repair tool cannot process, such as an unsupported hash type)",
                    localStore.printStorePath(path));
                return;
            }
        }

        auto narHashAndSize = hashPath(
            {makeFSSourceAccessor(tempPath), CanonPath::root},
            FileSerialisationMethod::NixArchive,
            HashAlgorithm::SHA256);

        ValidPathInfo newInfo{*info};
        newInfo.narHash = narHashAndSize.hash;
        newInfo.narSize = narHashAndSize.numBytesDigested;
        /* The signatures signed the old contents. */
        newInfo.sigs.clear();

        localStore.replaceStorePath(path, tempPath, newInfo);

        printInfo("repaired '%s'", localStore.printStorePath(path));
        repaired++;
    };

    for (auto & path : storePaths) {
        try {
            processPath(path);
        } catch (Error & e) {
            failed++;
            logError(e.info());
        } catch (std::exception & e) {
            /* A filesystem or allocation error on one path must not
               abort the rest of the sweep either. */
            failed++;
            printError("error processing '%s': %s", localStore.printStorePath(path), e.what());
        }
    }

    if (dryRun)
        printInfo("%d path(s) with invalid Mach-O signatures", stale);
    else
        printInfo("repaired %d path(s)", repaired);
    if (failed)
        throw Error("failed to process %d path(s)", failed);
}

#endif

static auto rStoreFixupMachO = registerCommand2<CmdStoreFixupMachO>({"store", "fixup-macho"});

} // namespace nix
