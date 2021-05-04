// based on: nix-instantiate --parse

#include "command.hh"
#include "eval.hh"

using namespace nix;

enum OutputFormat {
    ATerm,
    XML,
    JSON,
};

struct CmdParse : Command
{
    uint outputFormat = OutputFormat::ATerm;
    Path inputFile;

    CmdParse()
    {
        expectArgs({
            .label = "input-file",
            .handler = {&inputFile},
            .completer = completePath // ./src/libutil/args.hh:232
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
                /*
                if (formatName == "xml") {
                    outputFormat = OutputFormat::XML;
                    return;
                }
                */
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
        e = state->parseExprFromFile(lookupFileArg(*state, inputFile));

        if (outputFormat == OutputFormat::JSON)
            e->showAsJson(std::cout);
        /*
        else if (outputFormat == OutputFormat::XML)
            e->showAsXml(std::cout);
        */
        else
            e->show(std::cout);
    }
};

static auto rCmdParse = registerCommand<CmdParse>("parse");
