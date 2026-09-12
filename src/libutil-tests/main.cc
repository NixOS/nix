#include "nix/util/environment-variables.hh"
#include "nix/util/strings.hh"

#include <gtest/gtest.h>

#include <iostream>
#include <ranges>
#include <cstdlib>
#include <cerrno>
#include <filesystem>

#ifndef _WIN32
#  include <unistd.h>
#endif

static int spawnTrivialMain()
{
    std::cout << "hello";
    return EXIT_SUCCESS;
}

/**
 * Exit with the status given as the next argument, so a test can assert on
 * `ExecError::status` rather than only on the fact that something threw.
 */
static int spawnExitCodeMain(int argc, char ** argv)
{
    return argc > 2 ? std::atoi(argv[2]) : EXIT_SUCCESS;
}

/**
 * Write distinguishable text to each of stdout and stderr, so a test can tell
 * whether `mergeStderrToStdout` actually merged them.
 */
static int spawnStreamsMain()
{
    std::cout << "out:" << std::flush;
    std::cerr << "err:" << std::flush;
    return EXIT_SUCCESS;
}

/**
 * Print the value of the environment variable named by the next argument, or
 * `<unset>`. Distinguishes "environment was replaced" from "environment was
 * inherited", which `RunOptions::environment` promises.
 */
static int spawnPrintEnvMain(int argc, char ** argv)
{
    auto value = nix::getEnv(argc > 2 ? argv[2] : "");
    std::cout << value.value_or("<unset>");
    return EXIT_SUCCESS;
}

/** Print the working directory, to observe `RunOptions::chdir`. */
static int spawnPrintCwdMain()
{
    std::cout << std::filesystem::current_path().string();
    return EXIT_SUCCESS;
}

#ifndef _WIN32

/** Print `argv[0]`, to observe `RunOptions::argv0` (which is Unix-only). */
static int spawnPrintArgv0Main(char ** argv)
{
    std::cout << argv[0];
    return EXIT_SUCCESS;
}

#endif

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

#ifndef _WIN32

/**
 * Write to each descriptor named in `NIX_CHILD_WRITE_FDS` (as `fd:text`
 * pairs) and confirm every descriptor named in `NIX_CHILD_FDS_SHOULD_BE_CLOSED`
 * is closed.
 *
 * This is the observation point for `RunOptions::Redirection`. Both halves
 * matter: the writes prove the redirection survived until `exec`, and the
 * closed-check proves exempting the targets did not accidentally leak the
 * sources or anything else above stderr.
 */
static int spawnRedirectionsMain()
{
    for (const auto & pair :
         nix::splitString<std::vector<std::string>>(nix::getEnv("NIX_CHILD_WRITE_FDS").value_or(""), ",")) {
        if (pair.empty())
            continue;
        auto colon = pair.find(':');
        if (colon == std::string::npos)
            return EXIT_FAILURE;
        int fd = std::stoi(pair.substr(0, colon));
        auto text = pair.substr(colon + 1);
        if (::write(fd, text.data(), text.size()) != static_cast<ssize_t>(text.size()))
            return EXIT_FAILURE;
    }

    for (const auto & s :
         nix::splitString<std::vector<std::string>>(nix::getEnv("NIX_CHILD_FDS_SHOULD_BE_CLOSED").value_or(""), ",")) {
        if (s.empty())
            continue;
        if (::close(std::stoi(s)) != -1 || errno != EBADF)
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
        } else if (argv1 == "__util_test_spawn_exit_code") {
            return spawnExitCodeMain(argc, argv);
        } else if (argv1 == "__util_test_spawn_streams") {
            return spawnStreamsMain();
        } else if (argv1 == "__util_test_spawn_print_env") {
            return spawnPrintEnvMain(argc, argv);
        } else if (argv1 == "__util_test_spawn_print_cwd") {
            return spawnPrintCwdMain();
        } else if (argv1 == "__util_test_spawn_print_argv0") {
#ifndef _WIN32
            return spawnPrintArgv0Main(argv);
#endif
        } else if (argv1 == "__util_test_spawn_leaked_fds") {
#ifndef _WIN32
            return spawnTestForLeakedFDsMain();
#endif
        } else if (argv1 == "__util_test_spawn_redirections") {
#ifndef _WIN32
            return spawnRedirectionsMain();
#endif
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
