#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>

#include <setjmp.h>

#ifdef READLINE
#include <readline/history.h>
#include <readline/readline.h>
#else
// editline < 1.15.2 don't wrap their API for C++ usage
// (added in https://github.com/troglobit/editline/commit/91398ceb3427b730995357e9d120539fb9bb7461).
// This results in linker errors due to to name-mangling of editline C symbols.
// For compatibility with these versions, we wrap the API here
// (wrapping multiple times on newer versions is no problem).
extern "C" {
#include <editline.h>
}
#endif

#include "ansicolor.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "attr-path.hh"
#include "store-api.hh"
#include "common-eval-args.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "globals.hh"
#include "command.hh"
#include "finally.hh"
#include "repl.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

namespace nix {

struct CmdRepl : StoreCommand, MixEvalArgs
{
    std::vector<std::string> files;

    CmdRepl()
    {
        expectArgs({
            .label = "files",
            .handler = {&files},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "start an interactive environment for evaluating Nix expressions";
    }

    Examples examples() override
    {
        return {
          Example{
            "Display all special commands within the REPL:",
              "nix repl\n  nix-repl> :?"
          }
        };
    }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = false;
        auto repl = std::make_unique<NixRepl>(searchPath, openStore());
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->mainLoop(files);
    }
};

static auto r1 = registerCommand<CmdRepl>("repl");

}
