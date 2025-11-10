#pragma once
///@file

#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

#include <nlohmann/json_fwd.hpp>

#include "nix/util/types.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/ref.hh"

namespace nix {

enum struct HashAlgorithm : char;
enum struct HashFormat : int;

class MultiCommand;

class RootArgs;

class AddCompletions;

class Args
{

public:

    /**
     * Return a short one-line description of the command.
     */
    virtual std::string description()
    {
        return "";
    }

    virtual bool forceImpureByDefault()
    {
        return false;
    }

    /**
     * Return documentation about this command, in Markdown format.
     */
    virtual std::string doc()
    {
        return "";
    }

    /**
     * @brief Get the [base directory](https://nix.dev/manual/nix/development/glossary.html#gloss-base-directory) for
     * the command.
     *
     * @return Generally the working directory, but in case of a shebang
     *         interpreter, returns the directory of the script.
     *
     * This only returns the correct value after parseCmdline() has run.
     */
    virtual Path getCommandBaseDir() const;

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

        Handler() = default;

        Handler(std::function<void(std::vector<std::string>)> && fun)
            : fun(std::move(fun))
            , arity(ArityAny)
        {
        }

        Handler(std::function<void()> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string>) { handler(); })
            , arity(0)
        {
        }

        Handler(std::function<void(std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) { handler(std::move(ss[0])); })
            , arity(1)
        {
        }

        Handler(std::function<void(std::string, std::string)> && handler)
            : fun([handler{std::move(handler)}](std::vector<std::string> ss) {
                handler(std::move(ss[0]), std::move(ss[1]));
            })
            , arity(2)
        {
        }

        Handler(std::vector<std::string> * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = ss; })
            , arity(ArityAny)
        {
        }

        Handler(std::string * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        {
        }

        Handler(std::optional<std::string> * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        {
        }

        Handler(std::filesystem::path * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        {
        }

        Handler(std::optional<std::filesystem::path> * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = ss[0]; })
            , arity(1)
        {
        }

        template<class T>
        Handler(T * dest, const T & val)
            : fun([dest, val](std::vector<std::string> ss) { *dest = val; })
            , arity(0)
        {
        }

        template<class I>
        Handler(I * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = string2IntWithUnitPrefix<I>(ss[0]); })
            , arity(1)
        {
        }

        template<class I>
        Handler(std::optional<I> * dest)
            : fun([dest](std::vector<std::string> ss) { *dest = string2IntWithUnitPrefix<I>(ss[0]); })
            , arity(1)
        {
        }
    };

    /**
     * The basic function type of the completion callback.
     *
     * Used to define `CompleterClosure` and some common case completers
     * that individual flags/arguments can use.
     *
     * The `AddCompletions` that is passed is an interface to the state
     * stored as part of the root command
     */
    using CompleterFun = void(AddCompletions &, size_t, std::string_view);

    /**
     * The closure type of the completion callback.
     *
     * This is what is actually stored as part of each Flag / Expected
     * Arg.
     */
    using CompleterClosure = std::function<CompleterFun>;

public:

    /**
     * Description of flags / options
     *
     * These are arguments like `-s` or `--long` that can (mostly)
     * appear in any order.
     */
    struct Flag
    {
        using ptr = std::shared_ptr<Flag>;

        std::string longName;
        StringSet aliases;
        char shortName = 0;
        std::string description;
        std::string category;
        Strings labels;
        Handler handler;
        CompleterClosure completer;
        bool required = false;

        std::optional<ExperimentalFeature> experimentalFeature;

        // FIXME: this should be private, but that breaks designated initializers.
        size_t timesUsed = 0;
    };

protected:

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

public:

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
        CompleterClosure completer;
    };

protected:

    /**
     * Queue of expected positional argument forms.
     *
     * Positional argument descriptions are inserted on the back.
     *
     * As positional arguments are passed, these are popped from the
     * front, until there are hopefully none left as all args that were
     * expected in fact were passed.
     */
    std::list<ExpectedArg> expectedArgs;
    /**
     * List of processed positional argument forms.
     *
     * All items removed from `expectedArgs` are added here. After all
     * arguments were processed, this list should be exactly the same as
     * `expectedArgs` was before.
     *
     * This list is used to extend the lifetime of the argument forms.
     * If this is not done, some closures that reference the command
     * itself will segfault.
     */
    std::list<ExpectedArg> processedArgs;

    /**
     * Process some positional arguments
     *
     * @param finish: We have parsed everything else, and these are the only
     * arguments left. Used because we accumulate some "pending args" we might
     * have left over.
     */
    virtual bool processArgs(const Strings & args, bool finish);

    virtual Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos)
    {
        return pos;
    }

    StringSet hiddenCategories;

    virtual void checkArgs();

    /**
     * Called after all command line flags before the first non-flag
     * argument (if any) have been processed.
     */
    virtual void initialFlagsProcessed() {}

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
        expectArgs({.label = label, .optional = optional, .handler = {dest}});
    }

    /**
     * Expect a path argument.
     */
    void expectArg(const std::string & label, std::filesystem::path * dest, bool optional = false)
    {
        expectArgs({.label = label, .optional = optional, .handler = {dest}});
    }

    /**
     * Expect 0 or more arguments.
     */
    void expectArgs(const std::string & label, std::vector<std::string> * dest)
    {
        expectArgs({.label = label, .handler = {dest}});
    }

    static CompleterFun completePath;

    static CompleterFun completeDir;

    virtual nlohmann::json toJSON();

    friend class MultiCommand;

    /**
     * The parent command, used if this is a subcommand.
     *
     * Invariant: An Args with a null parent must also be a RootArgs
     *
     * \todo this would probably be better in the CommandClass.
     * getRoot() could be an abstract method that peels off at most one
     * layer before recuring.
     */
    MultiCommand * parent = nullptr;

    /**
     * Traverse parent pointers until we find the \ref RootArgs "root
     * arguments" object.
     */
    RootArgs & getRoot();
};

/**
 * A command is an argument parser that can be executed by calling its
 * run() method.
 */
struct Command : virtual public Args
{
    friend class MultiCommand;

    virtual ~Command() = default;

    /**
     * Entry point to the command
     */
    virtual void run() = 0;

    using Category = int;

    static constexpr Category catDefault = 0;

    virtual std::optional<ExperimentalFeature> experimentalFeature();

    virtual Category category()
    {
        return catDefault;
    }
};

using Commands = std::map<std::string, std::function<ref<Command>()>>;

/**
 * An argument parser that supports multiple subcommands,
 * i.e. `<command> <subcommand>`.
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

    MultiCommand(std::string_view commandName, const Commands & commands);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;

    nlohmann::json toJSON() override;

    enum struct AliasStatus {
        /** Aliases that don't go away */
        AcceptedShorthand,
        /** Aliases that will go away */
        Deprecated,
    };

    /** An alias, except for the original syntax, which is in the map key. */
    struct AliasInfo
    {
        AliasStatus status;
        std::vector<std::string> replacement;
    };

    /**
     * A list of aliases (remapping a deprecated/shorthand subcommand
     * to something else).
     */
    std::map<std::string, AliasInfo> aliases;

    Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos) override;

protected:
    std::string commandName = "";
    bool aliasUsed = false;

    void checkArgs() override;
};

Strings argvToStrings(int argc, char ** argv);

struct Completion
{
    std::string completion;
    std::string description;

    auto operator<=>(const Completion & other) const noexcept;
};

/**
 * The abstract interface for completions callbacks
 *
 * The idea is to restrict the callback so it can only add additional
 * completions to the collection, or set the completion type. By making
 * it go through this interface, the callback cannot make any other
 * changes, or even view the completions / completion type that have
 * been set so far.
 */
class AddCompletions
{
public:

    /**
     * The type of completion we are collecting.
     */
    enum class Type {
        Normal,
        Filenames,
        Attrs,
    };

    /**
     * Set the type of the completions being collected
     *
     * \todo it should not be possible to change the type after it has been set.
     */
    virtual void setType(Type type) = 0;

    /**
     * Add a single completion to the collection
     */
    virtual void add(std::string completion, std::string description = "") = 0;
};

Strings parseShebangContent(std::string_view s);

} // namespace nix
