#include "args.hh"
#include "args/root.hh"
#include "hash.hh"
#include "environment-variables.hh"
#include "signals.hh"
#include "users.hh"
#include "json-utils.hh"

#include <fstream>
#include <string>
#include <regex>
#include <glob.h>

namespace nix {

void Args::addFlag(Flag && flag_)
{
    auto flag = std::make_shared<Flag>(std::move(flag_));
    if (flag->handler.arity != ArityAny)
        assert(flag->handler.arity == flag->labels.size());
    assert(flag->longName != "");
    longFlags[flag->longName] = flag;
    for (auto & alias : flag->aliases)
        longFlags[alias] = flag;
    if (flag->shortName) shortFlags[flag->shortName] = flag;
}

void Args::removeFlag(const std::string & longName)
{
    auto flag = longFlags.find(longName);
    assert(flag != longFlags.end());
    if (flag->second->shortName) shortFlags.erase(flag->second->shortName);
    longFlags.erase(flag);
}

void Completions::setType(AddCompletions::Type t)
{
    type = t;
}

void Completions::add(std::string completion, std::string description)
{
    description = trim(description);
    // ellipsize overflowing content on the back of the description
    auto end_index = description.find_first_of(".\n");
    if (end_index != std::string::npos) {
        auto needs_ellipsis = end_index != description.size() - 1;
        description.resize(end_index);
        if (needs_ellipsis)
            description.append(" [...]");
    }
    completions.insert(Completion {
        .completion = completion,
        .description = description
    });
}

bool Completion::operator<(const Completion & other) const
{ return completion < other.completion || (completion == other.completion && description < other.description); }

std::string completionMarker = "___COMPLETE___";

RootArgs & Args::getRoot()
{
    Args * p = this;
    while (p->parent)
        p = p->parent;

    auto * res = dynamic_cast<RootArgs *>(p);
    assert(res);
    return *res;
}

std::optional<std::string> RootArgs::needsCompletion(std::string_view s)
{
    if (!completions) return {};
    auto i = s.find(completionMarker);
    if (i != std::string::npos)
        return std::string(s.begin(), i);
    return {};
}

/**
 * Basically this is `typedef std::optional<Parser> Parser(std::string_view s, Strings & r);`
 *
 * Except we can't recursively reference the Parser typedef, so we have to write a class.
 */
struct Parser {
    std::string_view remaining;

    /**
     * @brief Parse the next character(s)
     *
     * @param r
     * @return std::shared_ptr<Parser>
     */
    virtual void operator()(std::shared_ptr<Parser> & state, Strings & r) = 0;

    Parser(std::string_view s) : remaining(s) {};

    virtual ~Parser() { };
};

struct ParseQuoted : public Parser {
    /**
     * @brief Accumulated string
     *
     * Parsed argument up to this point.
     */
    std::string acc;

    ParseQuoted(std::string_view s) : Parser(s) {};

    virtual void operator()(std::shared_ptr<Parser> & state, Strings & r) override;
};


struct ParseUnquoted : public Parser {
    /**
     * @brief Accumulated string
     *
     * Parsed argument up to this point. Empty string is not representable in
     * unquoted syntax, so we use it for the initial state.
     */
    std::string acc;

    ParseUnquoted(std::string_view s) : Parser(s) {};

    virtual void operator()(std::shared_ptr<Parser> & state, Strings & r) override {
        if (remaining.empty()) {
            if (!acc.empty())
                r.push_back(acc);
            state = nullptr; // done
            return;
        }
        switch (remaining[0]) {
            case ' ': case '\t': case '\n': case '\r':
                if (!acc.empty())
                    r.push_back(acc);
                state = std::make_shared<ParseUnquoted>(ParseUnquoted(remaining.substr(1)));
                return;
            case '`':
                if (remaining.size() > 1 && remaining[1] == '`') {
                    state = std::make_shared<ParseQuoted>(ParseQuoted(remaining.substr(2)));
                    return;
                }
                else
                    throw Error("single backtick is not a supported syntax in the nix shebang.");

            // reserved characters
            // meaning to be determined, or may be reserved indefinitely so that
            // #!nix syntax looks unambiguous
            case '$':
            case '*':
            case '~':
            case '<':
            case '>':
            case '|':
            case ';':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '\'':
            case '"':
            case '\\':
                throw Error("unsupported unquoted character in nix shebang: " + std::string(1, remaining[0]) + ". Use double backticks to escape?");

            case '#':
                if (acc.empty()) {
                    throw Error ("unquoted nix shebang argument cannot start with #. Use double backticks to escape?");
                } else {
                    acc += remaining[0];
                    remaining = remaining.substr(1);
                    return;
                }

            default:
                acc += remaining[0];
                remaining = remaining.substr(1);
                return;
        }
        assert(false);
    }
};

void ParseQuoted::operator()(std::shared_ptr<Parser> &state, Strings & r) {
    if (remaining.empty()) {
        throw Error("unterminated quoted string in nix shebang");
    }
    switch (remaining[0]) {
        case ' ':
            if ((remaining.size() == 3 && remaining[1] == '`' && remaining[2] == '`')
                || (remaining.size() > 3 && remaining[1] == '`' && remaining[2] == '`' && remaining[3] != '`')) {
                // exactly two backticks mark the end of a quoted string, but a preceding space is ignored if present.
                state = std::make_shared<ParseUnquoted>(ParseUnquoted(remaining.substr(3)));
                r.push_back(acc);
                return;
            }
            else {
                // just a normal space
                acc += remaining[0];
                remaining = remaining.substr(1);
                return;
            }
        case '`':
            // exactly two backticks mark the end of a quoted string
            if ((remaining.size() == 2 && remaining[1] == '`')
                || (remaining.size() > 2 && remaining[1] == '`' && remaining[2] != '`')) {
                state = std::make_shared<ParseUnquoted>(ParseUnquoted(remaining.substr(2)));
                r.push_back(acc);
                return;
            }

            // a sequence of at least 3 backticks is one escape-backtick which is ignored, followed by any number of backticks, which are verbatim
            else if (remaining.size() >= 3 && remaining[1] == '`' && remaining[2] == '`') {
                // ignore "escape" backtick
                remaining = remaining.substr(1);
                // add the rest
                while (remaining.size() > 0 && remaining[0] == '`') {
                    acc += '`';
                    remaining = remaining.substr(1);
                }
                return;
            }
            else {
                acc += remaining[0];
                remaining = remaining.substr(1);
                return;
            }
        default:
            acc += remaining[0];
            remaining = remaining.substr(1);
            return;
    }
    assert(false);
}

Strings parseShebangContent(std::string_view s) {
    Strings result;
    std::shared_ptr<Parser> parserState(std::make_shared<ParseUnquoted>(ParseUnquoted(s)));

    // trampoline == iterated strategy pattern
    while (parserState) {
        auto currentState = parserState;
        (*currentState)(parserState, result);
    }

    return result;
}

void RootArgs::parseCmdline(const Strings & _cmdline, bool allowShebang)
{
    Strings pendingArgs;
    bool dashDash = false;

    Strings cmdline(_cmdline);

    if (auto s = getEnv("NIX_GET_COMPLETIONS")) {
        size_t n = std::stoi(*s);
        assert(n > 0 && n <= cmdline.size());
        *std::next(cmdline.begin(), n - 1) += completionMarker;
        completions = std::make_shared<Completions>();
        verbosity = lvlError;
    }

    bool argsSeen = false;

    // Heuristic to see if we're invoked as a shebang script, namely,
    // if we have at least one argument, it's the name of an
    // executable file, and it starts with "#!".
    Strings savedArgs;
    if (allowShebang){
        auto script = *cmdline.begin();
        try {
            std::ifstream stream(script);
            char shebang[3]={0,0,0};
            stream.get(shebang,3);
            if (strncmp(shebang,"#!",2) == 0){
                for (auto pos = std::next(cmdline.begin()); pos != cmdline.end();pos++)
                    savedArgs.push_back(*pos);
                cmdline.clear();

                std::string line;
                std::getline(stream,line);
                static const std::string commentChars("#/\\%@*-(");
                std::string shebangContent;
                while (std::getline(stream,line) && !line.empty() && commentChars.find(line[0]) != std::string::npos){
                    line = chomp(line);

                    std::smatch match;
                    // We match one space after `nix` so that we preserve indentation.
                    // No space is necessary for an empty line. An empty line has basically no effect.
                    if (std::regex_match(line, match, std::regex("^#!\\s*nix(:? |$)(.*)$")))
                        shebangContent += match[2].str() + "\n";
                }
                for (const auto & word : parseShebangContent(shebangContent)) {
                    cmdline.push_back(word);
                }
                cmdline.push_back(script);
                commandBaseDir = dirOf(script);
                for (auto pos = savedArgs.begin(); pos != savedArgs.end();pos++)
                    cmdline.push_back(*pos);
            }
        } catch (SystemError &) { }
    }
    for (auto pos = cmdline.begin(); pos != cmdline.end(); ) {

        auto arg = *pos;

        /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f',
           `-j3` -> `-j 3`). */
        if (!dashDash && arg.length() > 2 && arg[0] == '-' && arg[1] != '-' && isalpha(arg[1])) {
            *pos = (std::string) "-" + arg[1];
            auto next = pos; ++next;
            for (unsigned int j = 2; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    cmdline.insert(next, (std::string) "-" + arg[j]);
                else {
                    cmdline.insert(next, std::string(arg, j));
                    break;
                }
            arg = *pos;
        }

        if (!dashDash && arg == "--") {
            dashDash = true;
            ++pos;
        }
        else if (!dashDash && std::string(arg, 0, 1) == "-") {
            if (!processFlag(pos, cmdline.end()))
                throw UsageError("unrecognised flag '%1%'", arg);
        }
        else {
            if (!argsSeen) {
                argsSeen = true;
                initialFlagsProcessed();
            }
            pos = rewriteArgs(cmdline, pos);
            pendingArgs.push_back(*pos++);
            if (processArgs(pendingArgs, false))
                pendingArgs.clear();
        }
    }

    processArgs(pendingArgs, true);

    if (!argsSeen)
        initialFlagsProcessed();

    /* Now that we are done parsing, make sure that any experimental
     * feature required by the flags is enabled */
    for (auto & f : flagExperimentalFeatures)
        experimentalFeatureSettings.require(f);

    /* Now that all the other args are processed, run the deferred completions.
     */
    for (auto d : deferredCompletions)
        d.completer(*completions, d.n, d.prefix);
}

Path Args::getCommandBaseDir() const
{
    assert(parent);
    return parent->getCommandBaseDir();
}

Path RootArgs::getCommandBaseDir() const
{
    return commandBaseDir;
}

bool Args::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    assert(pos != end);

    auto & rootArgs = getRoot();

    auto process = [&](const std::string & name, const Flag & flag) -> bool {
        ++pos;

        if (auto & f = flag.experimentalFeature)
            rootArgs.flagExperimentalFeatures.insert(*f);

        std::vector<std::string> args;
        bool anyCompleted = false;
        for (size_t n = 0 ; n < flag.handler.arity; ++n) {
            if (pos == end) {
                if (flag.handler.arity == ArityAny || anyCompleted) break;
                throw UsageError(
                    "flag '%s' requires %d argument(s), but only %d were given",
                    name, flag.handler.arity, n);
            }
            if (auto prefix = rootArgs.needsCompletion(*pos)) {
                anyCompleted = true;
                if (flag.completer) {
                    rootArgs.deferredCompletions.push_back({
                        .completer = flag.completer,
                        .n = n,
                        .prefix = *prefix,
                    });
                }
            }
            args.push_back(*pos++);
        }
        if (!anyCompleted)
            flag.handler.fun(std::move(args));
        return true;
    };

    if (std::string(*pos, 0, 2) == "--") {
        if (auto prefix = rootArgs.needsCompletion(*pos)) {
            for (auto & [name, flag] : longFlags) {
                if (!hiddenCategories.count(flag->category)
                    && hasPrefix(name, std::string(*prefix, 2)))
                {
                    if (auto & f = flag->experimentalFeature)
                        rootArgs.flagExperimentalFeatures.insert(*f);
                    rootArgs.completions->add("--" + name, flag->description);
                }
            }
            return false;
        }
        auto i = longFlags.find(std::string(*pos, 2));
        if (i == longFlags.end()) return false;
        return process("--" + i->first, *i->second);
    }

    if (std::string(*pos, 0, 1) == "-" && pos->size() == 2) {
        auto c = (*pos)[1];
        auto i = shortFlags.find(c);
        if (i == shortFlags.end()) return false;
        return process(std::string("-") + c, *i->second);
    }

    if (auto prefix = rootArgs.needsCompletion(*pos)) {
        if (prefix == "-") {
            rootArgs.completions->add("--");
            for (auto & [flagName, flag] : shortFlags)
                if (experimentalFeatureSettings.isEnabled(flag->experimentalFeature))
                    rootArgs.completions->add(std::string("-") + flagName, flag->description);
        }
    }

    return false;
}

bool Args::processArgs(const Strings & args, bool finish)
{
    if (expectedArgs.empty()) {
        if (!args.empty())
            throw UsageError("unexpected argument '%1%'", args.front());
        return true;
    }

    auto & rootArgs = getRoot();

    auto & exp = expectedArgs.front();

    bool res = false;

    if ((exp.handler.arity == ArityAny && finish) ||
        (exp.handler.arity != ArityAny && args.size() == exp.handler.arity))
    {
        std::vector<std::string> ss;
        bool anyCompleted = false;
        for (const auto &[n, s] : enumerate(args)) {
            if (auto prefix = rootArgs.needsCompletion(s)) {
                anyCompleted = true;
                ss.push_back(*prefix);
                if (exp.completer) {
                    rootArgs.deferredCompletions.push_back({
                        .completer = exp.completer,
                        .n = n,
                        .prefix = *prefix,
                    });
                }
            } else
                ss.push_back(s);
        }
        if (!anyCompleted)
            exp.handler.fun(ss);

        /* Move the list element to the processedArgs. This is almost the same as
           `processedArgs.push_back(expectedArgs.front()); expectedArgs.pop_front()`,
           except that it will only adjust the next and prev pointers of the list
           elements, meaning the actual contents don't move in memory. This is
           critical to prevent invalidating internal pointers! */
        processedArgs.splice(
            processedArgs.end(),
            expectedArgs,
            expectedArgs.begin(),
            ++expectedArgs.begin());

        res = true;
    }

    if (finish && !expectedArgs.empty() && !expectedArgs.front().optional)
        throw UsageError("more arguments are required");

    return res;
}

nlohmann::json Args::toJSON()
{
    auto flags = nlohmann::json::object();

    for (auto & [name, flag] : longFlags) {
        auto j = nlohmann::json::object();
        j["hiddenCategory"] = hiddenCategories.count(flag->category) > 0;
        if (flag->aliases.count(name)) continue;
        if (flag->shortName)
            j["shortName"] = std::string(1, flag->shortName);
        if (flag->description != "")
            j["description"] = trim(flag->description);
        j["category"] = flag->category;
        if (flag->handler.arity != ArityAny)
            j["arity"] = flag->handler.arity;
        if (!flag->labels.empty())
            j["labels"] = flag->labels;
        j["experimental-feature"] = flag->experimentalFeature;
        flags[name] = std::move(j);
    }

    auto args = nlohmann::json::array();

    for (auto & arg : expectedArgs) {
        auto j = nlohmann::json::object();
        j["label"] = arg.label;
        j["optional"] = arg.optional;
        if (arg.handler.arity != ArityAny)
            j["arity"] = arg.handler.arity;
        args.push_back(std::move(j));
    }

    auto res = nlohmann::json::object();
    res["description"] = trim(description());
    res["flags"] = std::move(flags);
    res["args"] = std::move(args);
    auto s = doc();
    if (s != "") res.emplace("doc", stripIndentation(s));
    return res;
}

static void _completePath(AddCompletions & completions, std::string_view prefix, bool onlyDirs)
{
    completions.setType(Completions::Type::Filenames);
    glob_t globbuf;
    int flags = GLOB_NOESCAPE;
    #ifdef GLOB_ONLYDIR
    if (onlyDirs)
        flags |= GLOB_ONLYDIR;
    #endif
    // using expandTilde here instead of GLOB_TILDE(_CHECK) so that ~<Tab> expands to /home/user/
    if (glob((expandTilde(prefix) + "*").c_str(), flags, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            if (onlyDirs) {
                auto st = stat(globbuf.gl_pathv[i]);
                if (!S_ISDIR(st.st_mode)) continue;
            }
            completions.add(globbuf.gl_pathv[i]);
        }
    }
    globfree(&globbuf);
}

void Args::completePath(AddCompletions & completions, size_t, std::string_view prefix)
{
    _completePath(completions, prefix, false);
}

void Args::completeDir(AddCompletions & completions, size_t, std::string_view prefix)
{
    _completePath(completions, prefix, true);
}

Strings argvToStrings(int argc, char * * argv)
{
    Strings args;
    argc--; argv++;
    while (argc--) args.push_back(*argv++);
    return args;
}

std::optional<ExperimentalFeature> Command::experimentalFeature ()
{
    return { Xp::NixCommand };
}

MultiCommand::MultiCommand(std::string_view commandName, const Commands & commands_)
    : commands(commands_)
    , commandName(commandName)
{
    expectArgs({
        .label = "subcommand",
        .optional = true,
        .handler = {[=,this](std::string s) {
            assert(!command);
            auto i = commands.find(s);
            if (i == commands.end()) {
                std::set<std::string> commandNames;
                for (auto & [name, _] : commands)
                    commandNames.insert(name);
                auto suggestions = Suggestions::bestMatches(commandNames, s);
                throw UsageError(suggestions, "'%s' is not a recognised command", s);
            }
            command = {s, i->second()};
            command->second->parent = this;
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            for (auto & [name, command] : commands)
                if (hasPrefix(name, prefix))
                    completions.add(name);
        }}
    });

    categories[Command::catDefault] = "Available commands";
}

bool MultiCommand::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (Args::processFlag(pos, end)) return true;
    if (command && command->second->processFlag(pos, end)) return true;
    return false;
}

bool MultiCommand::processArgs(const Strings & args, bool finish)
{
    if (command)
        return command->second->processArgs(args, finish);
    else
        return Args::processArgs(args, finish);
}

nlohmann::json MultiCommand::toJSON()
{
    auto cmds = nlohmann::json::object();

    for (auto & [name, commandFun] : commands) {
        auto command = commandFun();
        auto j = command->toJSON();
        auto cat = nlohmann::json::object();
        cat["id"] = command->category();
        cat["description"] = trim(categories[command->category()]);
        cat["experimental-feature"] = command->experimentalFeature();
        j["category"] = std::move(cat);
        cmds[name] = std::move(j);
    }

    auto res = Args::toJSON();
    res["commands"] = std::move(cmds);
    return res;
}

}
