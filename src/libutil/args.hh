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

    virtual void printHelp(const string & programName, std::ostream & out);

    virtual std::string description() { return ""; }

protected:

    static const size_t ArityAny = std::numeric_limits<size_t>::max();

    /* Flags. */
    struct Flag
    {
        typedef std::shared_ptr<Flag> ptr;
        std::string longName;
        char shortName = 0;
        std::string description;
        Strings labels;
        size_t arity = 0;
        std::function<void(std::vector<std::string>)> handler;
        std::string category;
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

    class FlagMaker
    {
        Args & args;
        Flag::ptr flag;
        friend class Args;
        FlagMaker(Args & args) : args(args), flag(std::make_shared<Flag>()) { };
    public:
        ~FlagMaker();
        FlagMaker & longName(const std::string & s) { flag->longName = s; return *this; };
        FlagMaker & shortName(char s) { flag->shortName = s; return *this; };
        FlagMaker & description(const std::string & s) { flag->description = s; return *this; };
        FlagMaker & label(const std::string & l) { flag->arity = 1; flag->labels = {l}; return *this; };
        FlagMaker & labels(const Strings & ls) { flag->arity = ls.size(); flag->labels = ls; return *this; };
        FlagMaker & arity(size_t arity) { flag->arity = arity; return *this; };
        FlagMaker & handler(std::function<void(std::vector<std::string>)> handler) { flag->handler = handler; return *this; };
        FlagMaker & handler(std::function<void()> handler) { flag->handler = [handler](std::vector<std::string>) { handler(); }; return *this; };
        FlagMaker & handler(std::function<void(std::string)> handler) {
            flag->arity = 1;
            flag->handler = [handler](std::vector<std::string> ss) { handler(std::move(ss[0])); };
            return *this;
        };
        FlagMaker & category(const std::string & s) { flag->category = s; return *this; };

        template<class T>
        FlagMaker & dest(T * dest)
        {
            flag->arity = 1;
            flag->handler = [=](std::vector<std::string> ss) { *dest = ss[0]; };
            return *this;
        };

        template<class T>
        FlagMaker & set(T * dest, const T & val)
        {
            flag->arity = 0;
            flag->handler = [=](std::vector<std::string> ss) { *dest = val; };
            return *this;
        };

        FlagMaker & mkHashTypeFlag(HashType * ht);
    };

    FlagMaker mkFlag();

    /* Helper functions for constructing flags / positional
       arguments. */

    void mkFlag1(char shortName, const std::string & longName,
        const std::string & label, const std::string & description,
        std::function<void(std::string)> fun)
    {
        mkFlag()
            .shortName(shortName)
            .longName(longName)
            .labels({label})
            .description(description)
            .arity(1)
            .handler([=](std::vector<std::string> ss) { fun(ss[0]); });
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
        mkFlag()
            .shortName(shortName)
            .longName(longName)
            .description(description)
            .handler([=](std::vector<std::string> ss) { *dest = value; });
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
        mkFlag()
            .shortName(shortName)
            .longName(longName)
            .labels({"N"})
            .description(description)
            .arity(1)
            .handler([=](std::vector<std::string> ss) {
                I n;
                if (!string2Int(ss[0], n))
                    throw UsageError("flag '--%s' requires a integer argument", longName);
                fun(n);
            });
    }

    /* Expect a string argument. */
    void expectArg(const std::string & label, string * dest, bool optional = false)
    {
        expectedArgs.push_back(ExpectedArg{label, 1, optional, [=](std::vector<std::string> ss) {
            *dest = ss[0];
        }});
    }

    /* Expect 0 or more arguments. */
    void expectArgs(const std::string & label, std::vector<std::string> * dest)
    {
        expectedArgs.push_back(ExpectedArg{label, 0, false, [=](std::vector<std::string> ss) {
            *dest = std::move(ss);
        }});
    }

    friend class MultiCommand;
};

Strings argvToStrings(int argc, char * * argv);

/* Helper function for rendering argument labels. */
std::string renderLabels(const Strings & labels);

/* Helper function for printing 2-column tables. */
typedef std::vector<std::pair<std::string, std::string>> Table2;

void printTable(std::ostream & out, const Table2 & table);

}
