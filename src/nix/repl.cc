#include "command.hh"
#include "repl.hh"
#include "callable.hh"

// editline < 1.15.2 don't wrap their API for C++ usage
// (added in https://github.com/troglobit/editline/commit/91398ceb3427b730995357e9d120539fb9bb7461).
// This results in linker errors due to to name-mangling of editline C symbols.
// For compatibility with these versions, we wrap the API here
// (wrapping multiple times on newer versions is no problem).
extern "C" {
#include <editline.h>
}

namespace nix {

static int listPossibleCallback(NixRepl & repl, char *s, char ***avp) {
  auto possible = repl.completePrefix(s);

  if (possible.size() > (INT_MAX / sizeof(char*)))
    throw Error("too many completions");

  int ac = 0;
  char **vp = nullptr;

  auto check = [&](auto *p) {
    if (!p) {
      if (vp) {
        while (--ac >= 0)
          free(vp[ac]);
        free(vp);
      }
      throw Error("allocation failure");
    }
    return p;
  };

  vp = check((char **)malloc(possible.size() * sizeof(char*)));

  for (auto & p : possible)
    vp[ac++] = check(strdup(p.c_str()));

  *avp = vp;

  return ac;
}

static char * completionCallback(NixRepl & repl, char * s, int *match) {
  auto possible = repl.completePrefix(s);
  if (possible.size() == 1) {
    *match = 1;
    auto *res = strdup(possible.begin()->c_str() + strlen(s));
    if (!res) throw Error("allocation failure");
    return res;
  } else if (possible.size() > 1) {
    auto checkAllHaveSameAt = [&](size_t pos) {
      auto &first = *possible.begin();
      for (auto &p : possible) {
        if (p.size() <= pos || p[pos] != first[pos])
          return false;
      }
      return true;
    };
    size_t start = strlen(s);
    size_t len = 0;
    while (checkAllHaveSameAt(start + len)) ++len;
    if (len > 0) {
      *match = 1;
      auto *res = strdup(std::string(*possible.begin(), start, len).c_str());
      if (!res) throw Error("allocation failure");
      return res;
    }
  }

  *match = 0;
  return nullptr;
}

static void replMainLoop(NixRepl & repl, const std::vector<std::string> & files)
{
    string error = ANSI_RED "error:" ANSI_NORMAL " ";
    std::cout << "Welcome to Nix version " << nixVersion << ". Type :? for help." << std::endl << std::endl;

    for (auto & i : files)
        repl.loadedFiles.push_back(i);

    repl.reloadFiles();
    if (!repl.loadedFiles.empty()) std::cout << std::endl;

    // Allow nix-repl specific settings in .inputrc
    rl_readline_name = "nix-repl";
    createDirs(dirOf(repl.historyFile));

    el_hist_size = 1000;
    read_history(repl.historyFile.c_str());
    rl_set_complete_func(cify([&repl](char * s, int * match) {
        return completionCallback(repl, s, match);
    }));
    rl_set_list_possib_func(cify([&repl](char *s, char ***avp) {
            return listPossibleCallback(repl, s, avp);
        }));

    std::string input;

    while (true) {
        // When continuing input from previous lines, don't print a prompt, just align to the same
        // number of chars as the prompt.
        if (!repl.getLine(input, input.empty() ? "nix-repl> " : "          "))
            break;

        try {
            if (!repl.removeWhitespace(input).empty() && !repl.processLine(input)) return;
        } catch (ParseError & e) {
            if (e.msg().find("unexpected $end") != std::string::npos) {
                // For parse errors on incomplete input, we continue waiting for the next line of
                // input without clearing the input so far.
                continue;
            } else {
              printMsg(lvlError, e.msg());
            }
        } catch (Error & e) {
          printMsg(lvlError, e.msg());
        } catch (Interrupted & e) {
          printMsg(lvlError, e.msg());
        }

        // We handled the current input fully, so we should clear it
        // and read brand new input.
        input.clear();
        std::cout << std::endl;
    }
}


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
        auto repl = std::make_unique<NixRepl>(searchPath, openStore(),
            NixRepl::CompletionFunctions {
                .writeHistory = write_history,
                .readline = readline
            }
        );
        repl->autoArgs = getAutoArgs(*repl->state);
        replMainLoop(*repl, files);
    }
};

static auto r1 = registerCommand<CmdRepl>("repl");

}
