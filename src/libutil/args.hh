#pragma once
///@file

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

    /**
     * Parse the command line, throwing a UsageError if something goes
     * wrong.
     */
    void parseCmdline(const Strings & cmdline);

    /**
     * Return a short one-line description of the command.
     */
    virtual std::string description() { return ""; }

    virtual bool forceImpureByDefault() { return false; }

    /**
     * Return documentation about this command, in Markdown format.
     */
    virtual std::string doc() { return ""; }

protected:

    /**
     * The largest `size_t` is used to indicate the "any" arity, for
     * handlers/flags/arguments that accept an arbitrary number of
     * arguments.
     */
    static const size_t ArityAny = std::numeric_limits<size_t>::max();

    /**
     * Arguments (flags/options and positional) have a "handler" which is
     * caused when the argument is parsed. The handler has an arbitrary side
     * effect, including possible affect further command-line parsing.
     *
     * There are many constructors in order to support many shorthand
     * initializations, and this is used a lot.
     */
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

    /**
     * Description of flags / options
     *
     * These are arguments like `-s` or `--long` that can (mostly)
     * appear in any order.
     */
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

        std::optional<ExperimentalFeature> experimentalFeature;

        static Flag mkHashTypeFlag(std::string && longName, HashType * ht);
        static Flag mkHashTypeOptFlag(std::string && longName, std::optional<HashType> * oht);
    };

    /**
     * Index of all registered "long" flag descriptions (flags like
     * `--long`).
     */
    std::map<std::string, Flag::ptr> longFlags;

    /**
     * Index of all registered "short" flag descriptions (flags like
     * `-s`).
     */
    std::map<char, Flag::ptr> shortFlags;

    /**
     * Process a single flag and its arguments, pulling from an iterator
     * of raw CLI args as needed.
     */
    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end);

    /**
     * Description of positional arguments
     *
     * These are arguments that do not start with a `-`, and for which
     * the order does matter.
     */
    struct ExpectedArg
    {
        std::string label;
        bool optional = false;
        Handler handler;
        std::function<void(size_t, std::string_view)> completer;
    };

    /**
     * Queue of expected positional argument forms.
     *
     * Positional arugment descriptions are inserted on the back.
     *
     * As positional arguments are passed, these are popped from the
     * front, until there are hopefully none left as all args that were
     * expected in fact were passed.
     */
    std::list<ExpectedArg> expectedArgs;

    /**
     * Process some positional arugments
     *
     * @param finish: We have parsed everything else, and these are the only
     * arguments left. Used because we accumulate some "pending args" we might
     * have left over.
     */
    virtual bool processArgs(const Strings & args, bool finish);

    virtual Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos)
    { return pos; }

    std::set<std::string> hiddenCategories;

    /**
     * Called after all command line flags before the first non-flag
     * argument (if any) have been processed.
     */
    virtual void initialFlagsProcessed() {}

    /**
     * Called after the command line has been processed if we need to generate
     * completions. Useful for commands that need to know the whole command line
     * in order to know what completions to generate.
     */
    virtual void completionHook() { }

public:

    void addFlag(Flag && flag);

    void removeFlag(const std::string & longName);

    void expectArgs(ExpectedArg && arg)
    {
        expectedArgs.emplace_back(std::move(arg));
    }

    /**
     * Expect a string argument.
     */
    void expectArg(const std::string & label, std::string * dest, bool optional = false)
    {
        expectArgs({
            .label = label,
            .optional = optional,
            .handler = {dest}
        });
    }

    /**
     * Expect 0 or more arguments.
     */
    void expectArgs(const std::string & label, std::vector<std::string> * dest)
    {
        expectArgs({
            .label = label,
            .handler = {dest}
        });
    }

    virtual nlohmann::json toJSON();

    friend class MultiCommand;

    /**
     * The parent command, used if this is a subcommand.
     */
    MultiCommand * parent = nullptr;

private:

    /**
     * Experimental features needed when parsing args. These are checked
     * after flag parsing is completed in order to support enabling
     * experimental features coming after the flag that needs the
     * experimental feature.
     */
    std::set<ExperimentalFeature> flagExperimentalFeatures;
};

/**
 * A command is an argument parser that can be executed by calling its
 * run() method.
 */
struct Command : virtual public Args
{
    friend class MultiCommand;

    virtual ~Command() { }

    /**
     * Entry point to the command
     */
    virtual void run() = 0;

    typedef int Category;

    static constexpr Category catDefault = 0;

    virtual std::optional<ExperimentalFeature> experimentalFeature();

    virtual Category category() { return catDefault; }
};

typedef std::map<std::string, std::function<ref<Command>()>> Commands;

/**
 * An argument parser that supports multiple subcommands,
 * i.e. ‘<command> <subcommand>’.
 */
class MultiCommand : virtual public Args
{
public:
    Commands commands;

    std::map<Command::Category, std::string> categories;

    /**
     * Selected command, if any.
     */
    std::optional<std::pair<std::string, ref<Command>>> command;

    MultiCommand(const Commands & commands);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;

    void completionHook() override;

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

void completePath(size_t, std::string_view prefix);

void completeDir(size_t, std::string_view prefix);

}
