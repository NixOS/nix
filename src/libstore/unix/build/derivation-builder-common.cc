#include "derivation-builder-common.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"
#include "nix/util/logging.hh"
#include "nix/store/local-store.hh"
#include "nix/store/path-references.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/archive.hh"
#include "nix/util/git.hh"
#include "nix/util/topo-sort.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "build/derivation-check.hh"
#include "store-config-private.hh"

#include "nix/util/strings.hh"
#include "nix/util/environment-variables.hh"

#include <unistd.h>
#include <fcntl.h>

namespace nix {

void handleDiffHook(
    const Path & diffHook,
    uid_t uid,
    uid_t gid,
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
                .uid = uid,
                .gid = gid,
                .chdir = "/"});
        if (!statusOk(diffRes.first))
            throw ExecError(
                diffRes.first, "diff-hook program %s %2%", PathFmt(diffHook), statusToString(diffRes.first));

        if (diffRes.second != "")
            printError(chomp(diffRes.second));
    } catch (Error & error) {
        ErrorInfo ei = error.info();
        ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
        logError(ei);
    }
}

void rethrowExceptionAsError()
{
    try {
        throw;
    } catch (Error &) {
        throw;
    } catch (std::exception & e) {
        throw Error(e.what());
    } catch (...) {
        throw Error("unknown exception");
    }
}

void handleChildException(bool sendException)
{
    try {
        rethrowExceptionAsError();
    } catch (Error & e) {
        if (sendException) {
            writeFull(STDERR_FILENO, "\1\n");
            FdSink sink(STDERR_FILENO);
            sink << e;
            sink.flush();
        } else
            std::cerr << e.msg();
    }
}

void checkNotWorldWritable(std::filesystem::path path)
{
    while (true) {
        auto st = lstat(path);
        if (st.st_mode & S_IWOTH)
            throw Error("Path %s is world-writable or a symlink. That's not allowed for security.", PathFmt(path));
        if (path == path.parent_path())
            break;
        path = path.parent_path();
    }
    return;
}

void movePath(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    auto st = lstat(src);

    bool changePerm = (geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));

    if (changePerm)
        chmod(src, st.st_mode | S_IWUSR);

    std::filesystem::rename(src, dst);

    if (changePerm)
        chmod(dst, st.st_mode);
}

void replaceValidPath(const std::filesystem::path & storePath, const std::filesystem::path & tmpPath)
{
    std::filesystem::path oldPath;

    if (pathExists(storePath)) {
        do {
            oldPath = makeTempPath(storePath, ".old");
        } while (pathExists(oldPath));
        movePath(storePath, oldPath);
    }
    try {
        movePath(tmpPath, storePath);
    } catch (...) {
        try {
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

SingleDrvOutputs registerOutputs(
    LocalStore & store,
    const LocalSettings & localSettings,
    const DerivationBuilderParams & params,
    const StorePathSet & addedPaths,
    const std::map<std::string, StorePath> & scratchOutputs,
    StringMap & outputRewrites,
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    std::function<std::filesystem::path(const std::string &)> realPathInHost)
{
    auto & drv = params.drv;
    auto & drvPath = params.drvPath;
    auto & drvOptions = params.drvOptions;
    auto & initialOutputs = params.initialOutputs;
    auto & buildMode = params.buildMode;
    auto & inputPaths = params.inputPaths;

    std::map<std::string, ValidPathInfo> infos;

    InodesSeen inodesSeen;

    StorePathSet referenceablePaths;
    for (auto & p : inputPaths)
        referenceablePaths.insert(p);
    for (auto & i : scratchOutputs)
        referenceablePaths.insert(i.second);
    for (auto & p : addedPaths)
        referenceablePaths.insert(p);

    StringSet outputsToSort;

    struct AlreadyRegistered
    {
        StorePath path;
    };

    struct PerhapsNeedToRegister
    {
        StorePathSet refs;
        StringSet otherOutputs;
    };

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

        auto initialOutput = get(initialOutputs, outputName);
        assert(initialOutput);
        auto & initialInfo = *initialOutput;

        bool wanted = buildMode == bmCheck || !(initialInfo.known && initialInfo.known->isValid());
        if (!wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName, AlreadyRegistered{.path = initialInfo.known->path});
            continue;
        }

        auto optSt = maybeLstat(actualPath.c_str());
        if (!optSt)
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to produce output path for output '%s' at %s",
                store.printStorePath(drvPath),
                outputName,
                PathFmt(actualPath));
        PosixStat & st = *optSt;

#ifndef __CYGWIN__
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH)))
            || (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "suspicious ownership or permission on %s for output '%s'; rejecting this build output",
                PathFmt(actualPath),
                outputName);
#endif

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
                [&](const AlreadyRegistered &) -> const StringSet & { return emptySet; },
                [&](const PerhapsNeedToRegister & refs) -> const StringSet & { return refs.otherOutputs; },
            },
            *orifu);
    });

    auto sortedOutputNames = std::visit(
        overloaded{
            [&](Cycle<std::string> & cycle) -> std::vector<std::string> {
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                    store.printStorePath(drvPath),
                    cycle.path,
                    cycle.parent);
            },
            [](auto & sorted) { return sorted; }},
        topoSortResult);

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    OutputPathMap finalOutputs;

    for (auto & outputName : sortedOutputNames) {
        auto output = get(drv.outputs, outputName);
        auto scratchPath = get(scratchOutputs, outputName);
        assert(output && scratchPath);
        auto actualPath = realPathInHost(store.printStorePath(*scratchPath));

        auto finish = [&](StorePath finalStorePath) {
            finalOutputs.insert_or_assign(outputName, finalStorePath);
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
            if (!rewrites.empty()) {
                debug("rewriting hashes in %1%; cross fingers", PathFmt(actualPath));

                auto source = sinkToSource([&](Sink & nextSink) {
                    RewritingSink rsink(rewrites, nextSink);
                    dumpPath(actualPath, rsink);
                    rsink.flush();
                });
                std::filesystem::path tmpPath = actualPath.native() + ".tmp";
                restorePath(tmpPath, *source);
                deletePath(actualPath);
                movePath(tmpPath, actualPath);

                canonicalisePathMetaData(
                    actualPath,
                    {
#ifndef _WIN32
                        .uidRange = std::nullopt,
#endif
                        NIX_WHEN_SUPPORT_ACLS(localSettings.ignoredAcls)},
                    inodesSeen);
            }
        };

        auto rewriteRefs = [&]() -> StoreReferences {
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
                if (!S_ISREG(st->st_mode) || (st->st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "output path %1% should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        PathFmt(actualPath));
            }
            rewriteOutput(outputRewrites);
            std::string oldHashPart{scratchPath->hashPart()};
            auto got = [&] {
                auto fim = outputHash.method.getFileIngestionMethod();
                switch (fim) {
                case FileIngestionMethod::Flat:
                case FileIngestionMethod::NixArchive: {
                    HashModuloSink caSink{outputHash.hashAlgo, oldHashPart};
                    auto fim = outputHash.method.getFileIngestionMethod();
                    dumpPath(
                        {getFSSourceAccessor(), CanonPath(actualPath.native())}, caSink, (FileSerialisationMethod) fim);
                    return caSink.finish().hash;
                }
                case FileIngestionMethod::Git: {
                    return git::dumpHash(outputHash.hashAlgo, {getFSSourceAccessor(), CanonPath(actualPath.native())})
                        .hash;
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
                rewriteOutput(StringMap{{oldHashPart, std::string(newInfo0.path.hashPart())}});
            }

            {
                HashResult narHashAndSize = hashPath(
                    {getFSSourceAccessor(), CanonPath(actualPath.native())},
                    FileSerialisationMethod::NixArchive,
                    HashAlgorithm::SHA256);
                newInfo0.narHash = narHashAndSize.hash;
                newInfo0.narSize = narHashAndSize.numBytesDigested;
            }

            assert(newInfo0.ca);
            return newInfo0;
        };

        ValidPathInfo newInfo = std::visit(
            overloaded{

                [&](const DerivationOutput::InputAddressed & output) {
                    auto requiredFinalPath = output.path;
                    if (*scratchPath != requiredFinalPath)
                        outputRewrites.insert_or_assign(
                            std::string{scratchPath->hashPart()}, std::string{requiredFinalPath.hashPart()});
                    rewriteOutput(outputRewrites);
                    HashResult narHashAndSize = hashPath(
                        {getFSSourceAccessor(), CanonPath(actualPath.native())},
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

                    std::filesystem::path tmpOutput = actualPath.native() + ".tmp";
                    copyFile(actualPath, tmpOutput, true);
                    std::filesystem::rename(tmpOutput, actualPath);

                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = dof.ca.method,
                            .hashAlgo = wanted.algo,
                        });
                },

                [&](const DerivationOutput::CAFloating & dof) { return newInfoFromCA(dof); },

                [&](const DerivationOutput::Deferred &) -> ValidPathInfo { assert(false); },

                [&](const DerivationOutput::Impure & doi) {
                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = doi.method,
                            .hashAlgo = doi.hashAlgo,
                        });
                },

            },
            output->raw);

        canonicalisePathMetaData(
            actualPath,
            {
#ifndef _WIN32
                .uidRange = std::nullopt,
#endif
                NIX_WHEN_SUPPORT_ACLS(localSettings.ignoredAcls)},
            inodesSeen);

        auto finalDestPath = store.printStorePath(newInfo.path);

        PathLocks dynamicOutputLock;
        dynamicOutputLock.setDeletion(true);
        auto optFixedPath = output->path(store, drv.name, outputName);
        if (!optFixedPath || store.printStorePath(*optFixedPath) != finalDestPath) {
            assert(newInfo.ca);
            dynamicOutputLock.lockPaths({store.toRealPath(finalDestPath)});
        }

        if (store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                replaceValidPath(store.toRealPath(finalDestPath), actualPath);
            } else if (buildMode == bmCheck) {
                /* leave new path in place for comparison */
            } else if (store.isValidPath(newInfo.path)) {
                assert(newInfo.ca);
                deletePath(actualPath);
            } else {
                auto destPath = store.toRealPath(finalDestPath);
                deletePath(destPath);
                movePath(actualPath, destPath);
            }
        }

        if (buildMode == bmCheck) {
            if (store.isValidPath(newInfo.path)) {
                ValidPathInfo oldInfo(*store.queryPathInfo(newInfo.path));
                if (newInfo.narHash != oldInfo.narHash) {
                    auto * diffHook = localSettings.getDiffHook();
                    if (diffHook || settings.keepFailed) {
                        auto dst = store.toRealPath(finalDestPath + ".check");
                        deletePath(dst);
                        movePath(actualPath, dst);

                        if (diffHook) {
                            handleDiffHook(
                                *diffHook,
                                buildUser ? buildUser->getUID() : getuid(),
                                buildUser ? buildUser->getGID() : getgid(),
                                finalDestPath,
                                dst,
                                store.printStorePath(drvPath),
                                tmpDir);
                        }

                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output '%s' differs from '%s'",
                            store.printStorePath(drvPath),
                            store.toRealPath(finalDestPath),
                            dst);
                    } else
                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output '%s' differs",
                            store.printStorePath(drvPath),
                            store.toRealPath(finalDestPath));
                }

                if (!oldInfo.ultimate) {
                    oldInfo.ultimate = true;
                    store.signPathInfo(oldInfo);
                    store.registerValidPaths({{oldInfo.path, oldInfo}});
                }
            }
        } else {
            for (auto & i : inputPaths) {
                if (references.count(i))
                    debug("referenced input: '%1%'", store.printStorePath(i));
                else
                    debug("unreferenced input: '%1%'", store.printStorePath(i));
            }

            if (!store.isValidPath(newInfo.path))
                store.optimisePath(store.toRealPath(finalDestPath), NoRepair);

            newInfo.deriver = drvPath;
            newInfo.ultimate = true;
            store.signPathInfo(newInfo);

            finish(newInfo.path);

            if (newInfo.ca)
                store.registerValidPaths({{newInfo.path, newInfo}});
        }

        infos.emplace(outputName, std::move(newInfo));
    }

    checkOutputs(store, drvPath, drv.outputs, drvOptions.outputChecks, infos);

    if (buildMode == bmCheck) {
        return {};
    }

    {
        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        store.registerValidPaths(infos2);
    }

    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, newInfo] : infos) {
        auto oldinfo = get(initialOutputs, outputName);
        assert(oldinfo);
        auto thisRealisation = Realisation{
            {
                .outPath = newInfo.path,
            },
            DrvOutput{oldinfo->outputHash, outputName},
        };
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !drv.type().isImpure()) {
            store.signRealisation(thisRealisation);
            store.registerDrvOutput(thisRealisation);
        }
        builtOutputs.emplace(outputName, thisRealisation);
    }

    return builtOutputs;
}

void chownToBuilder(UserLock * buildUser, const std::filesystem::path & path)
{
    if (!buildUser)
        return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of %1%", PathFmt(path));
}

void chownToBuilder(UserLock * buildUser, int fd, const std::filesystem::path & path)
{
    if (!buildUser)
        return;
    if (fchown(fd, buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of file %1%", PathFmt(path));
}

void writeBuilderFile(
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    int tmpDirFd,
    const std::string & name,
    std::string_view contents)
{
    auto path = std::filesystem::path(tmpDir) / name;
    AutoCloseFD fd{
        openat(tmpDirFd, name.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC | O_EXCL | O_NOFOLLOW, 0666)};
    if (!fd)
        throw SysError("creating file %s", PathFmt(path));
    writeFile(fd, path, contents);
    chownToBuilder(buildUser, fd.get(), path);
}

void initEnv(
    StringMap & env,
    const std::filesystem::path & homeDir,
    const std::string & storeDir,
    const DerivationBuilderParams & params,
    const StringMap & inputRewrites,
    const DerivationType & derivationType,
    const LocalSettings & localSettings,
    const std::filesystem::path & tmpDirInSandbox,
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    int tmpDirFd)
{
    env.clear();

    env["PATH"] = "/path-not-set";
    env["HOME"] = homeDir;
    env["NIX_STORE"] = storeDir;
    env["NIX_BUILD_CORES"] = fmt(
        "%d",
        settings.getLocalSettings().buildCores ? settings.getLocalSettings().buildCores : settings.getDefaultCores());

    for (const auto & [name, info] : params.desugaredEnv.variables) {
        env[name] = info.prependBuildDirectory ? (tmpDirInSandbox / info.value).string() : info.value;
    }

    for (const auto & [fileName, value] : params.desugaredEnv.extraFiles) {
        writeBuilderFile(buildUser, tmpDir, tmpDirFd, fileName, rewriteStrings(value, inputRewrites));
    }

    env["NIX_BUILD_TOP"] = tmpDirInSandbox;
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;
    env["PWD"] = tmpDirInSandbox;

    if (derivationType.isFixed())
        env["NIX_OUTPUT_CHECKED"] = "1";

    if (!derivationType.isSandboxed()) {
        auto & impureEnv = localSettings.impureEnv.get();
        if (!impureEnv.empty())
            experimentalFeatureSettings.require(Xp::ConfigurableImpureEnv);

        for (auto & i : params.drvOptions.impureEnvVars) {
            auto envVar = impureEnv.find(i);
            if (envVar != impureEnv.end()) {
                env[i] = envVar->second;
            } else {
                env[i] = getEnv(i).value_or("");
            }
        }
    }

    env["NIX_LOG_FD"] = "2";
    env["TERM"] = "xterm-256color";
}

} // namespace nix
