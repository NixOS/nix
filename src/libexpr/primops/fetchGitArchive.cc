#include "primops.hh"
#include "store-api.hh"
#include "cache.hh"
#include "tarfile.hh"
#include "archive.hh"


namespace nix {

static void prim_fetchGitArchive(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::optional<Hash> expectedHash;
    std::optional<std::string> remote;
    std::string name = "source";
    std::string format = "tar.gz";
    std::string version = "HEAD";

    state.forceValue(*args[0], pos);
    for (auto & attr : *args[0]->attrs) {
        std::string_view n(state.symbols[attr.name]);
        if (n == "name")
            name = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the name of the git archive we should fetch");
        else if (n == "sha256")
            expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the sha256 of the git archive we should fetch"), htSHA256);
        else if (n == "remote")
            remote.emplace(state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the remote of the git archive we should fetch"));
        else if (n == "format")
            format = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the format of the git archive we should fetch");
        else if (n == "version")
            version = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the version of the git archive we should fetch");
        else
            state.debugThrowLastTrace(EvalError({
                .msg = hintfmt("unsupported argument '%s' to 'fetchGitArchive'", n),
                .errPos = state.positions[attr.pos]
            }));
    }

    if (!remote) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("missing required argument 'remote' to 'fetchGitArchive'"),
            .errPos = state.positions[pos]
        }));
    }
    if (evalSettings.pureEval && !expectedHash) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("in pure evaluation mode, 'fetchGitArchive' requires a 'sha256' argument"),
            .errPos = state.positions[pos]
        }));
    }
    if (expectedHash) {
        auto expectedPath = state.store->makeFixedOutputPath(FileIngestionMethod::Recursive, *expectedHash, name, {});
        if (state.store->isValidPath(expectedPath)) {
            state.allowAndSetStorePathString(expectedPath, v);
            return;
        }
    }

    auto inAttrs = fetchers::Attrs({
        {"type", "git-archive"},
        {"name", name},
        {"remote", *remote},
        {"version", version},
        {"format", format}
    });

    if (auto res = fetchers::getCache()->lookup(state.store, inAttrs)) {
        auto infoAttrs = res->first;
        auto storePath = res->second;
        state.allowAndSetStorePathString(storePath, v);
        return;
    }

    auto [errorCode, programOutput] = runProgram(RunOptions {
        .program = "git",
        .args = {"archive", "--format=" + format, "--remote=" + *remote, version},
        .mergeStderrToStdout = true
    });
    if (errorCode) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("git archive failed with exit code %i:\n" + programOutput, errorCode),
            .errPos = state.positions[pos]
        }));
    }

    auto tarSource = StringSource(programOutput);
    auto tmpDir = createTempDir();
    AutoDelete(tmpDir, true);
    unpackTarfile(tarSource, tmpDir);

    PathFilter filter = [](const Path &) { return true; };
    auto storePath = state.store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, filter);
    if (expectedHash) {
        auto narHash = state.store->queryPathInfo(storePath)->narHash;
        if (narHash != *expectedHash) {
            state.debugThrowLastTrace(EvalError({
                .msg = hintfmt(
                    "hash mismatch in git archive downloaded from (remote) :\n  specified: %s\n  got:       %s",
                    expectedHash->to_string(Base32, true),
                    narHash.to_string(Base32, true)
                ),
                .errPos = state.positions[pos]
            }));
        }
    }
    state.allowAndSetStorePathString(storePath, v);

    auto infoAttrs = fetchers::Attrs({});
    bool locked = (bool) expectedHash;
    fetchers::getCache()->add(state.store, inAttrs, infoAttrs, storePath, locked);
}

static RegisterPrimOp primop_fetchGitArchive({
    .name = "fetchGitArchive",
    .args = {"args"},
    .doc = R"(
      Fetch a git archive using the git-archive command.
      See https://git-scm.com/docs/git-archive
      *args* is an attribute set with the following attributes:
      - `name`
      - `remote`
      - `format`
      - `version`
      - `sha256`

      To fetch a version from a private repository over SSH:
      ```nix
      builtins.fetchGitArchive {
      remote = "git@gitlab.com:my-secret/repository.git";
      version = "v1.2.3";
      }
      ```
    )",
    .fun = prim_fetchGitArchive,
});
}
