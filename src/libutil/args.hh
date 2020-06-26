#pragma once

#include <iostream>
#include <map>
#include <memory>

#include "util.hh"

namespace nix {

enum HashType : char;

class Args
{
public:

    /* Parse the command line, throwing a UsageError if something goes
       wrong. */
    void parseCmdline(const Strings & cmdline);

    virtual void printHelp(const string & programName, std::ostream & out);

    virtual std::string description() { return ""; }

protected:

    static const size_t ArityAny = std::numeric_limits<size_t>::max();

    struct Handler
    {
        std::function<void(std::vector<std::string>)> fun;
        size_t arity;

        Handler() {}

        Handler(std::function<void(std::vector<std::string>)> && fun)
            : fun(std::move(fun))
            , arity(ArityAny)
        { }

        Handler(std::function<void()> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string>) { handler(); })
            , arity(0)
        { }

        Handler(std::function<void(std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) {
                handler(std::move(ss[0]));
              })
            , arity(1)
        { }

        Handler(std::function<void(std::string, std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) {
                handler(std::move(ss[0]), std::move(ss[1]));
              })
            , arity(2)
        { }

        Handler(std::vector<std::string> * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss; })
            , arity(ArityAny)
        { }

        template<class T>
        Handler(T * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        { }

        template<class T>
        Handler(T * dest, const T & val)
            : fun([=](std::vector<std::string> ss) { *dest = val; })
            , arity(0)
        { }
    };

    /* Flags. */
    struct Flag
    {
        typedef std::shared_ptr<Flag> ptr;

        std::string longName;
        char shortName = 0;
        std::string description;
        std::string category;
        Strings labels;
        Handler handler;
        std::function<void(size_t, std::string_view)> completer;

        static Flag mkHashTypeFlag(std::string && longName, HashType * ht);
        static Flag mkHashTypeOptFlag(std::string && longName, std::optional<HashType> * oht);
    };

    std::map<std::string, Flag::ptr> longFlags;
    std::map<char, Flag::ptr> shortFlags;

    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end);

    virtual void printFlags(std::ostream & out);

    /* Positional arguments. */
    struct ExpectedArg
    {
        std::string label;
        bool optional = false;
        Handler handler;
        std::function<void(size_t, std::string_view)> completer;
    };

    std::list<ExpectedArg> expectedArgs;

    virtual bool processArgs(const Strings & args, bool finish);

    std::set<std::string> hiddenCategories;

public:

    void addFlag(Flag && flag);

    /* Helper functions for constructing flags / positional
       arguments. */

    void mkFlag1(char shortName, const std::string & longName,
        const std::string & label, const std::string & description,
        std::function<void(std::string)> fun)
    {
        addFlag({
            .longName = longName,
            .shortName = shortName,
            .description = description,
            .labels = {label},
            .handler = {[=](std::string s) { fun(s); }}
        });
    }

    void mkFlag(char shortName, const std::string & name,
        const std::string & description, bool * dest)
    {
        mkFlag(shortName, name, description, dest, true);
    }

    template<class T>
    void mkFlag(char shortName, const std::string & longName, const std::string & description,
        T * dest, const T & value)
    {
        addFlag({
            .longName = longName,
            .shortName = shortName,
            .description = description,
            .handler = {[=]() { *dest = value; }}
        });
    }

    template<class I>
    void mkIntFlag(char shortName, const std::string & longName,
        const std::string & description, I * dest)
    {
        mkFlag<I>(shortName, longName, description, [=](I n) {
            *dest = n;
        });
    }

    template<class I>
    void mkFlag(char shortName, const std::string & longName,
        const std::string & description, std::function<void(I)> fun)
    {
        addFlag({
            .longName = longName,
            .shortName = shortName,
            .description = description,
            .labels = {"N"},
            .handler = {[=](std::string s) {
                I n;
                if (!string2Int(s, n))
                    throw UsageError("flag '--%s' requires a integer argument", longName);
                fun(n);
            }}
        });
    }

    void expectArgs(ExpectedArg && arg)
    {
        expectedArgs.emplace_back(std::move(arg));
    }

    /* Expect a string argument. */
    void expectArg(const std::string & label, string * dest, bool optional = false)
    {
        expectArgs({
            .label = label,
            .optional = true,
            .handler = {dest}
        });
    }

    /* Expect 0 or more arguments. */
    void expectArgs(const std::string & label, std::vector<std::string> * dest)
    {
        expectArgs({
            .label = label,
            .handler = {dest}
        });
    }

    friend class MultiCommand;
};

/* A command is an argument parser that can be executed by calling its
   run() method. */
struct Command : virtual Args
{
    friend class MultiCommand;

    virtual ~Command() { }

    virtual void prepare() { };
    virtual void run() = 0;

    struct Example
    {
        std::string description;
        std::string command;
    };

    typedef std::list<Example> Examples;

    virtual Examples examples() { return Examples(); }

    typedef int Category;

    static constexpr Category catDefault = 0;

    virtual Category category() { return catDefault; }

    void printHelp(const string & programName, std::ostream & out) override;
};

typedef std::map<std::string, std::function<ref<Command>()>> Commands;

/* An argument parser that supports multiple subcommands,
   i.e. ‘<command> <subcommand>’. */
class MultiCommand : virtual Args
{
public:
    Commands commands;

    std::map<Command::Category, std::string> categories;

    std::map<std::string, std::string> deprecatedAliases;

    // Selected command, if any.
    std::optional<std::pair<std::string, ref<Command>>> command;

    MultiCommand(const Commands & commands);

    void printHelp(const string & programName, std::ostream & out) override;

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};

Strings argvToStrings(int argc, char * * argv);

/* Helper function for rendering argument labels. */
std::string renderLabels(const Strings & labels);

/* Helper function for printing 2-column tables. */
typedef std::vector<std::pair<std::string, std::string>> Table2;

void printTable(std::ostream & out, const Table2 & table);

extern std::shared_ptr<std::set<std::string>> completions;
extern bool pathCompletions;

std::optional<std::string> needsCompletion(std::string_view s);

void completePath(size_t, std::string_view prefix);

void completeDir(size_t, std::string_view prefix);

}
