#pragma once

#include <iostream>
#include <map>
#include <memory>

#include "util.hh"

namespace nix {

MakeError(UsageError, Error);

enum HashType : char;

class Args
{
public:

    /* Parse the command line, throwing a UsageError if something goes
       wrong. */
    void parseCmdline(const Strings & cmdline);

    virtual void printHelp(std::string_view programName, std::ostream & out);

    virtual std::string description() { return ""; }

protected:

    static const size_t ArityAny = std::numeric_limits<size_t>::max();

    /* Flags. */
    struct Flag
    {
        typedef std::shared_ptr<Flag> ptr;

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

        std::string longName;
        char shortName = 0;
        std::string description;
        std::string category;
        Strings labels;
        Handler handler;

        static Flag mkHashTypeFlag(std::string && longName, HashType * ht);
    };

    std::map<std::string, Flag::ptr> longFlags;
    std::map<char, Flag::ptr> shortFlags;

    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end);

    virtual void printFlags(std::ostream & out);

    /* Positional arguments. */
    struct ExpectedArg
    {
        std::string label;
        size_t arity; // 0 = any
        bool optional;
        std::function<void(std::vector<std::string>)> handler;
    };

    std::list<ExpectedArg> expectedArgs;

    virtual bool processArgs(const Strings & args, bool finish);

    std::set<std::string> hiddenCategories;

public:

    void addFlag(Flag && flag);

    /* Helper functions for constructing flags / positional
       arguments. */

    // TODO move strings, not copy string views
    void mkFlag1(char shortName, std::string_view longName,
        std::string_view label, std::string_view description,
        std::function<void(std::string)> fun)
    {
        addFlag({
            .longName = std::string { longName },
            .shortName = shortName,
            .description = std::string { description },
            .labels = { std::string { label } },
            .handler = {[=](std::string s) { fun(s); }}
        });
    }

    void mkFlag(char shortName, std::string_view name,
        std::string_view description, bool * dest)
    {
        mkFlag(shortName, name, description, dest, true);
    }

    // TODO move strings, not copy string views, also move value
    template<class T>
    void mkFlag(char shortName, std::string_view longName, std::string_view description,
        T * dest, const T & value)
    {
        addFlag({
            .longName = std::string { longName },
            .shortName = shortName,
            .description = std::string { description },
            .handler = {[=]() { *dest = value; }}
        });
    }

    template<class I>
    void mkIntFlag(char shortName, std::string_view longName,
        std::string_view description, I * dest)
    {
        mkFlag<I>(shortName, longName, description, [=](I n) {
            *dest = n;
        });
    }

    template<class I>
    void mkFlag(char shortName, std::string_view longName,
        std::string_view description, std::function<void(I)> fun)
    {
        addFlag({
            .longName = std::string { longName },
            .shortName = shortName,
            .description = std::string { description },
            .labels = {"N"},
            .handler = {[=](std::string s) {
                I n;
                if (!string2Int(s, n))
                    throw UsageError("flag '--%s' requires a integer argument", longName);
                fun(n);
            }}
        });
    }

    /* Expect a string argument. */
    void expectArg(std::string_view label, string * dest, bool optional = false)
    {
        expectedArgs.push_back(ExpectedArg{std::string { label }, 1, optional, [=](std::vector<std::string> ss) {
            *dest = ss[0];
        }});
    }

    /* Expect 0 or more arguments. */
    void expectArgs(std::string_view label, std::vector<std::string> * dest)
    {
        expectedArgs.push_back(ExpectedArg{std::string { label }, 0, false, [=](std::vector<std::string> ss) {
            *dest = std::move(ss);
        }});
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

    void printHelp(std::string_view programName, std::ostream & out) override;
};

typedef std::map<std::string, std::function<ref<Command>()>, std::less<>> Commands;

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

    void printHelp(std::string_view programName, std::ostream & out) override;

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};

Strings argvToStrings(int argc, char * * argv);

/* Helper function for rendering argument labels. */
std::string renderLabels(const Strings & labels);

/* Helper function for printing 2-column tables. */
typedef std::vector<std::pair<std::string, std::string>> Table2;

void printTable(std::ostream & out, const Table2 & table);

}
