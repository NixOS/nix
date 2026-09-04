#include "build/derivation-builder-impl.hh"
#include "build/derivation-check.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-store.hh"
#include "nix/store/path-references.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/archive.hh"
#include "nix/util/file-content-address.hh"
#include "nix/util/file-system.hh"
#include "nix/util/git.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/topo-sort.hh"

#include "nix/util/unix-domain-socket.hh"

#ifdef _WIN32
#  include <winsock2.h>
#  include <afunix.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#endif

namespace nix {

void DerivationBuilderImpl::anchor() {}

class NotDeterministic final : public CloneableError<NotDeterministic, BuildError>
{
    void anchor() override;

public:
    NotDeterministic(auto &&... args)
        : CloneableError(BuildResult::Failure::NotDeterministic, args...)
    {
        isNonDeterministic = true;
    }
};

void NotDeterministic::anchor() {}

static void handleDiffHook(
    const std::filesystem::path & diffHook,
#ifndef _WIN32
    uid_t uid,
    uid_t gid,
#endif
    const std::filesystem::path & tryA,
    const std::filesystem::path & tryB,
    const std::filesystem::path & drvPath,
    const std::filesystem::path & tmpDir)
{
    try {
        auto diffRes = runProgram(
            RunOptions{
                .program = diffHook,
                .lookupPath = true,
                .args = {tryA, tryB, drvPath, tmpDir},
#ifndef _WIN32
                .uid = uid,
                .gid = gid,
#endif
                .chdir = "/"});
        if (!statusOk(diffRes.first))
            throw ExecError(diffRes.first, "diff-hook program %s %s", PathFmt(diffHook), statusToString(diffRes.first));

        if (diffRes.second != "")
            printError(chomp(diffRes.second));
    } catch (Error & error) {
        ErrorInfo ei = error.info();
        // FIXME: wrap errors.
        ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
        logError(ei);
    }
}

static void replaceValidPath(const std::filesystem::path & storePath, const std::filesystem::path & tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    std::filesystem::path oldPath;

    if (pathExists(storePath)) {
        // why do we loop here?
        // although makeTempPath should be unique, we can't
        // guarantee that.
        do {
            oldPath = makeTempPath(storePath, ".old");
            // store paths are often directories so we can't just unlink() it
            // let's make sure the path doesn't exist before we try to use it
        } while (pathExists(oldPath));
        movePath(storePath, oldPath);
    }
    try {
        movePath(tmpPath, storePath);
    } catch (...) {
        try {
            // attempt to recover
            if (!oldPath.empty())
                movePath(oldPath, storePath);
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
        throw;
    }
    if (!oldPath.empty())
        deletePath(oldPath);
}

SingleDrvOutputs DerivationBuilderImpl::registerOutputs()
{
    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    /* The paths that can be referenced are the input closures, the
       output paths, and any paths that have been built via recursive
       Nix calls. */
    StorePathSet referenceablePaths;
    for (auto & p : inputPaths)
        referenceablePaths.insert(p);
    for (auto & i : scratchOutputs)
        referenceablePaths.insert(i.second);
    for (auto & [p, _] : state_.lock()->addedPaths)
        referenceablePaths.insert(p);

    /* Check whether the output paths were created, and make all
       output paths read-only.  Then get the references of each output (that we
       might need to register), so we can topologically sort them. For the ones
       that are most definitely already installed, we just store their final
       name so we can also use it in rewrites. */
    StringSet outputsToSort;

    struct AlreadyRegistered
    {
        StorePath path;
    };

    struct PerhapsNeedToRegister
    {
        StorePathSet refs;
        /**
         * References to other outputs. Built by looking up in
         * `scratchOutputsInverse`.
         */
        StringSet otherOutputs;
    };

    /* inverse map of scratchOutputs for efficient lookup */
    std::map<StorePath, std::string> scratchOutputsInverse;
    for (auto & [outputName, path] : scratchOutputs)
        scratchOutputsInverse.insert_or_assign(path, outputName);

    std::map<std::string, std::variant<AlreadyRegistered, PerhapsNeedToRegister>> outputReferencesIfUnregistered;
    std::map<std::string, PosixStat> outputStats;
    for (auto & [outputName, _] : drv.outputs) {
        auto scratchOutput = get(scratchOutputs, outputName);
        assert(scratchOutput);
        auto actualPath = realPathInHost(store.printStorePath(*scratchOutput));

        outputsToSort.insert(outputName);

        /* Updated wanted info to remove the outputs we definitely don't need to register */
        auto initialOutput = get(initialOutputs, outputName);
        assert(initialOutput);
        auto & initialInfo = *initialOutput;

        /* Don't register if already valid, and not checking */
        bool wanted = buildMode == bmCheck || !(initialInfo.known && initialInfo.known->isValid());
        if (!wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName, AlreadyRegistered{.path = initialInfo.known->path});
            continue;
        }

        auto optSt = maybeLstat(actualPath);
        if (!optSt)
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to produce output path for output '%s' at %s",
                store.printStorePath(drvPath),
                outputName,
                PathFmt(actualPath));
        PosixStat & st = *optSt;

#if !defined(__CYGWIN__) && !defined(_WIN32)
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH)))
            || (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "suspicious ownership or permission on %s for output '%s'; rejecting this build output",
                PathFmt(actualPath),
                outputName);
#endif

        /* Canonicalise first.  This ensures that the path we're
           rewriting doesn't contain a hard link to /etc/shadow or
           something like that. */
        canonicalisePathMetaData(
            actualPath,
            {
#ifndef _WIN32
                .uidRange = buildUser ? std::optional(buildUser->getUIDRange()) : std::nullopt,
#endif
                NIX_WHEN_SUPPORT_ACLS(localSettings.ignoredAcls)},
            inodesSeen);

        bool discardReferences = false;
        if (auto udr = get(drvOptions.unsafeDiscardReferences, outputName)) {
            discardReferences = *udr;
        }

        StorePathSet references;
        if (discardReferences)
            debug("discarding references of output '%s'", outputName);
        else {
            debug("scanning for references for output '%s' in temp location %s", outputName, PathFmt(actualPath));

            /* Pass blank Sink as we are not ready to hash data at this stage. */
            NullSink blank;
            references = scanForReferences(blank, actualPath, referenceablePaths);
        }

        StringSet referencedOutputs;
        for (auto & r : references)
            if (auto * o = get(scratchOutputsInverse, r))
                referencedOutputs.insert(*o);

        outputReferencesIfUnregistered.insert_or_assign(
            outputName,
            PerhapsNeedToRegister{
                .refs = references,
                .otherOutputs = referencedOutputs,
            });
        outputStats.insert_or_assign(outputName, std::move(st));
    }

    StringSet emptySet;

    auto topoSortResult = topoSort(outputsToSort, [&](const std::string & name) -> const StringSet & {
        auto * orifu = get(outputReferencesIfUnregistered, name);
        if (!orifu)
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "no output reference for '%s' in build of '%s'",
                name,
                store.printStorePath(drvPath));
        return std::visit(
            overloaded{
                /* Since we'll use the already installed versions of these, we
                   can treat them as leaves and ignore any references they
                   have. */
                [&](const AlreadyRegistered &) -> const StringSet & { return emptySet; },
                [&](const PerhapsNeedToRegister & refs) -> const StringSet & { return refs.otherOutputs; },
            },
            *orifu);
    });

    auto sortedOutputNames = std::visit(
        overloaded{
            [&](Cycle<std::string> & cycle) -> std::vector<std::string> {
                // TODO with more -vvvv also show the temporary paths for manual inspection.
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                    store.printStorePath(drvPath),
                    cycle.path,
                    cycle.parent);
            },
            [](auto & sorted) { return sorted; }},
        topoSortResult);

    OutputPathMap finalOutputs;

    for (auto & outputName : sortedOutputNames | std::views::reverse) {
        auto output = get(drv.outputs, outputName);
        auto scratchPath = get(scratchOutputs, outputName);
        assert(output && scratchPath);
        auto actualPath = realPathInHost(store.printStorePath(*scratchPath));

        /* An optional file descriptor of a directory used for intermediate
           operations. */
        AutoCloseFD tempDirFd;
        /* RAII cleanup of a temporary directory inside the store that is used
           for intermediate operations. */
        AutoDelete delTempDir;

        auto finish = [&](StorePath finalStorePath) {
            /* Store the final path */
            finalOutputs.insert_or_assign(outputName, finalStorePath);
            /* The rewrite rule will be used in downstream outputs that refer to
               use. This is why the topological sort is essential to do first
               before this for loop. */
            if (*scratchPath != finalStorePath)
                outputRewrites[std::string{scratchPath->hashPart()}] = std::string{finalStorePath.hashPart()};
        };

        auto orifu = get(outputReferencesIfUnregistered, outputName);
        assert(orifu);

        std::optional<StorePathSet> referencesOpt = std::visit(
            overloaded{
                [&](const AlreadyRegistered & skippedFinalPath) -> std::optional<StorePathSet> {
                    finish(skippedFinalPath.path);
                    return std::nullopt;
                },
                [&](const PerhapsNeedToRegister & r) -> std::optional<StorePathSet> { return r.refs; },
            },
            *orifu);

        if (!referencesOpt)
            continue;
        auto references = *referencesOpt;

        auto rewriteOutput = [&](const StringMap & rewrites) {
            /* Apply hash rewriting if necessary. */
            if (!rewrites.empty()) {
                debug("rewriting hashes in %1%; cross fingers", PathFmt(actualPath));

                /* FIXME: Is this actually streaming? */
                auto source = sinkToSource([&](Sink & nextSink) {
                    RewritingSink rsink(rewrites, nextSink);
                    dumpPath(actualPath, rsink);
                    rsink.flush();
                });
                /* Put the temporary copy in a directory inaccessible to the builder.
                   actualPath might point inside the build chroot, which is controlled
                   by the derivation builder. */
                auto [rewriteTempDir, rewriteTempDirFd] = store.createTempDirInStore();
                AutoDelete delRewriteTempDir(rewriteTempDir);
                std::filesystem::path tmpPath = rewriteTempDir / "x";
                restorePath(tmpPath, *source);
                deletePath(actualPath);
                movePath(tmpPath, actualPath);

                /* FIXME: set proper permissions in restorePath() so
                   we don't have to do another traversal. */
                canonicalisePathMetaData(
                    actualPath,
                    {
#ifndef _WIN32
                        // builder UIDs are already dealt with
                        .uidRange = std::nullopt,
#endif
                        NIX_WHEN_SUPPORT_ACLS(localSettings.ignoredAcls)},
                    inodesSeen);
            }
        };

        auto rewriteRefs = [&]() -> StoreReferences {
            /* In the CA case, we need the rewritten refs to calculate the
               final path, therefore we look for a *non-rewritten
               self-reference, and use a bool rather try to solve the
               computationally intractable fixed point. */
            StoreReferences res{
                .self = false,
            };
            for (auto & r : references) {
                auto name = r.name();
                auto origHash = std::string{r.hashPart()};
                if (r == *scratchPath) {
                    res.self = true;
                } else if (auto outputRewrite = get(outputRewrites, origHash)) {
                    std::string newRef = *outputRewrite;
                    newRef += '-';
                    newRef += name;
                    res.others.insert(StorePath{newRef});
                } else {
                    res.others.insert(r);
                }
            }
            return res;
        };

        auto newInfoFromCA = [&](const DerivationOutput::CAFloating outputHash) -> ValidPathInfo {
            auto st = get(outputStats, outputName);
            if (!st)
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "output path %1% without valid stats info",
                    PathFmt(actualPath));
            if (outputHash.method.getFileIngestionMethod() == FileIngestionMethod::Flat) {
                /* The output path should be a regular file without execute permission. */
                if (!S_ISREG(st->st_mode) || (st->st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "output path %1% should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        PathFmt(actualPath));
            }
            rewriteOutput(outputRewrites);
            /* FIXME optimize and deduplicate with addToStore */
            std::string oldHashPart{scratchPath->hashPart()};
            auto got = [&] {
                auto fim = outputHash.method.getFileIngestionMethod();
                switch (fim) {
                case FileIngestionMethod::Flat:
                case FileIngestionMethod::NixArchive: {
                    HashModuloSink caSink{outputHash.hashAlgo, oldHashPart};
                    auto fim = outputHash.method.getFileIngestionMethod();
                    dumpPath(
                        {makeFSSourceAccessor(actualPath), CanonPath::root}, caSink, (FileSerialisationMethod) fim);
                    return caSink.finish().hash;
                }
                case FileIngestionMethod::Git: {
                    return git::dumpHash(outputHash.hashAlgo, {makeFSSourceAccessor(actualPath), CanonPath::root}).hash;
                }
                }
                assert(false);
            }();

            auto newInfo0 = ValidPathInfo::makeFromCA(
                store,
                outputPathName(drv.name, outputName),
                ContentAddressWithReferences::fromParts(outputHash.method, std::move(got), rewriteRefs()),
                Hash::dummy);
            if (*scratchPath != newInfo0.path) {
                // If the path has some self-references, we need to rewrite
                // them.
                // (note that this doesn't invalidate the ca hash we calculated
                // above because it's computed *modulo the self-references*, so
                // it already takes this rewrite into account).
                rewriteOutput(StringMap{{oldHashPart, std::string(newInfo0.path.hashPart())}});
            }

            {
                HashResult narHashAndSize = hashPath(
                    {makeFSSourceAccessor(actualPath), CanonPath::root},
                    FileSerialisationMethod::NixArchive,
                    HashAlgorithm::SHA256);
                newInfo0.narHash = narHashAndSize.hash;
                newInfo0.narSize = narHashAndSize.numBytesDigested;
            }

            assert(newInfo0.ca);
            return newInfo0;
        };

        auto moveOutputToTempDir = [&]() -> void {
            std::filesystem::path tempDir;
            std::tie(tempDir, tempDirFd) = store.createTempDirInStore();
            delTempDir = AutoDelete(tempDir);

            auto tmpOutput = tempDir / "x";

            /* Copy files to break stale file descriptors. copyRecursive below will use
               reflinking to optimise the copying overhead. */
            auto pathAccessor = makeFSSourceAccessor(actualPath);
            RestoreSink restoreSink{store.config->getLocalSettings().fsyncStorePaths};
            restoreSink.dstPath = tmpOutput;
            copyRecursive(*pathAccessor, CanonPath::root, restoreSink, CanonPath::root);
            /* This makes it slightly harder to make sense of the control flow. The rule
               of thumb is that actualPath points to the current location of the stuff
               that we'll end up registering. */
            actualPath = std::move(tmpOutput);
        };

        ValidPathInfo newInfo = std::visit(
            overloaded{

                [&](const DerivationOutput::InputAddressed & output) {
                    /* input-addressed case */
                    auto requiredFinalPath = output.path;
                    /* Preemptively add rewrite rule for final hash, as that is
                       what the NAR hash will use rather than normalized-self references */
                    if (*scratchPath != requiredFinalPath)
                        outputRewrites.insert_or_assign(
                            std::string{scratchPath->hashPart()}, std::string{requiredFinalPath.hashPart()});
                    rewriteOutput(outputRewrites);
                    HashResult narHashAndSize = hashPath(
                        {makeFSSourceAccessor(actualPath), CanonPath::root},
                        FileSerialisationMethod::NixArchive,
                        HashAlgorithm::SHA256);
                    ValidPathInfo newInfo0{requiredFinalPath, {store, narHashAndSize.hash}};
                    newInfo0.narSize = narHashAndSize.numBytesDigested;
                    auto refs = rewriteRefs();
                    newInfo0.references = std::move(refs.others);
                    if (refs.self)
                        newInfo0.references.insert(newInfo0.path);
                    return newInfo0;
                },

                [&](const DerivationOutput::CAFixed & dof) {
                    auto & wanted = dof.ca.hash;
                    moveOutputToTempDir();
                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = dof.ca.method,
                            .hashAlgo = wanted.algo,
                        });
                },

                [&](const DerivationOutput::CAFloating & dof) { return newInfoFromCA(dof); },

                [&](const DerivationOutput::Deferred &) -> ValidPathInfo {
                    // No derivation should reach that point without having been
                    // rewritten first
                    assert(false);
                },

                [&](const DerivationOutput::Impure & doi) {
                    moveOutputToTempDir();
                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = doi.method,
                            .hashAlgo = doi.hashAlgo,
                        });
                },

            },
            output->raw);

        /* FIXME: set proper permissions in restorePath() so
            we don't have to do another traversal. */
        canonicalisePathMetaData(
            actualPath,
            {
#ifndef _WIN32
                // builder UIDs are already dealt with
                .uidRange = std::nullopt,
#endif
                NIX_WHEN_SUPPORT_ACLS(localSettings.ignoredAcls)},
            inodesSeen);

        /* Calculate where we'll move the output files. In the checking case we
           will leave leave them where they are, for now, rather than move to
           their usual "final destination" */
        auto finalDestPath = store.printStorePath(newInfo.path);

        /* Lock final output path, if not already locked. This happens with
           floating CA derivations and hash-mismatching fixed-output
           derivations. */
        PathLocks dynamicOutputLock;
        dynamicOutputLock.setDeletion(true);
        auto optFixedPath = output->path(store, drv.name, outputName);
        if (!optFixedPath || store.printStorePath(*optFixedPath) != finalDestPath) {
            assert(newInfo.ca);

            /* Don't wait on lock for the hash-mismatching fixed-output
               derivation case, to avoid a deadlock in the case where a build
               with the correct hash is in progress. */
            bool locked = dynamicOutputLock.lockPaths({store.toRealPath(newInfo.path)}, "", !optFixedPath);

            /* If we can't lock the correct path, clean up and bail now. */
            if (!locked) {
                debug(
                    "failed to lock correct output path of %s, namely %s, not moving output",
                    store.printStorePath(drvPath),
                    PathFmt(store.toRealPath(newInfo.path)));
                deletePath(actualPath);
                /* Trigger the hash-mismatch error. */
                checkCAOutput(store, drvPath, *output, newInfo, outputName);
                unreachable();
            }
        }

        /* Move files, if needed */
        if (store.toRealPath(newInfo.path) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(store.toRealPath(newInfo.path), actualPath);
            } else if (buildMode == bmCheck) {
                /* Path already exists, and we want to compare, so we leave out
                   new path in place. */
            } else if (store.isValidPath(newInfo.path)) {
                /* Path already exists because CA path produced by something
                   else. No moving needed. */
                assert(newInfo.ca);
                /* Can delete our scratch copy now. */
                deletePath(actualPath);
            } else {
                auto destPath = store.toRealPath(newInfo.path);
                deletePath(destPath);
                movePath(actualPath, destPath);
            }
        }

        if (buildMode == bmCheck) {
            /* Check against already registered outputs */

            if (store.isValidPath(newInfo.path)) {
                ValidPathInfo oldInfo(*store.queryPathInfo(newInfo.path));
                if (newInfo.narHash != oldInfo.narHash) {
                    auto * diffHook = localSettings.getDiffHook();
                    if (diffHook || settings.keepFailed) {
                        auto dst = store.toRealPath(newInfo.path);
                        dst += ".check";
                        deletePath(dst);
                        movePath(actualPath, dst);

                        if (diffHook) {
                            handleDiffHook(
                                *diffHook,
#ifndef _WIN32
                                buildUser ? buildUser->getUID() : getuid(),
                                buildUser ? buildUser->getGID() : getgid(),
#endif
                                finalDestPath,
                                dst,
                                store.printStorePath(drvPath),
                                tmpDir);
                        }

                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output %s differs from %s",
                            store.printStorePath(drvPath),
                            PathFmt(store.toRealPath(newInfo.path)),
                            PathFmt(dst));
                    } else
                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output %s differs",
                            store.printStorePath(drvPath),
                            PathFmt(store.toRealPath(newInfo.path)));
                }

                /* Since we verified the build, it's now ultimately trusted. */
                if (!oldInfo.ultimate) {
                    oldInfo.ultimate = true;
                    store.signPathInfo(oldInfo);
                    store.registerValidPaths({{oldInfo.path, oldInfo}});
                }
            }
        } else {
            /* do tasks relating to registering these outputs */

            /* For debugging, print out the referenced and unreferenced paths. */
            for (auto & i : inputPaths) {
                if (references.count(i))
                    debug("referenced input: '%1%'", store.printStorePath(i));
                else
                    debug("unreferenced input: '%1%'", store.printStorePath(i));
            }

            if (!store.isValidPath(newInfo.path))
                store.optimisePath(store.toRealPath(newInfo.path), NoRepair); // FIXME: combine with scanForReferences()

            newInfo.deriver = drvPath;
            newInfo.ultimate = true;
            store.signPathInfo(newInfo);

            finish(newInfo.path);

            /* If it's a CA path, register it right away. This is necessary if it
               isn't statically known so that we can safely unlock the path before
               the next iteration

               This is also good so that if a fixed-output produces the
               wrong path, we still store the result (just don't consider
               the derivation sucessful, so if someone fixes the problem by
               just changing the wanted hash, the redownload (or whateer
               possibly quite slow thing it was) doesn't have to be done
               again. */
            if (newInfo.ca)
                store.registerValidPaths({{newInfo.path, newInfo}});
        }

        /* Do this in both the check and non-check cases, because we
           want `checkOutputs` below to work, which needs these path
           infos. */
        infos.emplace(outputName, std::move(newInfo));
    }

    /* Apply output checks. This includes checking of the wanted vs got
       hash of fixed-outputs. */
    checkOutputs(store, drvPath, drv, drvOptions.outputChecks, infos);

    if (buildMode == bmCheck) {
        return {};
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        store.registerValidPaths(infos2);
    }

    /* If we made it this far, we are sure the output matches the
       derivation That means it's safe to link the derivation to the
       output hash. We must do that for floating CA derivations, which
       otherwise couldn't be cached, but it's fine to do in all cases.
       */
    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, newInfo] : infos) {
        auto oldinfo = get(initialOutputs, outputName);
        assert(oldinfo);
        auto thisRealisation = Realisation{
            {
                .outPath = newInfo.path,
            },
            DrvOutput{
                .drvPath = drvPath,
                .outputName = outputName,
            },
        };
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !type(drv).isImpure()) {
            store.signRealisation(thisRealisation);
            store.registerDrvOutput(thisRealisation, NoCheckSigs);
        }
        builtOutputs.emplace(outputName, thisRealisation);
    }

    return builtOutputs;
}

SingleDrvOutputs DerivationBuilderImpl::checkSubmittedOutputs()
{
    // Submitted outputs from the recursive nix daemon
    // It's fine to lock here since all other threads with the reference have been shut down.
    auto submittedOutputs(this->submittedOutputs.lock());

    SingleDrvOutputs builtOutputs;

    std::map<std::string, ValidPathInfo> infos;

    for (auto & [outputName, outputPath] : *submittedOutputs) {
        infos.emplace(outputName, *store.queryPathInfo(outputPath));
    }

    // checkOutputs only performs checks that make sense for both submitting and non-submitting derivations,
    // more verification steps needed afterward
    checkOutputs(store, drvPath, drv, drvOptions.outputChecks, infos);

    for (auto & [outputName, output] : drv.outputs) {
        // For some reason cannot be moved to checkOutputs, needs debugging
        if (!submittedOutputs->contains(outputName)) {
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to submit output path for '%s'",
                store.printStorePath(drvPath),
                outputName);
        }

        // We should have already checked that the derivation is content-addressing in startBuild
        // and that the outputs of a content-addressing derivation is content-addressed in checkOutputs.
        // Add an assert here just in case, but it should never trigger.
        assert(
            std::get_if<DerivationOutput::CAFloating>(&output.raw)
            || std::get_if<DerivationOutput::CAFixed>(&output.raw));

        // No need to sign CA outputs, only the realisation matters
        auto realisation = Realisation{
            {
                .outPath = *get(*submittedOutputs, outputName),
            },
            DrvOutput{
                .drvPath = drvPath,
                .outputName = outputName,
            },
        };

        store.signRealisation(realisation);
        store.registerDrvOutput(realisation, NoCheckSigs);
        builtOutputs.emplace(outputName, realisation);

        // TODO: handle --check
    }

    return builtOutputs;
}

namespace {

/** An absolute path that cannot exist, spelled for the running platform. */
std::filesystem::path noSuchPath()
{
#ifdef _WIN32
    return std::filesystem::path{"C:\\no-such-path"};
#else
    return std::filesystem::path{"/no-such-path"};
#endif
}

} // namespace

void DerivationBuilderImpl::startDaemon()
{
    if (usingSubmittedOutputs()) {
        experimentalFeatureSettings.require(Xp::DynamicDerivations);
    } else {
        experimentalFeatureSettings.require(Xp::RecursiveNix);
    }

    auto store = makeRestrictedStore(
        [&] {
            auto config = make_ref<LocalStore::Config>(*this->store.config);
            config->pathInfoCacheSize = 0;
            /* A deliberately unusable location: the restricted store must not
               touch real state. It has to be an absolute path, and on Windows a
               POSIX-rooted one is not --- `is_absolute()` wants a root name as
               well as a root directory --- so spell it natively. */
            config->stateDir = noSuchPath();
            config->logDir = noSuchPath();
            return config;
        }(),
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(this->store.shared_from_this())),
        *this);

    state_.lock()->addedPaths.clear();

    auto socketName = ".nix-socket";
    std::filesystem::path socketPath = tmpDir / socketName;
    daemonRemoteUri = "unix://" + (tmpDirInSandbox() / socketName).string();

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    prepareDaemonSocket(socketPath);

    daemon::RecursiveFlag recursiveFlag;
    if (usingSubmittedOutputs()) {
        recursiveFlag = daemon::RecursiveFlag::RecursiveSubmitted;
    } else {
        recursiveFlag = daemon::RecursiveFlag::Recursive;
    }

    daemonThread = std::thread([this, store, recursiveFlag]() {
        while (true) {

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            /* Winsock's `accept` takes an `int *`, POSIX a `socklen_t *`. */
#ifdef _WIN32
            int remoteAddrLen = sizeof(remoteAddr);
#else
            socklen_t remoteAddrLen = sizeof(remoteAddr);
#endif

            /* `toSocket`/`toDescriptor` are identities on Unix and the
               `SOCKET`/`HANDLE` conversion on Windows, as in
               `unix-domain-socket.cc`. */
            AutoCloseFD remote =
                toDescriptor(accept(toSocket(daemonSocket.get()), (struct sockaddr *) &remoteAddr, &remoteAddrLen));
            if (!remote) {
#ifdef _WIN32
                /* Winsock reports through `WSAGetLastError`, not `errno`. */
                auto err = WSAGetLastError();
                if (err == WSAEINTR)
                    continue;
                if (err == WSAEINVAL || err == WSAECONNABORTED || err == WSAENOTSOCK)
                    break;
                /* Cast required: `WSAGetLastError` yields `int`, which would
                   otherwise bind the variadic format overload rather than the
                   `(DWORD, fmt)` one. */
                throw windows::WinError(static_cast<DWORD>(err), "accepting connection");
#else
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                if (errno == EINVAL || errno == ECONNABORTED)
                    break;
                throw SysError("accepting connection");
#endif
            }

            setCloseOnExec(remote.get());

            debug("received daemon connection");

            auto doneFlag = make_ref<std::atomic_flag>();

            auto workerThread = std::thread([this, doneFlag, store, remote{std::move(remote)}, recursiveFlag]() {
                try {
                    miscMethods->processDaemonConnection(
                        store, FdSource(remote.get()), FdSink(remote.get()), *this, recursiveFlag);
                    debug("terminated daemon connection");
                } catch (const Interrupted &) {
                    debug("interrupted daemon connection");
                } catch (...) {
                    /* Swallow all exceptions to avoid crashing the the process (exceptions that escape from the thread
                     * trigger std::terminate()). */
                    ignoreExceptionExceptInterrupt();
                }

                doneFlag->test_and_set(std::memory_order_relaxed);
            });

            daemonWorkerThreads.push_back(
                DaemonWorkerState{
                    .thread = std::move(workerThread),
                    .done = std::move(doneFlag),
                });

            /* Prune threads eagerly to free up resources. Ideally we'd also limit the number of concurrent workers. */
            for (auto it = daemonWorkerThreads.begin(), end = daemonWorkerThreads.end(); it != end;) {
                auto & state = *it;
                auto & thread = state.thread;
                if (state.done->test(std::memory_order_relaxed) && thread.joinable()) {
                    thread.join();
                    it = daemonWorkerThreads.erase(it);
                } else {
                    ++it;
                }
            }
        }

        debug("daemon shutting down");
    });
}

void DerivationBuilderImpl::stopDaemon()
{
#ifdef _WIN32
    if (daemonSocket && ::shutdown(toSocket(daemonSocket.get()), SD_BOTH) == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAENOTCONN) {
            daemonSocket.close();
        } else {
            throw windows::WinError("shutting down daemon socket");
        }
    }
#else
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
        // According to the POSIX standard, the 'shutdown' function should
        // return an ENOTCONN error when attempting to shut down a socket that
        // hasn't been connected yet. This situation occurs when the 'accept'
        // function is called on a socket without any accepted connections,
        // leaving the socket unconnected. While Linux doesn't seem to produce
        // an error for sockets that have only been accepted, more
        // POSIX-compliant operating systems like OpenBSD, macOS, and others do
        // return the ENOTCONN error. Therefore, we handle this error here to
        // avoid raising an exception for compliant behaviour.
        if (errno == ENOTCONN) {
            daemonSocket.close();
        } else {
            throw SysError("shutting down daemon socket");
        }
    }
#endif

    if (daemonThread.joinable())
        daemonThread.join();

    for (auto & [thread, doneFlag] : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    // release the socket.
    daemonSocket.close();
}

void DerivationBuilderImpl::submitOutput(const SingleDerivedPath & path, const OutputName & output)
{
    auto submittedOutputs(this->submittedOutputs.lock());

    auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&path.raw());
    if (!opaque)
        throw Error(
            "Attempted to submit Built path '%s' for output '%s'.\n"
            " Only Opaque paths are supported, see https://github.com/NixOS/nix/issues/12727",
            path.to_string(store),
            output);

    if (submittedOutputs->contains(output))
        throw Error(
            "Attempted to submit duplicate output '%s' (old '%s', new '%s')",
            output,
            store.printStorePath(*get(*submittedOutputs, output)),
            store.printStorePath(opaque->path));

    submittedOutputs->insert_or_assign(output, opaque->path);
}

} // namespace nix
