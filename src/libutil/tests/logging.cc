#include "logging.hh"
#include "nixexpr.hh"
#include "util.hh"
#include <fstream>

#include <gtest/gtest.h>

namespace nix {

    /* ----------------------------------------------------------------------------
     * logEI
     * --------------------------------------------------------------------------*/

    const char *test_file =
        "previous line of code\n"
        "this is the problem line of code\n"
        "next line of code\n";
    const char *one_liner =
        "this is the other problem line of code";

    TEST(logEI, catpuresBasicProperties) {

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
                .description = "error without any code lines.",
                .hint = hintfmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foFile, problem_file, 02, 13)
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- SysError --- error-unit-test\x1B[0m\nopening file '\x1B[33;1mrandom.nix\x1B[0m': \x1B[33;1mNo such file or directory\x1B[0m\n@nix {\"action\":\"msg\",\"column\":13,\"file\":\"random.nix\",\"level\":0,\"line\":2,\"msg\":\"\\u001b[31;1merror:\\u001b[0m\\u001b[34;1m --- error name --- error-unit-test\\u001b[0m\\n\\u001b[34;1mat: \\u001b[33;1m(2:13)\\u001b[34;1m in file: \\u001b[0mrandom.nix\\n\\nerror without any code lines.\\n\\nthis hint has \\u001b[33;1myellow\\u001b[0m templated \\u001b[33;1mvalues\\u001b[0m!!\",\"raw_msg\":\"this hint has \\u001b[33;1myellow\\u001b[0m templated \\u001b[33;1mvalues\\u001b[0m!!\"}\n");
    }

    TEST(logEI, appendingHintsToPreviousError) {

        MakeError(TestError, Error);
        ErrorInfo::programName = std::optional("error-unit-test");

        try {
            auto e = Error("initial error");
            throw TestError(e.info());
        } catch (Error &e) {
            ErrorInfo ei = e.info();
            ei.hint = hintfmt("%s; subsequent error message.", normaltxt(e.info().hint ? e.info().hint->str() : ""));

            testing::internal::CaptureStderr();
            logger->logEI(ei);
            auto str = testing::internal::GetCapturedStderr();

            ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- TestError --- error-unit-test\x1B[0m\ninitial error; subsequent error message.\n");
        }

    }

    TEST(logEI, picksUpSysErrorExitCode) {

        MakeError(TestError, Error);
        ErrorInfo::programName = std::optional("error-unit-test");

        try {
            auto x = readFile(-1);
        }
        catch (SysError &e) {
            testing::internal::CaptureStderr();
            logError(e.info());
            auto str = testing::internal::GetCapturedStderr();

            ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- SysError --- error-unit-test\x1B[0m\nstatting file: \x1B[33;1mBad file descriptor\x1B[0m\n");
        }
    }

    TEST(logEI, loggingErrorOnInfoLevel) {
        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlInfo,
                        .name = "Info name",
                        .description = "Info description",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1minfo:\x1B[0m\x1B[34;1m --- Info name --- error-unit-test\x1B[0m\nInfo description\n");
    }

    TEST(logEI, loggingErrorOnTalkativeLevel) {
        verbosity = lvlTalkative;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlTalkative,
                        .name = "Talkative name",
                        .description = "Talkative description",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1mtalk:\x1B[0m\x1B[34;1m --- Talkative name --- error-unit-test\x1B[0m\nTalkative description\n");
    }

    TEST(logEI, loggingErrorOnChattyLevel) {
        verbosity = lvlChatty;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlChatty,
                        .name = "Chatty name",
                        .description = "Talkative description",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[32;1mchat:\x1B[0m\x1B[34;1m --- Chatty name --- error-unit-test\x1B[0m\nTalkative description\n");
    }

    TEST(logEI, loggingErrorOnDebugLevel) {
        verbosity = lvlDebug;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlDebug,
                        .name = "Debug name",
                        .description = "Debug description",
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[33;1mdebug:\x1B[0m\x1B[34;1m --- Debug name --- error-unit-test\x1B[0m\nDebug description\n");
    }

    TEST(logEI, loggingErrorOnVomitLevel) {
        verbosity = lvlVomit;

        testing::internal::CaptureStderr();

        logger->logEI({ .level = lvlVomit,
                        .name = "Vomit name",
                        .description = "Vomit description",
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
                .description = "error description",
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
                .description = "error with code lines",
                .hint = hintfmt("this hint has %1% templated %2%!!",
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
                .description = "error without any code lines.",
                .hint = hintfmt("this hint has %1% templated %2%!!",
                    "yellow",
                    "values"),
                .errPos = Pos(foFile, problem_file, 02, 13)
            });

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- SysError --- error-unit-test\x1B[0m\nopening file '\x1B[33;1minvalid filename\x1B[0m': \x1B[33;1mNo such file or directory\x1B[0m\n\x1B[31;1merror:\x1B[0m\x1B[34;1m --- error name --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m in file: \x1B[0minvalid filename\n\nerror without any code lines.\n\nthis hint has \x1B[33;1myellow\x1B[0m templated \x1B[33;1mvalues\x1B[0m!!\n");
    }

    TEST(logError, logErrorWithOnlyHintAndName) {
        testing::internal::CaptureStderr();

        logError({
                .name = "error name",
                .hint = hintfmt("hint %1%", "only"),
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
                .description = "warning description",
                .hint = hintfmt("there was a %1%", "warning"),
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
                .description = "warning description",
                .hint = hintfmt("this hint has %1% templated %2%!!",
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
                .description = "show-traces",
                .hint = hintfmt("it has been %1% days since our last error", "zero"),
                .errPos = Pos(foString, problem_file, 2, 13),
            });

        e.addTrace(Pos(foStdin, oneliner_file, 1, 19), "while trying to compute %1%", 42);
        e.addTrace(std::nullopt, "while doing something without a %1%", "pos");
        e.addTrace(Pos(foFile, invalidfilename, 100, 1), "missing %s", "nix file");

        testing::internal::CaptureStderr();

        loggerSettings.showTrace.assign(true);

        logError(e.info());

        auto str = testing::internal::GetCapturedStderr();
        ASSERT_STREQ(str.c_str(), "\x1B[31;1merror:\x1B[0m\x1B[34;1m --- SysError --- error-unit-test\x1B[0m\nopening file '\x1B[33;1minvalid filename\x1B[0m': \x1B[33;1mNo such file or directory\x1B[0m\n\x1B[31;1merror:\x1B[0m\x1B[34;1m --- AssertionError --- error-unit-test\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(2:13)\x1B[34;1m from string\x1B[0m\n\nshow-traces\n\n     1| previous line of code\n     2| this is the problem line of code\n      |             \x1B[31;1m^\x1B[0m\n     3| next line of code\n\nit has been \x1B[33;1mzero\x1B[0m days since our last error\n\x1B[34;1m---- show-trace ----\x1B[0m\n\x1B[34;1mtrace: \x1B[0mwhile trying to compute \x1B[33;1m42\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(1:19)\x1B[34;1m from stdin\x1B[0m\n\n     1| this is the other problem line of code\n      |                   \x1B[31;1m^\x1B[0m\n\n\x1B[34;1mtrace: \x1B[0mwhile doing something without a \x1B[33;1mpos\x1B[0m\n\x1B[34;1mtrace: \x1B[0mmissing \x1B[33;1mnix file\x1B[0m\n\x1B[34;1mat: \x1B[33;1m(100:1)\x1B[34;1m in file: \x1B[0minvalid filename\n");
    }

    TEST(addTrace, hideTracesWithoutShowTrace) {
        SymbolTable testTable;
        auto problem_file = testTable.create(test_file);
        auto oneliner_file = testTable.create(one_liner);
        auto invalidfilename = testTable.create("invalid filename");

        auto e = AssertionError(ErrorInfo {
                .name = "wat",
                .description = "hide traces",
                .hint = hintfmt("it has been %1% days since our last error", "zero"),
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
     * hintfmt
     * --------------------------------------------------------------------------*/

    TEST(hintfmt, percentStringWithoutArgs) {

        const char *teststr = "this is 100%s correct!";

        ASSERT_STREQ(
            hintfmt(teststr).str().c_str(),
            teststr);

    }

    TEST(hintfmt, fmtToHintfmt) {

        ASSERT_STREQ(
            hintfmt(fmt("the color of this this text is %1%", "not yellow")).str().c_str(),
            "the color of this this text is not yellow");

    }

    TEST(hintfmt, tooFewArguments) {

        ASSERT_STREQ(
            hintfmt("only one arg %1% %2%", "fulfilled").str().c_str(),
            "only one arg " ANSI_YELLOW "fulfilled" ANSI_NORMAL " ");

    }

    TEST(hintfmt, tooManyArguments) {

        ASSERT_STREQ(
            hintfmt("what about this %1% %2%", "%3%", "one", "two").str().c_str(),
            "what about this " ANSI_YELLOW "%3%" ANSI_NORMAL " " ANSI_YELLOW "one" ANSI_NORMAL);

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

}
