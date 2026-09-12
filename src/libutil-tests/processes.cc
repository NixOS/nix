#include "nix/util/processes.hh"
#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <optional>

namespace nix {

/* ----------------------------------------------------------------------------
 * statusOk
 * --------------------------------------------------------------------------*/

TEST(statusOk, zeroIsOk)
{
    ASSERT_EQ(statusOk(0), true);
    ASSERT_EQ(statusOk(1), false);
}

/* ----------------------------------------------------------------------------
 * runProgram
 * --------------------------------------------------------------------------*/

TEST(runProgram, worksTrivial)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    for (bool isInteractive : {false, true}) {
        std::string output;
        ASSERT_NO_THROW({
            output = runProgram(
                *self,
                /*lookupPath=*/false,
                {
                    OS_STR("__util_test_spawn_trivial"),
                },
                /*isInteractive=*/isInteractive);
        });
        ASSERT_EQ(output, "hello");
    }
}

#ifdef _WIN32
#  define NIX_EXECUTABLE_EXTENSION ".exe"
/* FIXME: runProgram reports spawn errors as WinError, while other
   platforms conflate everything into ExecError. */
#  define NIX_SPAWN_EXCEPTION windows::WinError
#else
#  define NIX_EXECUTABLE_EXTENSION ""
#  define NIX_SPAWN_EXCEPTION ExecError
#endif

TEST(runProgram, nonexistent)
{
    /* For now spawn failures get reported as ExecError. TODO: Report the proper error that's less
       confusing. */
    ASSERT_THROW(
        runProgram("/this/path/really/should/not/exist/for/real" NIX_EXECUTABLE_EXTENSION), NIX_SPAWN_EXCEPTION);
}

TEST(runProgram2, nonexistent)
{
    ASSERT_THROW(
        {
            runProgram2({
                .program = "/this/path/really/should/not/exist/for/real" NIX_EXECUTABLE_EXTENSION,
            });
        },
        NIX_SPAWN_EXCEPTION);
}

#ifndef _WIN32 /* Leaking file descriptors into the child isn't a concern on windows. */

TEST(runProgram2, leakedFDsAreClosed)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);
    int fds[2];
    ASSERT_NE(::pipe(fds), -1);

    AutoCloseFD readSide = fds[0];
    AutoCloseFD writeSide = fds[1];

    /* Test that both fds are closed in the child. Just the one isn't always
       enough if the read side is assigned to 3 (also the fd of the relocated pipe in
       the child on linux that gets dup3-ed into). */
    ASSERT_NO_THROW(runProgram2({
        .program = *self,
        .args = {"__util_test_spawn_leaked_fds"},
        .environment = OsStringMap{{
            "NIX_CHILD_FDS_SHOULD_BE_CLOSED",
            fmt("%d,%d", readSide.get(), writeSide.get()),
        }},
    }));
}

#endif

/* ----------------------------------------------------------------------------
 * The `RunOptions` contract
 *
 * Each field below was previously unexercised, so a refactor of the spawn path
 * could drop one and stay green. These are deliberately behavioural rather than
 * structural: they observe the child, not the options struct.
 *
 * The child in every case is this test binary re-execed with a marker argument;
 * see the dispatch in `main.cc`.
 *
 * One footgun, found the hard way: the two `environment` tests replace the
 * child's environment wholesale, which drops `LD_LIBRARY_PATH`. That is correct
 * and is the point of the test, but it means the child can only find its shared
 * libraries via RPATH. Running this binary relocated from its build tree, with
 * the libraries reachable only through `LD_LIBRARY_PATH`, makes those two fail
 * in the loader rather than in the assertion.
 * --------------------------------------------------------------------------*/

TEST(runProgram2, nonZeroExitIsReportedWithItsStatus)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    /* `ExecError::status` is a raw wait status, not an exit code, so assert
       through `statusOk` rather than comparing it to 3 directly. */
    try {
        runProgram2({
            .program = *self,
            .lookupPath = false,
            .args = {OS_STR("__util_test_spawn_exit_code"), OS_STR("3")},
        });
        FAIL() << "expected ExecError for a child that exited 3";
    } catch (ExecError & e) {
        ASSERT_FALSE(statusOk(e.status));
#ifndef _WIN32
        ASSERT_TRUE(WIFEXITED(e.status));
        ASSERT_EQ(WEXITSTATUS(e.status), 3);
#endif
    }
}

TEST(runProgram, capturesStandardOutAndNotStandardError)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_streams")},
    });

    ASSERT_TRUE(statusOk(status));
    /* The child writes "out:" to stdout and "err:" to stderr. Without
       `mergeStderrToStdout` only the former is captured. */
    ASSERT_EQ(output, "out:");
}

TEST(runProgram, mergeStderrToStdoutCapturesBoth)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_streams")},
        .mergeStderrToStdout = true,
    });

    ASSERT_TRUE(statusOk(status));
    /* Assert containment rather than an exact string: the two streams are
       separately buffered, so their interleaving is not guaranteed. */
    ASSERT_THAT(output, ::testing::HasSubstr("out:"));
    ASSERT_THAT(output, ::testing::HasSubstr("err:"));
}

TEST(runProgram, environmentReplacesRatherThanExtends)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    setEnv("NIX_TEST_SHOULD_NOT_REACH_CHILD", "leaked");
    ASSERT_EQ(getEnv("NIX_TEST_SHOULD_NOT_REACH_CHILD"), std::optional<std::string>("leaked"));

    /* Supplying `environment` replaces the child's environment wholesale, so a
       variable set in this process must not be visible to the child. */
    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_print_env"), OS_STR("NIX_TEST_SHOULD_NOT_REACH_CHILD")},
        .environment = OsStringMap{{OS_STR("SOMETHING_ELSE"), OS_STR("1")}},
    });

    ASSERT_TRUE(statusOk(status));
    ASSERT_EQ(output, "<unset>");
}

TEST(runProgram, environmentPassesTheGivenVariable)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_print_env"), OS_STR("NIX_TEST_PASSED_THROUGH")},
        .environment = OsStringMap{{OS_STR("NIX_TEST_PASSED_THROUGH"), OS_STR("visible")}},
    });

    ASSERT_TRUE(statusOk(status));
    ASSERT_EQ(output, "visible");
}

TEST(runProgram, chdirIsHonoured)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    auto target = std::filesystem::canonical(std::filesystem::temp_directory_path());
    /* Positive control: if the test already runs there, the assertion below
       would hold whether or not `chdir` did anything. */
    ASSERT_NE(std::filesystem::canonical(std::filesystem::current_path()), target);

    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_print_cwd")},
        .chdir = target,
    });

    ASSERT_TRUE(statusOk(status));
    ASSERT_EQ(std::filesystem::canonical(output), target);
}

#ifndef _WIN32 /* `RunOptions::argv0` is Unix-only. */

TEST(runProgram, argv0IsHonoured)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    auto [status, output] = runProgram({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_print_argv0")},
        .argv0 = "not-the-program-name",
    });

    ASSERT_TRUE(statusOk(status));
    ASSERT_EQ(output, "not-the-program-name");
}

#endif

#ifndef _WIN32 /* `RunOptions::redirections` is Unix-only. */

/* ----------------------------------------------------------------------------
 * RunOptions::redirections
 *
 * Descriptors above stderr are the only reason this field exists, and they are
 * also exactly what the child's descriptor sweep is there to close. So the
 * ordering between "wire the redirections" and "close everything else" is the
 * whole behaviour, and it is not observable from the parent — the child has to
 * be asked.
 * --------------------------------------------------------------------------*/

namespace {

/**
 * Move `fd` to a descriptor well clear of the low numbers, so that a test can
 * use 4 and 5 as redirection *targets* without colliding with whatever
 * `::pipe` happened to hand out for the *sources*. Such a collision is
 * rejected by `validateRedirections`, which would make the test's outcome
 * depend on the process's descriptor allocation rather than on the code.
 */
static AutoCloseFD relocateHigh(AutoCloseFD fd, int to)
{
    EXPECT_NE(::dup2(fd.get(), to), -1);
    return AutoCloseFD{to};
}

} // namespace

TEST(runProgram2, redirectionsReachTheChild)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    int a[2], b[2];
    ASSERT_NE(::pipe(a), -1);
    ASSERT_NE(::pipe(b), -1);
    AutoCloseFD aRead = a[0], aWrite = relocateHigh(AutoCloseFD{a[1]}, 30);
    AutoCloseFD bRead = b[0], bWrite = relocateHigh(AutoCloseFD{b[1]}, 31);

    /* Fds 4 and 5 specifically: that is what `hook-instance.cc` wires, and it
       is above the `MAX_KEPT_FD` of the argument-less `closeExtraFDs`, so a
       sweep that does not exempt them takes both away before the exec. */
    ASSERT_NO_THROW(runProgram2({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_redirections")},
        .environment = OsStringMap{{OS_STR("NIX_CHILD_WRITE_FDS"), OS_STR("4:alpha,5:beta")}},
        .redirections = {{.sourceFd = aWrite.get(), .targetFd = 4}, {.sourceFd = bWrite.get(), .targetFd = 5}},
    }));

    /* Drop our own write ends, or the reads below would not see EOF. */
    aWrite.close();
    bWrite.close();

    auto drain = [](int fd) {
        std::string s;
        char buf[64];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0)
            s.append(buf, n);
        return s;
    };

    ASSERT_EQ(drain(aRead.get()), "alpha");
    ASSERT_EQ(drain(bRead.get()), "beta");
}

TEST(runProgram2, redirectionsDoNotKeepUnrelatedFDsOpen)
{
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    int a[2], b[2];
    ASSERT_NE(::pipe(a), -1);
    ASSERT_NE(::pipe(b), -1);
    AutoCloseFD aRead = a[0], aWrite = relocateHigh(AutoCloseFD{a[1]}, 30);
    AutoCloseFD bRead = b[0], bWrite = relocateHigh(AutoCloseFD{b[1]}, 31);

    /* Targets 4 and 9, with an unrelated descriptor at 6 — strictly between
       them — and another at 25, above both. That placement is the point: the
       child sweeps in two stages, a range close above the highest target and an
       individual walk below it, and only a descriptor inside the target range
       can reach the second one. A spare above the highest target exercises the
       first stage and would pass even if the second were removed entirely. */
    int spare[2];
    ASSERT_NE(::pipe(spare), -1);
    AutoCloseFD spareInside = relocateHigh(AutoCloseFD{spare[0]}, 6);
    AutoCloseFD spareAbove = relocateHigh(AutoCloseFD{spare[1]}, 25);

    ASSERT_NO_THROW(runProgram2({
        .program = *self,
        .lookupPath = false,
        .args = {OS_STR("__util_test_spawn_redirections")},
        .environment =
            OsStringMap{
                {OS_STR("NIX_CHILD_WRITE_FDS"), OS_STR("4:alpha,9:beta")},
                {OS_STR("NIX_CHILD_FDS_SHOULD_BE_CLOSED"), OS_STR("6,25")},
            },
        .redirections = {{.sourceFd = aWrite.get(), .targetFd = 4}, {.sourceFd = bWrite.get(), .targetFd = 9}},
    }));
}

TEST(startProgram, rejectsRedirectionOntoAStandardStream)
{
    /* `standardOut` and `mergeStderrToStdout` own the standard streams; a
       redirection onto one of them would fight with them silently. */
    ASSERT_THROW(
        {
            runProgram2({
                .program = "/nonexistent",
                .redirections = {{.sourceFd = 30, .targetFd = STDOUT_FILENO}},
            });
        },
        UsageError);
}

TEST(startProgram, rejectsSourceThatIsAnotherTarget)
{
    /* The duplications are applied in order with no bookkeeping, so this would
       silently give the second redirection the first one's descriptor. */
    ASSERT_THROW(
        {
            runProgram2({
                .program = "/nonexistent",
                .redirections = {{.sourceFd = 30, .targetFd = 7}, {.sourceFd = 7, .targetFd = 8}},
            });
        },
        UsageError);
}

TEST(startProgram, standardOutFdReceivesStdout)
{
    /* `standardOut` collects stdout into a `Sink`. A caller that already owns
       the write end of a pipe wants the descriptor wired directly instead, and
       `Redirection` refuses to express it because `targetFd` may not be a
       standard stream. Without `standardOutFd` such a caller has to drop back
       to `startProcess` and a hand-written `dup2`, which is what
       `SSHMaster::startMaster` did. */
    auto self = getSelfExe();
    ASSERT_TRUE(self);

    Pipe out;
    out.create();

    Pid pid = startProgram(
        RunOptions{
            .program = *self,
            .lookupPath = false,
            .args = {OS_STR("__util_test_spawn_trivial")},
            .standardOutFd = out.writeSide.get(),
        },
        nullptr);

    /* Drop our copy, or the read below never sees EOF. */
    out.writeSide.close();

    StringSink sink;
    drainFD(out.readSide.get(), sink);

    ASSERT_EQ(sink.s, "hello");
    ASSERT_TRUE(statusOk(pid.wait()));
}

TEST(startProgram, rejectsBothStandardOutAndStandardOutFd)
{
    /* The child has one stdout, and the two options disagree about who owns
       it. Honouring either silently would discard the other's intent. */
    StringSink sink;
    ASSERT_THROW(
        {
            runProgram2({
                .program = "/nonexistent",
                .standardOut = &sink,
                .standardOutFd = STDOUT_FILENO + 30,
            });
        },
        UsageError);
}

#endif

#undef NIX_EXECUTABLE_EXTENSION
#undef NIX_SPAWN_EXCEPTION

} // namespace nix
