// based on
// nix-instantiate
// store.cc
// dump-path.cc
// libcmd/installables.cc:105

#include "command.hh"

#include "eval.hh"

using namespace nix;

enum OutputFormat {
    ATerm,
    XML,
    JSON,
    JSONNumtypes,
    JSONArrays,
    JSONArraysFmt,
};

struct CmdParse : Command
{
    uint outputFormat = OutputFormat::ATerm;
    Path path; // TODO rename to inputFile ?

    CmdParse()
    {
        // ./src/nix/add-to-store.cc
        // ./src/nix/cat.cc
        // FIXME: completion
        //expectArg("path", &path);
        expectArgs({
            .label = "path",
            .handler = {&path},
            .completer = completePath // ./src/libutil/args.hh:232
        });

        addFlag({
            .longName = "output-format",
            //.shortName = 'f',
            .description = "output format",
            .labels = {"format"},
            .handler = {[&](std::string formatName) {
                if (formatName.empty()) throw UsageError("--output-format requires one argument");
                // TODO use std::map<string, OutputFormat> ?
                if (formatName == "json") {
                    outputFormat = OutputFormat::JSON;
                    return;
                }
                if (formatName == "json-numtypes") {
                    outputFormat = OutputFormat::JSONNumtypes;
                    return;
                }
                if (formatName == "json-arrays") {
                    outputFormat = OutputFormat::JSONArrays;
                    return;
                }
                if (formatName == "json-arrays-fmt") {
                    outputFormat = OutputFormat::JSONArraysFmt;
                    return;
                }
                if (formatName == "xml") {
                    outputFormat = OutputFormat::XML;
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
        //std::list<string> searchPath = {"."}; // TODO better?
        // ./src/libutil/util.hh:284: typedef list<string> Strings;

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

        //e = state->parseExprFromFile(resolveExprPath(state->checkSourcePath(lookupFileArg(*state, path))));
        e = state->parseExprFromFile(lookupFileArg(*state, path));

        if (outputFormat == OutputFormat::JSON) {
            e->showAsJson(std::cout);
        }
        else if (outputFormat == OutputFormat::JSONNumtypes) {
            e->showAsJsonNumtypes(std::cout);
        }
        else if (outputFormat == OutputFormat::JSONArrays) {
            e->showAsJsonArrays(std::cout);
        }
        else if (outputFormat == OutputFormat::JSONArraysFmt) {
            e->showAsJsonArraysFmt(stdout);
        }
        //else if (outputFormat == OutputFormat::XML) {
        //    e->showAsXml(std::cout);
        //}
        else {
            e->show(std::cout);
        }
        return;
    }
};

static auto rCmdParse = registerCommand<CmdParse>("parse");
