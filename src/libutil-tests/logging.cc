#include "nix/util/logging.hh"
#include "nix/util/tests/characterization.hh"
#include "nix/util/serialise.hh"
#include "nix/util/tests/capture-logging.hh"

#include <gtest/gtest.h>

namespace nix {

#if 0

    /* ----------------------------------------------------------------------------
     * logEI
     * --------------------------------------------------------------------------*/

    const char *test_file =
        "previous line of code\n"
        "this is the problem line of code\n"
        "next line of code\n";
    const char *one_liner =
        "this is the other problem line of code";

    TEST(logEI, capturesBasicProperties) {

        MakeError(TestError, Error);
        ErrorInfo::programName = std::optional("error-unit-test");

        try {
            throw TestError("an error for testing purposes");
        } catch (Error &e) {
            testing::internal::CaptureStderr();
            logger->logEI(e.info());
            auto str = testing::internal::GetCapturedStderr();

            ASSERT_STREQ(str.c_str(),"\x1B[31;1merror:\x1B[0m\x1B[34;1m --- TestError --- error-unit-test\x1B[0m\nan error for testing purposes\n");
        }
    }

    TEST(logEI, jsonOutput) {
        SymbolTable testTable;
        auto problem_file = testTable.create("random.nix");
        testing::internal::CaptureStderr();

        makeJSONLogger(*logger)->logEI({
                .name = "error name",
                .msg = HintFmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foFile, problem_file, 02, 13)
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "@nix {\"action\":\"msg\",\"column\":13,\"file\":\"random.nix\",\"level\":0,\"line\":2,\"msg\":\"\\u001b[31;1merror:\\u001b[0m\\u001b[34;1m --- error name --- error-unit-test\\u001b[0m\\n\\u001b[34;1mat: \\u001b[33;1m(2:13)\\u001b[34;1m in file: \\u001b[0mrandom.nix\\n\\nerror without any code lines.\\n\\nthis hint has \\u001b[33;1myellow\\u001b[0m templated \\u001b[33;1mvalues\\u001b[0m!!\",\"raw_msg\":\"this hint has \\u001b[33;1myellow\\u001b[0m templated \\u001b[33;1mvalues\\u001b[0m!!\"}\n");
    }

    TEST(logEI, appendingHintsToPreviousError) {

        MakeError(TestError, Error);
        ErrorInfo::programName = std::optional("error-unit-test");

        try {
            auto e = Error("initial error");
            throw TestError(e.info());
        } catch (Error &e) {
            ErrorInfo ei = e.info();
            ei.msg = HintFmt("%s; subsequent error message.", Uncolored(e.info().msg.str()));

            testing::internal::CaptureStderr();
            logger->logEI(ei);
            auto str = testing::internal::GetCapturedStderr();

            ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- TestError --- error-unit-test\x1B[0m\ninitial error; subsequent error message.\n");
        }

    }

    TEST(logEI, picksUpSystemErrorExitCode) {

        MakeError(TestError, Error);
        ErrorInfo::programName = std::optional("error-unit-test");

        try {
            auto x = readFile(-1);
        }
        catch (SystemError &e) {
            testing::internal::CaptureStderr();
            logError(e.info());
            auto str = testing::internal::GetCapturedStderr();

            ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- SystemError --- error-unit-test\x1B[0m\nstatting file: \x1B[33;1mBad file descriptor\x1B[0m\n");
        }
    }

    TEST(logEI, loggingErrorOnInfoLevel) {
        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlInfo,
                        .name = "Info name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1minfo:\x1B[0m\x1B[34;1m --- Info name --- error-unit-test\x1B[0m\nInfo description\n");
    }

    TEST(logEI, loggingErrorOnTalkativeLevel) {
        verbosity = lvlTalkative;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlTalkative,
                        .name = "Talkative name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1mtalk:\x1B[0m\x1B[34;1m --- Talkative name --- error-unit-test\x1B[0m\nTalkative description\n");
    }

    TEST(logEI, loggingErrorOnChattyLevel) {
        verbosity = lvlChatty;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlChatty,
                        .name = "Chatty name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1mchat:\x1B[0m\x1B[34;1m --- Chatty name --- error-unit-test\x1B[0m\nTalkative description\n");
    }

    TEST(logEI, loggingErrorOnDebugLevel) {
        verbosity = lvlDebug;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlDebug,
                        .name = "Debug name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[33;1mdebug:\x1B[0m\x1B[34;1m --- Debug name --- error-unit-test\x1B[0m\nDebug description\n");
    }

    TEST(logEI, loggingErrorOnVomitLevel) {
        verbosity = lvlVomit;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlVomit,
                        .name = "Vomit name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1mvomit:\x1B[0m\x1B[34;1m --- Vomit name --- error-unit-test\x1B[0m\nVomit description\n");
    }

    /* ----------------------------------------------------------------------------
     * logError
     * --------------------------------------------------------------------------*/

    TEST(logError, logErrorWithoutHintOrCode) {
        testing::internal::CaptureStderr();

        logError({
                .name = "name",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- name --- error-unit-test\x1B[0m\nerror description\n");
    }

    TEST(logError, logErrorWithPreviousAndNextLinesOfCode) {
        SymbolTable testTable;
        auto problem_file = testTable.create(test_file);

        testing::internal::CaptureStderr();

        logError({
                .name = "error name",
                .msg = HintFmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foString, problem_file, 02, 13),
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- error name --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m from string\x1B[0m\n\nerror with code lines\n\n     1| previous line of code\n     2| this is the problem line of code\n      |             \x1B[31;1m^\x1B[0m\n     3| next line of code\n\nthis hint has \x1B[33;1myellow\x1B[0m templated \x1B[33;1mvalues\x1B[0m!!\n");
    }

    TEST(logError, logErrorWithInvalidFile) {
        SymbolTable testTable;
        auto problem_file = testTable.create("invalid filename");
        testing::internal::CaptureStderr();

        logError({
                .name = "error name",
                .msg = HintFmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foFile, problem_file, 02, 13)
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- error name --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m in file: \x1B[0minvalid filename\n\nerror without any code lines.\n\nthis hint has \x1B[33;1myellow\x1B[0m templated \x1B[33;1mvalues\x1B[0m!!\n");
    }

    TEST(logError, logErrorWithOnlyHintAndName) {
        testing::internal::CaptureStderr();

        logError({
                .name = "error name",
                .msg = HintFmt("hint %1%", "only"),
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- error name --- error-unit-test\x1B[0m\nhint \x1B[33;1monly\x1B[0m\n");

    }

    /* ----------------------------------------------------------------------------
     * logWarning
     * --------------------------------------------------------------------------*/

    TEST(logWarning, logWarningWithNameDescriptionAndHint) {
        testing::internal::CaptureStderr();

        logWarning({
                .name = "name",
                .msg = HintFmt("there was a %1%", "warning"),
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[33;1mwarning:\x1B[0m\x1B[34;1m --- name --- error-unit-test\x1B[0m\nwarning description\n\nthere was a \x1B[33;1mwarning\x1B[0m\n");
    }

    TEST(logWarning, logWarningWithFileLineNumAndCode) {

        SymbolTable testTable;
        auto problem_file = testTable.create(test_file);

        testing::internal::CaptureStderr();

        logWarning({
                .name = "warning name",
                .msg = HintFmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foStdin, problem_file, 2, 13),
            });


        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[33;1mwarning:\x1B[0m\x1B[34;1m --- warning name --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m from stdin\x1B[0m\n\nwarning description\n\n     1| previous line of code\n     2| this is the problem line of code\n      |             \x1B[31;1m^\x1B[0m\n     3| next line of code\n\nthis hint has \x1B[33;1myellow\x1B[0m templated \x1B[33;1mvalues\x1B[0m!!\n");
    }

    /* ----------------------------------------------------------------------------
     * traces
     * --------------------------------------------------------------------------*/

    TEST(addTrace, showTracesWithShowTrace) {
        SymbolTable testTable;
        auto problem_file = testTable.create(test_file);
        auto oneliner_file = testTable.create(one_liner);
        auto invalidfilename = testTable.create("invalid filename");

        auto e = AssertionError(ErrorInfo {
                .name = "wat",
                .msg = HintFmt("it has been %1% days since our last error", "zero"),
                .errPos = Pos(foString, problem_file, 2, 13),
            });

        e.addTrace(Pos(foStdin, oneliner_file, 1, 19), "while trying to compute %1%", 42);
        e.addTrace(std::nullopt, "while doing something without a %1%", "pos");
        e.addTrace(Pos(foFile, invalidfilename, 100, 1), "missing %s", "nix file");

        testing::internal::CaptureStderr();

        loggerSettings.showTrace.assign(true);

        logError(e.info());

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- AssertionError --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m from string\x1B[0m\n\nshow-traces\n\n     1| previous line of code\n     2| this is the problem line of code\n      |             \x1B[31;1m^\x1B[0m\n     3| next line of code\n\nit has been \x1B[33;1mzero\x1B[0m days since our last error\n\x1B[34;1m---- show-trace ----\x1B[0m\n\x1B[34;1mtrace: \x1B[0mwhile trying to compute \x1B[33;1m42\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(1:19)\x1B[34;1m from stdin\x1B[0m\n\n     1| this is the other problem line of code\n      |                   \x1B[31;1m^\x1B[0m\n\n\x1B[34;1mtrace: \x1B[0mwhile doing something without a \x1B[33;1mpos\x1B[0m\n\x1B[34;1mtrace: \x1B[0mmissing \x1B[33;1mnix file\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(100:1)\x1B[34;1m in file: \x1B[0minvalid filename\n");
    }

    TEST(addTrace, hideTracesWithoutShowTrace) {
        SymbolTable testTable;
        auto problem_file = testTable.create(test_file);
        auto oneliner_file = testTable.create(one_liner);
        auto invalidfilename = testTable.create("invalid filename");

        auto e = AssertionError(ErrorInfo {
                .name = "wat",
                .msg = HintFmt("it has been %1% days since our last error", "zero"),
                .errPos = Pos(foString, problem_file, 2, 13),
            });

        e.addTrace(Pos(foStdin, oneliner_file, 1, 19), "while trying to compute %1%", 42);
        e.addTrace(std::nullopt, "while doing something without a %1%", "pos");
        e.addTrace(Pos(foFile, invalidfilename, 100, 1), "missing %s", "nix file");

        testing::internal::CaptureStderr();

        loggerSettings.showTrace.assign(false);

        logError(e.info());

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- AssertionError --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m from string\x1B[0m\n\nhide traces\n\n     1| previous line of code\n     2| this is the problem line of code\n      |             \x1B[31;1m^\x1B[0m\n     3| next line of code\n\nit has been \x1B[33;1mzero\x1B[0m days since our last error\n");
    }


    /* ----------------------------------------------------------------------------
     * HintFmt
     * --------------------------------------------------------------------------*/

    TEST(HintFmt, percentStringWithoutArgs) {

        const char *teststr = "this is 100%s correct!";

        ASSERT_STREQ(
            HintFmt(teststr).str().c_str(),
            teststr);

    }

    TEST(HintFmt, fmtToHintfmt) {

        ASSERT_STREQ(
            HintFmt(fmt("the color of this this text is %1%", "not yellow")).str().c_str(),
            "the color of this this text is not yellow");

    }

    TEST(HintFmt, tooFewArguments) {

        ASSERT_STREQ(
            HintFmt("only one arg %1% %2%", "fulfilled").str().c_str(),
            "only one arg " ANSI_WARNING "fulfilled" ANSI_NORMAL " ");

    }

    TEST(HintFmt, tooManyArguments) {

        ASSERT_STREQ(
            HintFmt("what about this %1% %2%", "%3%", "one", "two").str().c_str(),
            "what about this " ANSI_WARNING "%3%" ANSI_NORMAL " " ANSI_YELLOW "one" ANSI_NORMAL);

    }

    /* ----------------------------------------------------------------------------
     * ErrPos
     * --------------------------------------------------------------------------*/

    TEST(errpos, invalidPos) {

      // contains an invalid symbol, which we should not dereference!
      Pos invalid;

      // constructing without access violation.
      ErrPos ep(invalid);

      // assignment without access violation.
      ep = invalid;

    }
#endif

namespace {

class DeterministicActivityIdLogger : public Logger
{
    std::unique_ptr<Logger> inner;
    ActivityId currentId = 1;
    /* Remap activities to be deterministic. */
    std::map<ActivityId, ActivityId> actIdMap;

    ActivityId remapActivityId(ActivityId act)
    {
        auto [it, inserted] = actIdMap.emplace(act, currentId);
        if (inserted)
            ++currentId;
        return it->second;
    }

public:
    DeterministicActivityIdLogger(std::unique_ptr<Logger> inner)
        : inner(std::move(inner))
    {
        actIdMap.insert({0, 0}); /* 0 stands for "no activity". */
    }

    void stop() override
    {
        inner->stop();
    }

    void pause() override
    {
        inner->pause();
    }

    void resume() override
    {
        inner->resume();
    }

    bool isVerbose() override
    {
        return inner->isVerbose();
    }

    void log(Verbosity lvl, std::string_view s) noexcept override
    {
        inner->log(lvl, s);
    }

    void logEI(const ErrorInfo & ei) noexcept override
    {
        inner->logEI(ei);
    }

    void warn(const std::string & msg) noexcept override
    {
        inner->warn(msg);
    }

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        std::span<const Field> fields,
        ActivityId parent) noexcept override
    {
        act = remapActivityId(act);
        parent = remapActivityId(parent);
        inner->startActivity(act, lvl, type, s, fields, parent);
    }

    void stopActivity(ActivityId act) noexcept override
    {
        inner->stopActivity(remapActivityId(act));
    }

    void result(ActivityId act, ResultType type, std::span<const Field> fields) noexcept override
    {
        inner->result(remapActivityId(act), type, fields);
    }

    void result(ActivityId act, ResultType type, const nlohmann::json & json) noexcept override
    {
        inner->result(remapActivityId(act), type, json);
    }

    /* Functions below aren't supposed to be execrised and thus are marked unreachble to catch cases
       when they are called accidentally. */

    void writeToStdout(std::string_view s) override
    {
        unreachable();
    }

    std::optional<char> ask(std::string_view s) override
    {
        unreachable();
    }

    void setPrintBuildLogs(bool printBuildLogs) override
    {
        unreachable();
    }
};

class JSONLogMessageCharacterisationTest : public CharacterizationTest,
                                           public ::testing::WithParamInterface<std::string_view>
{
    std::filesystem::path unitTestData = getUnitTestData() / "logging" / "handle-json-message";

protected:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_P(JSONLogMessageCharacterisationTest, writesExpectedLogs)
{
    auto testStem = GetParam();
    auto inputFd = openFileReadonly(goldenMaster(testStem));

    ASSERT_TRUE(inputFd) << "could not open input test file '" << testStem << "'";

    auto tempFile = createAnonymousTempFile();
    auto jsonLogger =
        std::make_unique<DeterministicActivityIdLogger>(makeJSONLogger(tempFile.get(), /*includeNixPrefix=*/true));
    Finally restoreLogger([oldLogger = logger] { logger = oldLogger; });
    logger = jsonLogger.get();

    Activity act(
        *logger,
        lvlVomit,
        actUnknown,
        /*s=*/"",
        /*fields=*/{},
        /*parent=*/0);

    std::map<ActivityId, Activity> activities;
    FdSource input(inputFd.get());

    while (true) {
        try {
            auto line = input.readLine();
            try {
                handleJSONLogMessage(line, act, activities, line, /*trusted=*/true);
            } catch (std::exception &) {
                /* FIXME: Invalid input shouldn't escape. */
            }
        } catch (EndOfFile &) {
            break;
        }
    }

    lseek(tempFile.get(), 0, SEEK_SET);

    writeTest(testStem + ".log", [expected = readFile(tempFile.get())]() -> std::string { return expected; });
}

INSTANTIATE_TEST_SUITE_P(
    JSONLogMessageCharacterisation, JSONLogMessageCharacterisationTest, ::testing::Values("garbage-in", "legitimate"));

} // namespace

} // namespace nix
