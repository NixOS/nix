#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <unistd.h>
#include <boost/algorithm/string/replace.hpp>

#include "test-session.hh"
#include "util.hh"
#include "file-system.hh"
#include "tests/characterization.hh"
#include "tests/cli-literate-parser.hh"

using namespace std::string_literals;

namespace nix {

static constexpr const char * REPL_PROMPT = "nix-repl> ";

// ASCII ENQ character
static constexpr const char * AUTOMATION_PROMPT = "\x05";

static std::string_view trimOutLog(std::string_view outLog)
{
    const std::string trailer = "\n"s + AUTOMATION_PROMPT;
    if (outLog.ends_with(trailer)) {
        outLog.remove_suffix(trailer.length());
    }
    return outLog;
}

class ReplSessionTest : public CharacterizationTest
{
    Path unitTestData = getUnitTestData();

public:
    Path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData + "/" + testStem;
    }

    void runReplTest(std::string_view const & content, std::vector<std::string> extraArgs = {}) const
    {
        auto syntax = CLILiterateParser::parse(REPL_PROMPT, content);

        Strings args{"--quiet", "repl", "--quiet", "--extra-experimental-features", "repl-automation"};
        args.insert(args.end(), extraArgs.begin(), extraArgs.end());

        // TODO: why the fuck does this need two --quiets
        auto process = RunningProcess::start("nix", args);
        auto session = TestSession{AUTOMATION_PROMPT, std::move(process)};

        for (auto & bit : syntax) {
            if (bit.kind != CLILiterateParser::NodeKind::COMMAND) {
                continue;
            }

            if (!session.waitForPrompt()) {
                ASSERT_TRUE(false);
            }
            session.runCommand(bit.text);
        }
        if (!session.waitForPrompt()) {
            ASSERT_TRUE(false);
        }
        session.close();

        auto replacedOutLog = boost::algorithm::replace_all_copy(session.outLog, unitTestData, "TEST_DATA");
        auto cleanedOutLog = trimOutLog(replacedOutLog);

        auto parsedOutLog = CLILiterateParser::parse(AUTOMATION_PROMPT, cleanedOutLog, 0);

        CLILiterateParser::tidyOutputForComparison(parsedOutLog);
        CLILiterateParser::tidyOutputForComparison(syntax);

        ASSERT_EQ(parsedOutLog, syntax);
    }
};

TEST_F(ReplSessionTest, parses)
{
    writeTest("basic.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto parser = CLILiterateParser{REPL_PROMPT};
        parser.feed(content);

        std::ostringstream out{};
        for (auto & bit : parser.syntax()) {
            out << bit.print() << "\n";
        }
        return out.str();
    });

    writeTest("basic_tidied.ast", [this]() {
        const std::string content = readFile(goldenMaster("basic.test"));
        auto syntax = CLILiterateParser::parse(REPL_PROMPT, content);

        CLILiterateParser::tidyOutputForComparison(syntax);

        std::ostringstream out{};
        for (auto & bit : syntax) {
            out << bit.print() << "\n";
        }
        return out.str();
    });
}

TEST_F(ReplSessionTest, repl_basic)
{
    readTest("basic_repl.test", [this](std::string input) { runReplTest(input); });
}

#define DEBUGGER_TEST(name) \
    TEST_F(ReplSessionTest, name) \
    { \
        readTest(#name ".test", [this](std::string input) { \
            runReplTest(input, {"--debugger", "-f", goldenMaster(#name ".nix")}); \
        }); \
    }

DEBUGGER_TEST(regression_9918);
DEBUGGER_TEST(regression_9917);
DEBUGGER_TEST(stack_vars);

};
