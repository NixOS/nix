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
#include "nix/store/daemon.hh"
#include "nix/store/builtins.hh"
#include "nix/store/restricted-store.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/terminal.hh"
#include "nix/store/filetransfer.hh"
#include "build/derivation-check.hh"
#include "store-config-private.hh"

#include "nix/util/strings.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <grp.h>

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
#endif

namespace nix {

const std::filesystem::path homeDir = "/homeless-shelter";

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
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    std::function<std::filesystem::path(const std::string &)> realPathInHost)
{
    StringMap outputRewrites;
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

StringMap initEnv(
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
    StringMap env;

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

    return env;
}

std::tuple<OutputPathMap, StringMap, std::map<StorePath, StorePath>> computeScratchOutputs(
    LocalStore & store,
    const DerivationBuilderParams & params,
    bool needsHashRewrite)
{
    OutputPathMap scratchOutputs;
    StringMap inputRewrites;
    std::map<StorePath, StorePath> redirectedOutputs;
    for (auto & [outputName, status] : params.initialOutputs) {
        auto makeFallbackPath = [&](const std::string & suffix, std::string_view name) {
            return store.makeStorePath(
                "rewrite:" + std::string(params.drvPath.to_string()) + ":" + suffix,
                Hash(HashAlgorithm::SHA256),
                name);
        };
        auto scratchPath =
            !status.known
                ? makeFallbackPath("name:" + std::string(outputName), outputPathName(params.drv.name, outputName))
            : !needsHashRewrite        ? status.known->path
            : !status.known->isPresent() ? status.known->path
            : params.buildMode != bmRepair && !status.known->isValid()
                ? status.known->path
                : makeFallbackPath(std::string(status.known->path.to_string()), status.known->path.name());
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        inputRewrites[hashPlaceholder(outputName)] = store.printStorePath(scratchPath);

        if (!status.known)
            continue;
        auto fixedFinalPath = status.known->path;

        if (fixedFinalPath == scratchPath)
            continue;

        deletePath(store.printStorePath(scratchPath));

        {
            std::string h1{fixedFinalPath.hashPart()};
            std::string h2{scratchPath.hashPart()};
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    return {std::move(scratchOutputs), std::move(inputRewrites), std::move(redirectedOutputs)};
}

void stopDaemon(
    AutoCloseFD & daemonSocket,
    std::thread & daemonThread,
    std::vector<std::thread> & daemonWorkerThreads)
{
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
        if (errno == ENOTCONN) {
            daemonSocket.close();
        } else {
            throw SysError("shutting down daemon socket");
        }
    }

    if (daemonThread.joinable())
        daemonThread.join();

    for (auto & thread : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    daemonSocket.close();
}

void processSandboxSetupMessages(
    AutoCloseFD & builderOut,
    Pid & pid,
    const Store & store,
    const StorePath & drvPath)
{
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOut.get());
            } catch (Error & e) {
                auto status = pid.wait();
                e.addTrace(
                    {},
                    "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    store.printStorePath(drvPath),
                    statusToString(status),
                    concatStringsSep("|", msgs));
                throw;
            }
        }();
        if (msg.substr(0, 1) == "\2")
            break;
        if (msg.substr(0, 1) == "\1") {
            FdSource source(builderOut.get());
            auto ex = readError(source);
            ex.addTrace({}, "while setting up the build environment");
            throw ex;
        }
        debug("sandbox setup: " + msg);
        msgs.push_back(std::move(msg));
    }
}

void setupRecursiveNixDaemon(
    LocalStore & store,
    DerivationBuilder & builder,
    const DerivationBuilderParams & params,
    StorePathSet & addedPaths,
    StringMap & env,
    const std::filesystem::path & tmpDir,
    const std::filesystem::path & tmpDirInSandbox,
    AutoCloseFD & daemonSocket,
    std::thread & daemonThread,
    std::vector<std::thread> & daemonWorkerThreads,
    UserLock * buildUser)
{
    experimentalFeatureSettings.require(Xp::RecursiveNix);

    auto restrictedStore = makeRestrictedStore(
        [&] {
            auto config = make_ref<LocalStore::Config>(*store.config);
            config->pathInfoCacheSize = 0;
            config->stateDir = "/no-such-path";
            config->logDir = "/no-such-path";
            return config;
        }(),
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(store.shared_from_this())),
        builder);

    addedPaths.clear();

    auto socketName = ".nix-socket";
    std::filesystem::path socketPath = tmpDir / socketName;
    env["NIX_REMOTE"] = "unix://" + (tmpDirInSandbox / socketName).native();

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    nix::chownToBuilder(buildUser, socketPath);

    daemonThread = std::thread([&daemonSocket, &daemonWorkerThreads, restrictedStore]() {
        while (true) {
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(daemonSocket.get(), (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            if (!remote) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                if (errno == EINVAL || errno == ECONNABORTED)
                    break;
                throw SysError("accepting connection");
            }

            unix::closeOnExec(remote.get());

            debug("received daemon connection");

            auto workerThread = std::thread([restrictedStore, remote{std::move(remote)}]() {
                try {
                    daemon::processConnection(
                        restrictedStore,
                        FdSource(remote.get()),
                        FdSink(remote.get()),
                        NotTrusted,
                        daemon::Recursive);
                    debug("terminated daemon connection");
                } catch (const Interrupted &) {
                    debug("interrupted daemon connection");
                } catch (SystemError &) {
                    ignoreExceptionExceptInterrupt();
                }
            });

            daemonWorkerThreads.push_back(std::move(workerThread));
        }

        debug("daemon shutting down");
    });
}

void logBuilderInfo(const BasicDerivation & drv)
{
    printMsg(lvlChatty, "executing builder '%1%'", drv.builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv.args));
    for (auto & i : drv.env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);
}

void setupPTYMaster(
    AutoCloseFD & builderOut,
    UserLock * buildUser,
    bool grantOnNoBuildUser)
{
    builderOut = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut)
        throw SysError("opening pseudoterminal master");

    std::string slaveName = getPtsName(builderOut.get());

    if (buildUser) {
        chmod(slaveName, 0600);

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    } else if (grantOnNoBuildUser) {
        if (grantpt(builderOut.get()))
            throw SysError("granting access to pseudoterminal slave");
    }

    if (unlockpt(builderOut.get()))
        throw SysError("unlocking pseudoterminal");
}

void setupPTYSlave(int masterFd)
{
    std::string slaveName = getPtsName(masterFd);

    AutoCloseFD slaveOut = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
    if (!slaveOut)
        throw SysError("opening pseudoterminal slave");

    struct termios term;
    if (tcgetattr(slaveOut.get(), &term))
        throw SysError("getting pseudoterminal attributes");

    cfmakeraw(&term);

    if (tcsetattr(slaveOut.get(), TCSANOW, &term))
        throw SysError("putting pseudoterminal into raw mode");

    if (dup2(slaveOut.get(), STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");
}

#if NIX_WITH_AWS_AUTH
std::optional<AwsCredentials> preResolveAwsCredentials(const BasicDerivation & drv)
{
    if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
        auto url = drv.env.find("url");
        if (url != drv.env.end()) {
            try {
                auto parsedUrl = parseURL(url->second);
                if (parsedUrl.scheme == "s3") {
                    debug("Pre-resolving AWS credentials for S3 URL in builtin:fetchurl");
                    auto s3Url = ParsedS3URL::parse(parsedUrl);
                    auto credentials = getAwsCredentialsProvider()->getCredentials(s3Url);
                    debug("Successfully pre-resolved AWS credentials in parent process");
                    return credentials;
                }
            } catch (const std::exception & e) {
                debug("Error pre-resolving S3 credentials: %s", e.what());
            }
        }
    }
    return std::nullopt;
}
#endif

void setupBuiltinFetchurlContext(
    BuiltinBuilderContext & ctx,
    const BasicDerivation & drv)
{
    if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
        try {
            ctx.netrcData = readFile(fileTransferSettings.netrcFile);
        } catch (SystemError &) {
        }

        if (auto & caFile = fileTransferSettings.caFile.get())
            try {
                ctx.caFileData = readFile(*caFile);
            } catch (SystemError &) {
            }
    }
}

[[noreturn]] void runBuiltinBuilder(
    BuiltinBuilderContext & ctx,
    const BasicDerivation & drv,
    const OutputPathMap & scratchOutputs,
    Store & store)
{
    try {
        logger = makeJSONLogger(getStandardError());

        for (auto & e : drv.outputs)
            ctx.outputs.insert_or_assign(e.first, store.printStorePath(scratchOutputs.at(e.first)));

        std::string builtinName = drv.builder.substr(8);
        assert(RegisterBuiltinBuilder::builtinBuilders);
        if (auto builtin = get(RegisterBuiltinBuilder::builtinBuilders(), builtinName))
            (*builtin)(ctx);
        else
            throw Error("unsupported builtin builder '%1%'", builtinName);
        _exit(0);
    } catch (std::exception & e) {
        writeFull(STDERR_FILENO, e.what() + std::string("\n"));
        _exit(1);
    }
}

void dropPrivileges(UserLock & buildUser)
{
    preserveDeathSignal([&]() {
        auto gids = buildUser.getSupplementaryGIDs();
        if (setgroups(gids.size(), gids.data()) == -1)
            throw SysError("cannot set supplementary groups of build user");

        if (setgid(buildUser.getGID()) == -1 || getgid() != buildUser.getGID()
            || getegid() != buildUser.getGID())
            throw SysError("setgid failed");

        if (setuid(buildUser.getUID()) == -1 || getuid() != buildUser.getUID()
            || geteuid() != buildUser.getUID())
            throw SysError("setuid failed");
    });
}

[[noreturn]] void execBuilder(
    const BasicDerivation & drv,
    const StringMap & inputRewrites,
    const StringMap & env)
{
    Strings buildArgs;
    buildArgs.push_back(std::string(baseNameOf(drv.builder)));

    for (auto & i : drv.args)
        buildArgs.push_back(rewriteStrings(i, inputRewrites));

    Strings envStrs;
    for (auto & i : env)
        envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

    execve(drv.builder.c_str(), stringsToCharPtrs(buildArgs).data(), stringsToCharPtrs(envStrs).data());

    throw SysError("executing '%1%'", drv.builder);
}

bool isDiskFull(LocalStore & store, const std::filesystem::path & tmpDir)
{
    bool diskFull = false;
#if HAVE_STATVFS
    {
        uint64_t required = 8ULL * 1024 * 1024;
        struct statvfs st;
        if (statvfs(store.config->realStoreDir.get().c_str(), &st) == 0
            && (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
        if (statvfs(tmpDir.c_str(), &st) == 0 && (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
    }
#endif
    return diskFull;
}

int commonUnprepare(
    Pid & pid,
    const Store & store,
    const StorePath & drvPath,
    BuildResult & buildResult,
    DerivationBuilderCallbacks & miscMethods,
    AutoCloseFD & builderOut)
{
    int status = pid.kill();

    debug("builder process for '%s' finished", store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(0);

    miscMethods.childTerminated();

    builderOut.close();

    miscMethods.closeLogFile();

    return status;
}

void logCpuUsage(
    const Store & store,
    const StorePath & drvPath,
    const BuildResult & buildResult,
    int status)
{
    if (buildResult.cpuUser && buildResult.cpuSystem) {
        debug(
            "builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
            store.printStorePath(drvPath),
            status,
            ((double) buildResult.cpuUser->count()) / 1000000,
            ((double) buildResult.cpuSystem->count()) / 1000000);
    }
}

void cleanupBuildCore(
    bool force,
    LocalStore & store,
    const std::map<StorePath, StorePath> & redirectedOutputs,
    const BasicDerivation & drv,
    std::filesystem::path & topTmpDir,
    std::filesystem::path & tmpDir)
{
    if (force) {
        for (auto & i : redirectedOutputs)
            deletePath(store.toRealPath(i.second));
    }

    if (topTmpDir != "") {
        chmod(topTmpDir, 0000);

        if (settings.keepFailed && !force && !drv.isBuiltin()) {
            printError("note: keeping build directory %s", PathFmt(tmpDir));
            chmod(topTmpDir, 0755);
            chmod(tmpDir, 0755);
        } else
            deletePath(topTmpDir);
        topTmpDir = "";
        tmpDir = "";
    }
}

void checkAndAddImpurePaths(
    PathsInChroot & pathsInChroot,
    const DerivationOptions<StorePath> & drvOptions,
    const Store & store,
    const StorePath & drvPath,
    const PathSet & allowedPrefixes)
{
    auto impurePaths = drvOptions.impureHostDeps;

    for (auto & i : impurePaths) {
        bool found = false;
        std::filesystem::path canonI = canonPath(i);
        for (auto & a : allowedPrefixes) {
            std::filesystem::path canonA = canonPath(a);
            if (isDirOrInDir(canonI, canonA)) {
                found = true;
                break;
            }
        }
        if (!found)
            throw Error(
                "derivation '%s' requested impure path '%s', but it was not in allowed-impure-host-deps",
                store.printStorePath(drvPath),
                i);

        pathsInChroot[i] = {i, true};
    }
}

void parsePreBuildHook(
    PathsInChroot & pathsInChroot,
    const std::string & hookOutput)
{
    enum BuildHookState { stBegin, stExtraChrootDirs };

    auto state = stBegin;
    auto lastPos = std::string::size_type{0};
    for (auto nlPos = hookOutput.find('\n'); nlPos != std::string::npos; nlPos = hookOutput.find('\n', lastPos)) {
        auto line = hookOutput.substr(lastPos, nlPos - lastPos);
        lastPos = nlPos + 1;
        if (state == stBegin) {
            if (line == "extra-sandbox-paths" || line == "extra-chroot-dirs") {
                state = stExtraChrootDirs;
            } else {
                throw Error("unknown pre-build hook command '%1%'", line);
            }
        } else if (state == stExtraChrootDirs) {
            if (line == "") {
                state = stBegin;
            } else {
                auto p = line.find('=');
                if (p == std::string::npos)
                    pathsInChroot[line] = {.source = line};
                else
                    pathsInChroot[line.substr(0, p)] = {.source = line.substr(p + 1)};
            }
        }
    }
}

} // namespace nix
