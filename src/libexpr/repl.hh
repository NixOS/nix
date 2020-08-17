#include "eval.hh"
#include <vector>
#include <functional>

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

namespace nix {

struct NixRepl
    #if HAVE_BOEHMGC
    : gc
    #endif
{
    struct CompletionFunctions {
        std::function<int (const char * filename)> writeHistory;
        std::function<char * (const char * prompt)> readline;
    };

    string curDir;
    std::unique_ptr<EvalState> state;
    Bindings * autoArgs;

    Strings loadedFiles;

    const static int envSize = 32768;
    StaticEnv staticEnv;
    Env * env;
    int displ;
    StringSet varNames;

    const Path historyFile;
    CompletionFunctions completionFunctions;

    NixRepl(const Strings & searchPath, nix::ref<Store> store,
            CompletionFunctions completionFunctions);
    ~NixRepl();
    void mainLoop(const std::vector<std::string> & files);
    StringSet completePrefix(string prefix);
    bool getLine(string & input, const std::string &prompt);
    Path getDerivationPath(Value & v);
    bool processLine(string line);
    void loadFile(const Path & path);
    void initEnv();
    void reloadFiles();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol & name, Value & v);
    Expr * parseString(string s);
    void evalString(string s, Value & v);

    static string removeWhitespace(string s);

    typedef set<Value *> ValuesSeen;
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth);
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen);
};

using ReplCmdFun = std::function<void (NixRepl & repl, string name, string arg)>;

/**
 * A registry for extending the REPL commands list.
 */
struct RegisterReplCmd
{
    struct ReplCmd
    {
        /**
         * Names of the commands this matches, not prefixed by :. The first one
         * is displayed in help.
         */
        vector<string> names;
        /**
         * Argument placeholder, for example, "<expr>".
         */
        string argPlaceholder;
        /**
         * Help message displayed in :?.
         */
        string help;
        /**
         * Callback.
         */
        ReplCmdFun cmd;
    };

    using ReplCmds = vector<ReplCmd>;

    static ReplCmds * replCmds;

    RegisterReplCmd(
        vector<string> names,
        string help,
        ReplCmdFun cmd,
        string argPlaceholder = ""
    );
};
}