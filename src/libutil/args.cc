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
    if (flag->shortName) shortFlags[flag->shortName] = flag;
}

bool pathCompletions = false;
std::shared_ptr<std::set<std::string>> completions;

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

    for (auto pos = cmdline.begin(); pos != cmdline.end(); ) {

        auto arg = *pos;

        /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f',
           `-j3` -> `-j 3`). */
        if (!dashDash && arg.length() > 2 && arg[0] == '-' && arg[1] != '-' && isalpha(arg[1])) {
            *pos = (string) "-" + arg[1];
            auto next = pos; ++next;
            for (unsigned int j = 2; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    cmdline.insert(next, (string) "-" + arg[j]);
                else {
                    cmdline.insert(next, string(arg, j));
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
            pendingArgs.push_back(*pos++);
            if (processArgs(pendingArgs, false))
                pendingArgs.clear();
        }
    }

    processArgs(pendingArgs, true);
}

void Args::printHelp(const string & programName, std::ostream & out)
{
    std::cout << fmt(ANSI_BOLD "Usage:" ANSI_NORMAL " %s " ANSI_ITALIC "FLAGS..." ANSI_NORMAL, programName);
    for (auto & exp : expectedArgs) {
        std::cout << renderLabels({exp.label});
        // FIXME: handle arity > 1
        if (exp.handler.arity == ArityAny) std::cout << "...";
        if (exp.optional) std::cout << "?";
    }
    std::cout << "\n";

    auto s = description();
    if (s != "")
        std::cout << "\n" ANSI_BOLD "Summary:" ANSI_NORMAL " " << s << ".\n";

    if (longFlags.size()) {
        std::cout << "\n";
        std::cout << ANSI_BOLD "Flags:" ANSI_NORMAL "\n";
        printFlags(out);
    }
}

void Args::printFlags(std::ostream & out)
{
    Table2 table;
    for (auto & flag : longFlags) {
        if (hiddenCategories.count(flag.second->category)) continue;
        table.push_back(std::make_pair(
                (flag.second->shortName ? std::string("-") + flag.second->shortName + ", " : "    ")
                + "--" + flag.first + renderLabels(flag.second->labels),
                flag.second->description));
    }
    printTable(out, table);
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
            if (flag.completer)
                if (auto prefix = needsCompletion(*pos)) {
                    anyCompleted = true;
                    flag.completer(n, *prefix);
                }
            args.push_back(*pos++);
        }
        if (!anyCompleted)
            flag.handler.fun(std::move(args));
        return true;
    };

    if (string(*pos, 0, 2) == "--") {
        if (auto prefix = needsCompletion(*pos)) {
            for (auto & [name, flag] : longFlags) {
                if (!hiddenCategories.count(flag->category)
                    && hasPrefix(name, std::string(*prefix, 2)))
                    completions->insert("--" + name);
            }
        }
        auto i = longFlags.find(string(*pos, 2));
        if (i == longFlags.end()) return false;
        return process("--" + i->first, *i->second);
    }

    if (string(*pos, 0, 1) == "-" && pos->size() == 2) {
        auto c = (*pos)[1];
        auto i = shortFlags.find(c);
        if (i == shortFlags.end()) return false;
        return process(std::string("-") + c, *i->second);
    }

    if (auto prefix = needsCompletion(*pos)) {
        if (prefix == "-") {
            completions->insert("--");
            for (auto & [flag, _] : shortFlags)
                completions->insert(std::string("-") + flag);
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
            ss.push_back(s);
            if (exp.completer)
                if (auto prefix = needsCompletion(s))
                    exp.completer(n, *prefix);
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
        if (flag->shortName)
            j["shortName"] = std::string(1, flag->shortName);
        if (flag->description != "")
            j["description"] = flag->description;
        if (flag->category != "")
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
    return res;
}

static void hashTypeCompleter(size_t index, std::string_view prefix) 
{
    for (auto & type : hashTypes)
        if (hasPrefix(type, prefix))
            completions->insert(type);
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

static void completePath(std::string_view prefix, bool onlyDirs)
{
    pathCompletions = true;
    glob_t globbuf;
    int flags = GLOB_NOESCAPE | GLOB_TILDE;
    #ifdef GLOB_ONLYDIR
    if (onlyDirs)
        flags |= GLOB_ONLYDIR;
    #endif
    if (glob((std::string(prefix) + "*").c_str(), flags, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            if (onlyDirs) {
                auto st = lstat(globbuf.gl_pathv[i]);
                if (!S_ISDIR(st.st_mode)) continue;
            }
            completions->insert(globbuf.gl_pathv[i]);
        }
        globfree(&globbuf);
    }
}

void completePath(size_t, std::string_view prefix)
{
    completePath(prefix, false);
}

void completeDir(size_t, std::string_view prefix)
{
    completePath(prefix, true);
}

Strings argvToStrings(int argc, char * * argv)
{
    Strings args;
    argc--; argv++;
    while (argc--) args.push_back(*argv++);
    return args;
}

std::string renderLabels(const Strings & labels)
{
    std::string res;
    for (auto label : labels) {
        for (auto & c : label) c = std::toupper(c);
        res += " " ANSI_ITALIC + label + ANSI_NORMAL;
    }
    return res;
}

void printTable(std::ostream & out, const Table2 & table)
{
    size_t max = 0;
    for (auto & row : table)
        max = std::max(max, filterANSIEscapes(row.first, true).size());
    for (auto & row : table) {
        out << "  " << row.first
            << std::string(max - filterANSIEscapes(row.first, true).size() + 2, ' ')
            << row.second << "\n";
    }
}

void Command::printHelp(const string & programName, std::ostream & out)
{
    Args::printHelp(programName, out);

    auto exs = examples();
    if (!exs.empty()) {
        out << "\n" ANSI_BOLD "Examples:" ANSI_NORMAL "\n";
        for (auto & ex : exs)
            out << "\n"
                << "  " << ex.description << "\n" // FIXME: wrap
                << "  $ " << ex.command << "\n";
    }
}

nlohmann::json Command::toJSON()
{
    auto exs = nlohmann::json::array();

    for (auto & example : examples()) {
        auto ex = nlohmann::json::object();
        ex["description"] = example.description;
        ex["command"] = chomp(stripIndentation(example.command));
        exs.push_back(std::move(ex));
    }

    auto res = Args::toJSON();
    res["examples"] = std::move(exs);
    auto s = doc();
    if (s != "") res.emplace("doc", stripIndentation(s));
    return res;
}

MultiCommand::MultiCommand(const Commands & commands)
    : commands(commands)
{
    expectArgs({
        .label = "subcommand",
        .optional = true,
        .handler = {[=](std::string s) {
            assert(!command);
            if (auto alias = get(deprecatedAliases, s)) {
                warn("'%s' is a deprecated alias for '%s'", s, *alias);
                s = *alias;
            }
            if (auto prefix = needsCompletion(s)) {
                for (auto & [name, command] : commands)
                    if (hasPrefix(name, *prefix))
                        completions->insert(name);
            }
            auto i = commands.find(s);
            if (i == commands.end())
                throw UsageError("'%s' is not a recognised command", s);
            command = {s, i->second()};
        }}
    });

    categories[Command::catDefault] = "Available commands";
}

void MultiCommand::printHelp(const string & programName, std::ostream & out)
{
    if (command) {
        command->second->printHelp(programName + " " + command->first, out);
        return;
    }

    out << fmt(ANSI_BOLD "Usage:" ANSI_NORMAL " %s " ANSI_ITALIC "COMMAND FLAGS... ARGS..." ANSI_NORMAL "\n", programName);

    out << "\n" ANSI_BOLD "Common flags:" ANSI_NORMAL "\n";
    printFlags(out);

    std::map<Command::Category, std::map<std::string, ref<Command>>> commandsByCategory;

    for (auto & [name, commandFun] : commands) {
        auto command = commandFun();
        commandsByCategory[command->category()].insert_or_assign(name, command);
    }

    for (auto & [category, commands] : commandsByCategory) {
        out << fmt("\n" ANSI_BOLD "%s:" ANSI_NORMAL "\n", categories[category]);

        Table2 table;
        for (auto & [name, command] : commands) {
            auto descr = command->description();
            if (!descr.empty())
                table.push_back(std::make_pair(name, descr));
        }
        printTable(out, table);
    }
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
        j["category"] = categories[command->category()];
        cmds[name] = std::move(j);
    }

    auto res = Args::toJSON();
    res["commands"] = std::move(cmds);
    return res;
}

}
