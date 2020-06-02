#include "logging.hh"
#include "nixexpr.hh"
#include "util.hh"

#include <iostream>
#include <optional>

using namespace nix;

MakeError(DemoError, Error);

int main()
{
    verbosity = lvlVomit;

    // In each program where errors occur, this has to be set.
    ErrorInfo::programName = std::optional("error-demo");

    // 'DemoError' appears as error name.
    try {
        throw DemoError("demo error was thrown");
    } catch (Error &e) {
        logger->logEI(e.info());
    }

    // appending to the hint from the previous error
    try {
        auto e = Error("initial error");
        throw DemoError(e.info());
    } catch (Error &e) {
        ErrorInfo ei = e.info();
        // using normaltxt to avoid the default yellow highlighting.
        ei.hint = hintfmt("%s; subsequent error message.", 
            normaltxt(e.info().hint ? e.info().hint->str() : ""));
        logger->logEI(ei);
    }

    // SysError; picks up errno
    try {
        auto x = readFile(-1);
    }
    catch (SysError &e) {
        std::cout << "errno was: " << e.errNo << std::endl;
        logError(e.info());
    }

    // current exception
    try {
        throw DemoError("DemoError handled as a %1%", "std::exception");
    }
    catch (...) {
        const std::exception_ptr &eptr = std::current_exception();
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
    }

    // For completeness sake, show 'info' through 'vomit' levels.
    // But this is maybe a heavy format for those.
    logger->logEI({ .level = lvlInfo,
                    .name = "Info name",
                    .description = "Info description",
        });

    logger->logEI({ .level = lvlTalkative,
                    .name = "Talkative name",
                    .description = "Talkative description",
        });

    logger->logEI({ .level = lvlChatty,
                    .name = "Chatty name",
                    .description = "Chatty description",
        });

    logger->logEI({ .level = lvlDebug,
                    .name = "Debug name",
                    .description = "Debug description",
        });

    logger->logEI({ .level = lvlVomit,
                    .name = "Vomit name",
                    .description = "Vomit description",
        });

    // Error in a program; no hint and no nix code.
    logError({ 
        .name = "name",
        .description = "error description",
    });

    // Warning with name, description, and hint.
    // The hintfmt function makes all the substituted text yellow.
    logWarning({ 
        .name = "name",
        .description = "error description",
        .hint = hintfmt("there was a %1%", "warning"),
    });

    // Warning with nix file, line number, column, and the lines of
    // code where a warning occurred.
    SymbolTable testTable;
    auto problem_file = testTable.create("myfile.nix");

    logWarning({ 
        .name = "warning name",
        .description = "warning description",
        .hint = hintfmt("this hint has %1% templated %2%!!",
            "yellow",
            "values"),
        .nixCode = NixCode {
            .errPos = Pos(problem_file, 40, 13),
            .prevLineOfCode = std::nullopt,
            .errLineOfCode = "this is the problem line of code",
            .nextLineOfCode = std::nullopt
    }});

    // Error with previous and next lines of code.
    logError({ 
        .name = "error name",
        .description = "error with code lines",
        .hint = hintfmt("this hint has %1% templated %2%!!",
            "yellow",
            "values"),
        .nixCode = NixCode {
            .errPos = Pos(problem_file, 40, 13),
            .prevLineOfCode = "previous line of code",
            .errLineOfCode = "this is the problem line of code",
            .nextLineOfCode = "next line of code",
    }});


    // Error without lines of code.
    logError({ 
        .name = "error name",
        .description = "error without any code lines.",
        .hint = hintfmt("this hint has %1% templated %2%!!",
            "yellow",
            "values"),
        .nixCode = NixCode {
            .errPos = Pos(problem_file, 40, 13)
    }});

    // Error with only hint and name..
    logError({ 
        .name = "error name",
        .hint = hintfmt("hint %1%", "only"),
        .nixCode = NixCode {
            .errPos = Pos(problem_file, 40, 13)
    }});

    return 0;
}
