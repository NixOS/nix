#pragma once

#include <iostream>
#include <map>
#include <memory>

#include <nlohmann/json_fwd.hpp>

#include "util.hh"

namespace nix {

enum HashType : char;

class MultiCommand;

class Args
{
public:

    /* Parse the command line, throwing a UsageError if something goes
       wrong. */
    void parseCmdline(const Strings & cmdline);

    /* Return a short one-line description of the command. */
    virtual std::string description() { return ""; }

    /* Return documentation about this command, in Markdown format. */
    virtual std::string doc() { return ""; }

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

        Handler(std::string * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        { }

        Handler(std::optional<std::string> * dest)
            : fun([=](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        { }

        template<class T>
        Handler(T * dest, const T & val)
            : fun([=](std::vector<std::string> ss) { *dest = val; })
            , arity(0)
        { }

        template<class I>
        Handler(I * dest)
            : fun([=](std::vector<std::string> ss) {
                *dest = string2IntWithUnitPrefix<I>(ss[0]);
              })
            , arity(1)
        { }

        template<class I>
        Handler(std::optional<I> * dest)
            : fun([=](std::vector<std::string> ss) {
                *dest = string2IntWithUnitPrefix<I>(ss[0]);
            })
            , arity(1)
        { }
    };

    /* Options. */
    struct Flag
    {
        typedef std::shared_ptr<Flag> ptr;

        std::string longName;
        std::set<std::string> aliases;
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

    virtual Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos)
    { return pos; }

    std::set<std::string> hiddenCategories;

    /* Called after all command line flags before the first non-flag
       argument (if any) have been processed. */
    virtual void initialFlagsProcessed() {}

public:

    void addFlag(Flag && flag);

    void removeFlag(const std::string & longName);

    void expectArgs(ExpectedArg && arg)
    {
        expectedArgs.emplace_back(std::move(arg));
    }

    /* Expect a string argument. */
    void expectArg(const std::string & label, std::string * dest, bool optional = false)
    {
        expectArgs({
            .label = label,
            .optional = optional,
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

    virtual nlohmann::json toJSON();

    friend class MultiCommand;

    MultiCommand * parent = nullptr;
};

/* A command is an argument parser that can be executed by calling its
   run() method. */
struct Command : virtual public Args
{
    friend class MultiCommand;

    virtual ~Command() { }

    virtual void prepare() { };
    virtual void run() = 0;

    typedef int Category;

    static constexpr Category catDefault = 0;

    virtual Category category() { return catDefault; }
};

typedef std::map<std::string, std::function<ref<Command>()>> Commands;

/* An argument parser that supports multiple subcommands,
   i.e. ‘<command> <subcommand>’. */
class MultiCommand : virtual public Args
{
public:
    Commands commands;

    std::map<Command::Category, std::string> categories;

    // Selected command, if any.
    std::optional<std::pair<std::string, ref<Command>>> command;

    MultiCommand(const Commands & commands);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;

    nlohmann::json toJSON() override;
};

Strings argvToStrings(int argc, char * * argv);

struct Completion {
    std::string completion;
    std::string description;

    bool operator<(const Completion & other) const;
};
class Completions : public std::set<Completion> {
public:
    void add(std::string completion, std::string description = "");
};
extern std::shared_ptr<Completions> completions;

enum CompletionType {
    ctNormal,
    ctFilenames,
    ctAttrs
};
extern CompletionType completionType;

std::optional<std::string> needsCompletion(std::string_view s);

void completePath(size_t, std::string_view prefix);

void completeDir(size_t, std::string_view prefix);

}
