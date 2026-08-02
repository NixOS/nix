#include "nix/util/processes.hh"
#include "nix/util/current-process.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

/* TODO: Test a bunch more things. */

#undef NIX_EXECUTABLE_EXTENSION
#undef NIX_SPAWN_EXCEPTION

} // namespace nix
