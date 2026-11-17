#include "nix/util/environment-variables.hh"
#include "nix/util/strings.hh"

#include <gtest/gtest.h>

#include <iostream>
#include <filesystem>
#include <ranges>
#include <cstdlib>
#include <cerrno>

#ifndef _WIN32
#  include <signal.h>
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/param.h>
#endif

static int spawnTrivialMain()
{
    std::cout << "hello";
    return EXIT_SUCCESS;
}

static int spawnWriteToStdoutAndStderr()
{
    std::cout << "hi" << std::endl;
    std::cerr << "there" << std::endl;
    return EXIT_SUCCESS;
}

#ifndef _WIN32

static int spawnTestForLeakedFDsMain()
{
    /* The parent will open a file descriptor without O_CLOEXEC and we
       check if it's still open in the child. */
    auto shouldBeClosed =
        nix::splitString<std::vector<std::string>>(nix::getEnv("NIX_CHILD_FDS_SHOULD_BE_CLOSED").value(), ",")
        | std::views::transform([](const std::string & s) { return std::stoi(s); });

    for (auto fd : shouldBeClosed) {
        if (::close(fd) != -1 || errno != EBADF)
            return EXIT_FAILURE;
    }

    /* stdin, stdout and stderr should not be closed. */
    for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (::close(fd) == -1)
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int spawnTestArgv0AndChdir(std::string_view argv0, std::string_view argv2)
{
    if (std::filesystem::current_path().native() != argv2)
        return EXIT_FAILURE;

    if (argv0 != "test")
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int spawnTestSigMask()
{
    sigset_t current;
    if (::sigprocmask(SIG_SETMASK, nullptr, &current))
        return EXIT_SUCCESS;

    if (sigismember(&current, SIGPIPE))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int spawnTestSetuidSetgid()
{
    if (::geteuid() != 1 || ::getegid() != 1 || ::getuid() != 1 || ::getgid() != 1)
        return EXIT_FAILURE;

    int grn = ::getgroups(0, nullptr);
    switch (grn) {
    case 0:
        /* It's not specified whether egid is included in the getgroups. */
        return EXIT_SUCCESS;
    case 1: {
        gid_t groups[1] = {};
        if (::getgroups(1, groups) == -1 || groups[0] != ::getegid())
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    default:
        return EXIT_FAILURE;
    }
}

#endif

int main(int argc, char ** argv)
{
    /* This will get re-execed into from libutil-tests/processes.cc. */
    if (argc > 1) {
        auto argv1 = std::string_view(argv[1]);

        if (argv1 == "__util_test_spawn_trivial") {
            return spawnTrivialMain();
        } else if (argv1 == "__util_test_spawn_write_to_stdout_and_stderr") {
            return spawnWriteToStdoutAndStderr();
        }

#ifndef _WIN32
        if (argv1 == "__util_test_spawn_leaked_fds") {
            return spawnTestForLeakedFDsMain();
        } else if (argv1 == "__util_test_spawn_chdir_and_argv0" && argc > 2) {
            return spawnTestArgv0AndChdir(argv[0], argv[2]);
        } else if (argv1 == "__util_test_spawn_sigmask") {
            return spawnTestSigMask();
        } else if (argv1 == "__util_test_spawn_setuid_setgid") {
            return spawnTestSetuidSetgid();
        }
#endif
    }

    // Preserve environment filters; command-line flags are parsed below.
    if (!nix::getEnvOs(OS_STR("GTEST_FILTER")).has_value())
        GTEST_FLAG_SET(filter, "-Root*");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
