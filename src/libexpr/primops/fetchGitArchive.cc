#include "primops.hh"
#include "store-api.hh"
#include "cache.hh"

namespace nix {

static void prim_fetchGitArchive(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::optional<Hash> expectedHash;
    std::string name = "source.tar.gz";
    std::string remote = "";
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
            remote = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the remote of the git archive we should fetch");
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
        {"remote", remote},
        {"version", version},
        {"format", format}
    });

    if (auto res = fetchers::getCache()->lookup(state.store, inAttrs)) {
        auto infoAttrs = res->first;
        auto storePath = res->second;
        state.allowAndSetStorePathString(storePath, v);
        return;
    }

    // Run `git archive`
    auto [errorCode, programOutput] = runProgram(RunOptions {
        .program = "git",
        .args = {"archive", "--format=" + format, "--remote=" + remote, version},
        .mergeStderrToStdout = true
    });
    if (errorCode) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt(programOutput),
            .errPos = state.positions[pos]
        }));
    }

    // Add archive to nix store
    auto hash = expectedHash ? expectedHash.value() : hashString(htSHA256, programOutput);
    auto storePath = state.store->makeFixedOutputPath(FileIngestionMethod::Flat, hash, name);

    StringSink narSink;
    narSink << "nix-archive-1" << "(" << "type" << "regular" << "contents" << programOutput << ")";
    auto narSource = StringSource(narSink.s);
    auto narHash = hashString(htSHA256, narSink.s);
    auto narSize = narSink.s.size();

    auto info = ValidPathInfo { storePath, narHash };
    info.narSize = narSize;
    info.ca = FixedOutputHash { FileIngestionMethod::Flat, hash };

    state.store->addToStore(info, narSource, NoRepair, NoCheckSigs);
    state.allowAndSetStorePathString(storePath, v);

    bool locked = (bool) expectedHash;
    fetchers::getCache()->add(state.store, inAttrs, {}, storePath, locked);
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
