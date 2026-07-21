#include "nix/util/processes.hh"
#include "nix/util/logging.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/finally.hh"

#include <gtest/gtest.h>

#ifndef _WIN32
#  include <unistd.h>
#endif

namespace nix {

/* ----------------------------------------------------------------------------
 * statusOk
 * --------------------------------------------------------------------------*/

TEST(statusOk, zeroIsOk)
{
    ASSERT_EQ(statusOk(0), true);
    ASSERT_EQ(statusOk(1), false);
}

#ifndef _WIN32
/* ----------------------------------------------------------------------------
 * startProcess -- forked children inherit the parent's log format
 * --------------------------------------------------------------------------*/

TEST(startProcess, forkedChildPreservesLogFormat)
{
    /* Regression test: `startProcess()` used to unconditionally reset
       the child's logger to a plain `SimpleLogger`, discarding
       whatever format (e.g. JSON) the parent had configured. It should
       instead clone the parent's logger via `Logger::cloneForChild()`. */
    auto prevLogger = logger;
    Finally restoreLogger([&] { logger = prevLogger; });

    Pipe pipe;
    pipe.create();

    logger = makeJSONLogger(pipe.writeSide.get(), /*includeNixPrefix=*/true).release();

    ProcessOptions options;
    options.allowVfork = false;

    Pid pid = startProcess(
        [&] {
            logger->log(lvlInfo, "hello from child");
            _exit(0);
        },
        options);

    pipe.writeSide.close();
    int status = pid.wait();
    ASSERT_TRUE(statusOk(status));

    auto output = readFile(pipe.readSide.get());
    ASSERT_TRUE(output.starts_with("@nix "));
    ASSERT_NE(output.find("hello from child"), std::string::npos);
}
#endif

} // namespace nix
