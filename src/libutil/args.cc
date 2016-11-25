#include "args.hh"
#include "hash.hh"

namespace nix {

void Args::parseCmdline(const Strings & _cmdline)
{
    Strings pendingArgs;
    bool dashDash = false;

    Strings cmdline(_cmdline);

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
                throw UsageError(format("unrecognised flag ‘%1%’") % arg);
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
    std::cout << "Usage: " << programName << " <FLAGS>...";
    for (auto & exp : expectedArgs) {
        std::cout << renderLabels({exp.label});
        // FIXME: handle arity > 1
        if (exp.arity == 0) std::cout << "...";
    }
    std::cout << "\n";

    auto s = description();
    if (s != "")
        std::cout << "\nSummary: " << s << ".\n";

    if (longFlags.size()) {
        std::cout << "\n";
        std::cout << "Flags:\n";
        printFlags(out);
    }
}

void Args::printFlags(std::ostream & out)
{
    Table2 table;
    for (auto & flag : longFlags)
        table.push_back(std::make_pair(
                (flag.second.shortName ? std::string("-") + flag.second.shortName + ", " : "    ")
                + "--" + flag.first + renderLabels(flag.second.labels),
                flag.second.description));
    printTable(out, table);
}

bool Args::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    assert(pos != end);

    auto process = [&](const std::string & name, const Flag & flag) -> bool {
        ++pos;
        Strings args;
        for (size_t n = 0 ; n < flag.arity; ++n) {
            if (pos == end)
                throw UsageError(format("flag ‘%1%’ requires %2% argument(s)")
                    % name % flag.arity);
            args.push_back(*pos++);
        }
        flag.handler(args);
        return true;
    };

    if (string(*pos, 0, 2) == "--") {
        auto i = longFlags.find(string(*pos, 2));
        if (i == longFlags.end()) return false;
        return process("--" + i->first, i->second);
    }

    if (string(*pos, 0, 1) == "-" && pos->size() == 2) {
        auto c = (*pos)[1];
        auto i = shortFlags.find(c);
        if (i == shortFlags.end()) return false;
        return process(std::string("-") + c, i->second);
    }

    return false;
}

bool Args::processArgs(const Strings & args, bool finish)
{
    if (expectedArgs.empty()) {
        if (!args.empty())
            throw UsageError(format("unexpected argument ‘%1%’") % args.front());
        return true;
    }

    auto & exp = expectedArgs.front();

    bool res = false;

    if ((exp.arity == 0 && finish) ||
        (exp.arity > 0 && args.size() == exp.arity))
    {
        exp.handler(args);
        expectedArgs.pop_front();
        res = true;
    }

    if (finish && !expectedArgs.empty())
        throw UsageError("more arguments are required");

    return res;
}

void Args::mkHashTypeFlag(const std::string & name, HashType * ht)
{
    mkFlag1(0, name, "TYPE", "hash algorithm (‘md5’, ‘sha1’, ‘sha256’, or ‘sha512’)", [=](std::string s) {
        *ht = parseHashType(s);
        if (*ht == htUnknown)
            throw UsageError(format("unknown hash type ‘%1%’") % s);
    });
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
        res += " <" + label + ">";
    }
    return res;
}

void printTable(std::ostream & out, const Table2 & table)
{
    size_t max = 0;
    for (auto & row : table)
        max = std::max(max, row.first.size());
    for (auto & row : table) {
        out << "  " << row.first
            << std::string(max - row.first.size() + 2, ' ')
            << row.second << "\n";
    }
}

}
