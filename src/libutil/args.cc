#include "args.hh"
#include "hash.hh"

#include <glob.h>

#include <nlohmann/json.hpp>

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

void Completions::add(std::string completion, std::string description)
{
    assert(description.find('\n') == std::string::npos);
    insert(Completion {
        .completion = completion,
        .description = description
    });
}

bool Completion::operator<(const Completion & other) const
{ return completion < other.completion || (completion == other.completion && description < other.description); }

CompletionType completionType = ctNormal;
std::shared_ptr<Completions> completions;

std::string completionMarker = "___COMPLETE___";

std::optional<std::string> needsCompletion(std::string_view s)
{
    if (!completions) return {};
    auto i = s.find(completionMarker);
    if (i != std::string::npos)
        return std::string(s.begin(), i);
    return {};
}

void Args::parseCmdline(const Strings & _cmdline)
{
    Strings pendingArgs;
    bool dashDash = false;

    Strings cmdline(_cmdline);

    if (auto s = getEnv("NIX_GET_COMPLETIONS")) {
        size_t n = std::stoi(*s);
        assert(n > 0 && n <= cmdline.size());
        *std::next(cmdline.begin(), n - 1) += completionMarker;
        completions = std::make_shared<decltype(completions)::element_type>();
        verbosity = lvlError;
    }

    bool argsSeen = false;
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
}

bool Args::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    assert(pos != end);

    auto process = [&](const std::string & name, const Flag & flag) -> bool {
        ++pos;
        std::vector<std::string> args;
        bool anyCompleted = false;
        for (size_t n = 0 ; n < flag.handler.arity; ++n) {
            if (pos == end) {
                if (flag.handler.arity == ArityAny) break;
                throw UsageError("flag '%s' requires %d argument(s)", name, flag.handler.arity);
            }
            if (auto prefix = needsCompletion(*pos)) {
                anyCompleted = true;
                if (flag.completer)
                    flag.completer(n, *prefix);
            }
            args.push_back(*pos++);
        }
        if (!anyCompleted)
            flag.handler.fun(std::move(args));
        return true;
    };

    if (std::string(*pos, 0, 2) == "--") {
        if (auto prefix = needsCompletion(*pos)) {
            for (auto & [name, flag] : longFlags) {
                if (!hiddenCategories.count(flag->category)
                    && hasPrefix(name, std::string(*prefix, 2)))
                    completions->add("--" + name, flag->description);
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

    if (auto prefix = needsCompletion(*pos)) {
        if (prefix == "-") {
            completions->add("--");
            for (auto & [flagName, flag] : shortFlags)
                completions->add(std::string("-") + flagName, flag->description);
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

    auto & exp = expectedArgs.front();

    bool res = false;

    if ((exp.handler.arity == ArityAny && finish) ||
        (exp.handler.arity != ArityAny && args.size() == exp.handler.arity))
    {
        std::vector<std::string> ss;
        for (const auto &[n, s] : enumerate(args)) {
            if (auto prefix = needsCompletion(s)) {
                ss.push_back(*prefix);
                if (exp.completer)
                    exp.completer(n, *prefix);
            } else
                ss.push_back(s);
        }
        exp.handler.fun(ss);
        expectedArgs.pop_front();
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
        if (flag->aliases.count(name)) continue;
        if (flag->shortName)
            j["shortName"] = std::string(1, flag->shortName);
        if (flag->description != "")
            j["description"] = flag->description;
        j["category"] = flag->category;
        if (flag->handler.arity != ArityAny)
            j["arity"] = flag->handler.arity;
        if (!flag->labels.empty())
            j["labels"] = flag->labels;
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
    res["description"] = description();
    res["flags"] = std::move(flags);
    res["args"] = std::move(args);
    auto s = doc();
    if (s != "") res.emplace("doc", stripIndentation(s));
    return res;
}

static void hashTypeCompleter(size_t index, std::string_view prefix)
{
    for (auto & type : hashTypes)
        if (hasPrefix(type, prefix))
            completions->add(type);
}

Args::Flag Args::Flag::mkHashTypeFlag(std::string && longName, HashType * ht)
{
    return Flag {
        .longName = std::move(longName),
        .description = "hash algorithm ('md5', 'sha1', 'sha256', or 'sha512')",
        .labels = {"hash-algo"},
        .handler = {[ht](std::string s) {
            *ht = parseHashType(s);
        }},
        .completer = hashTypeCompleter
    };
}

Args::Flag Args::Flag::mkHashTypeOptFlag(std::string && longName, std::optional<HashType> * oht)
{
    return Flag {
        .longName = std::move(longName),
        .description = "hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.",
        .labels = {"hash-algo"},
        .handler = {[oht](std::string s) {
            *oht = std::optional<HashType> { parseHashType(s) };
        }},
        .completer = hashTypeCompleter
    };
}

static void _completePath(std::string_view prefix, bool onlyDirs)
{
    completionType = ctFilenames;
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
            completions->add(globbuf.gl_pathv[i]);
        }
    }
    globfree(&globbuf);
}

void completePath(size_t, std::string_view prefix)
{
    _completePath(prefix, false);
}

void completeDir(size_t, std::string_view prefix)
{
    _completePath(prefix, true);
}

Strings argvToStrings(int argc, char * * argv)
{
    Strings args;
    argc--; argv++;
    while (argc--) args.push_back(*argv++);
    return args;
}

MultiCommand::MultiCommand(const Commands & commands_)
    : commands(commands_)
{
    expectArgs({
        .label = "subcommand",
        .optional = true,
        .handler = {[=](std::string s) {
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
        .completer = {[&](size_t, std::string_view prefix) {
            for (auto & [name, command] : commands)
                if (hasPrefix(name, prefix))
                    completions->add(name);
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
        cat["description"] = categories[command->category()];
        j["category"] = std::move(cat);
        cmds[name] = std::move(j);
    }

    auto res = Args::toJSON();
    res["commands"] = std::move(cmds);
    return res;
}

}
