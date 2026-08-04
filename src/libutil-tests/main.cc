#include "nix/util/environment-variables.hh"
#include "nix/util/strings.hh"

#include <gtest/gtest.h>

#include <iostream>
#include <ranges>
#include <cstdlib>
#include <cerrno>

#ifndef _WIN32
#  include <unistd.h>
#endif

static int spawnTrivialMain()
{
    std::cout << "hello";
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

#endif

int main(int argc, char ** argv)
{
    /* This will get re-execed into from libutil-tests/processes.cc. */
    if (argc > 1) {
        auto argv1 = std::string_view(argv[1]);

        if (argv1 == "__util_test_spawn_trivial") {
            return spawnTrivialMain();
        } else if (argv1 == "__util_test_spawn_leaked_fds") {
#ifndef _WIN32
            return spawnTestForLeakedFDsMain();
#endif
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
