#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <optional>
#include <unistd.h>

#include "tests/characterization.hh"
#include "tests/cli-literate-parser.hh"

using namespace std::string_literals;

namespace nix {

static constexpr const char * REPL_PROMPT = "nix-repl> ";

class ReplSessionTest : public CharacterizationTest
{
    Path unitTestData = getUnitTestData();

public:
    Path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData + "/" + testStem;
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
}
};
