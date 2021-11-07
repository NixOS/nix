// based on: nix-instantiate --parse

#include "command.hh"
#include "eval.hh"

using namespace nix;

enum class OutputFormat {
    ATerm,
    XML,
    JSON,
};

struct CmdParse : Command
{
    OutputFormat outputFormat = OutputFormat::ATerm;
    std::string inputExpr;
    Path inputFile;

    CmdParse()
    {
        expectArgs({
            .label = "input-file",
            .optional = true,
            .handler = {&inputFile},
            .completer = completePath // libutil/args.hh
        });

        addFlag({
            .longName = "expr",
            //.shortName = "E", // FIXME error: cannot convert ‘<brace-enclosed initializer list>’ to ‘nix::Args::Flag&&’
            .description = "Nix expression",
            .labels = {"expression"},
            .handler = {[&](std::string exprString) {
                if (exprString.empty()) throw UsageError("--expr requires one argument");
                inputExpr = exprString;
                // TODO move string?
            }}
        });

        addFlag({
            .longName = "output-format",
            .description = "output format",
            .labels = {"format"},
            .handler = {[&](std::string formatName) {
                if (formatName.empty()) throw UsageError("--output-format requires one argument");
                if (formatName == "json") {
                    outputFormat = OutputFormat::JSON;
                    return;
                }
                if (formatName == "aterm") {
                    outputFormat = OutputFormat::ATerm;
                    return;
                }
                throw UsageError(fmt("--output-format: format not recognized: %1%", formatName));
            }}
        });
    }

    std::string description() override
    {
        return "parse a Nix expression";
    }

    std::string doc() override
    {
        return
          #include "parse.md"
          ;
    }

    Category category() override { return catUtility; }

    void run() override
    {
        RepairFlag repair = NoRepair;

        auto store = openStore();

        Strings searchPath = {"."}; // TODO better?

        auto state = std::make_unique<EvalState>(searchPath, store);
        state->repair = repair;

        Expr * e = nullptr;

        /*
        FIXME state->checkSourcePath throws
        "access to path '%1%' is forbidden in restricted mode" in eval.cc
        -> fix searchPath ?
        -> evalSettings.restrictEval = false ?
        -> evalSettings.pureEval = false ?

        how does nix-instantiate handle this?
        */

        //e = state->parseExprFromFile(resolveExprPath(state->checkSourcePath(lookupFileArg(*state, inputFile))));

        if ((inputExpr == "" && inputFile == "") || (inputExpr != "" && inputFile != "")) {
            // none or both are set
            // when none are set, nix-instantiate would read ./default.nix
            throw UsageError(fmt("'nix parse' requires either input-file or expression"));
        }

        if (inputFile != "") {
            e = state->parseExprFromFile(lookupFileArg(*state, inputFile));
        }
        else {
            e = state->parseExprFromString(inputExpr, absPath("."));
        }

        if (outputFormat == OutputFormat::JSON) {
            e->showAsJson(std::cout);
        }
        else {
            // default format
            e->showAsAterm(std::cout);
        }
    }
};

static auto rCmdParse = registerCommand<CmdParse>("parse");
