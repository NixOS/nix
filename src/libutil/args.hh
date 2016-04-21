#pragma once

#include <iostream>
#include <map>
#include <memory>

#include "util.hh"

namespace nix {

MakeError(UsageError, nix::Error);

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

    /* Flags. */
    struct Flag
    {
        char shortName;
        std::string description;
        Strings labels;
        size_t arity;
        std::function<void(Strings)> handler;
    };

    std::map<std::string, Flag> longFlags;
    std::map<char, Flag> shortFlags;

    virtual bool processFlag(Strings::iterator & pos, Strings::iterator end);

    void printFlags(std::ostream & out);

    /* Positional arguments. */
    struct ExpectedArg
    {
        std::string label;
        size_t arity; // 0 = any
        std::function<void(Strings)> handler;
    };

    std::list<ExpectedArg> expectedArgs;

    virtual bool processArgs(const Strings & args, bool finish);

public:

    /* Helper functions for constructing flags / positional
       arguments. */

    void mkFlag(char shortName, const std::string & longName,
        const Strings & labels, const std::string & description,
        size_t arity, std::function<void(Strings)> handler)
    {
        auto flag = Flag{shortName, description, labels, arity, handler};
        if (shortName) shortFlags[shortName] = flag;
        longFlags[longName] = flag;
    }

    void mkFlag(char shortName, const std::string & longName,
        const std::string & description, std::function<void()> fun)
    {
        mkFlag(shortName, longName, {}, description, 0, std::bind(fun));
    }

    void mkFlag1(char shortName, const std::string & longName,
        const std::string & label, const std::string & description,
        std::function<void(std::string)> fun)
    {
        mkFlag(shortName, longName, {label}, description, 1, [=](Strings ss) {
            fun(ss.front());
        });
    }

    void mkFlag(char shortName, const std::string & name,
        const std::string & description, bool * dest)
    {
        mkFlag(shortName, name, description, dest, true);
    }

    void mkFlag(char shortName, const std::string & longName,
        const std::string & label, const std::string & description,
        string * dest)
    {
        mkFlag1(shortName, longName, label, description, [=](std::string s) {
            *dest = s;
        });
    }

    void mkHashTypeFlag(const std::string & name, HashType * ht);

    template<class T>
    void mkFlag(char shortName, const std::string & longName, const std::string & description,
        T * dest, const T & value)
    {
        mkFlag(shortName, longName, {}, description, 0, [=](Strings ss) {
            *dest = value;
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
        mkFlag(shortName, longName, {"N"}, description, 1, [=](Strings ss) {
            I n;
            if (!string2Int(ss.front(), n))
                throw UsageError(format("flag ‘--%1%’ requires a integer argument") % longName);
            fun(n);
        });
    }

    /* Expect a string argument. */
    void expectArg(const std::string & label, string * dest)
    {
        expectedArgs.push_back(ExpectedArg{label, 1, [=](Strings ss) {
            *dest = ss.front();
        }});
    }

    /* Expect 0 or more arguments. */
    void expectArgs(const std::string & label, Strings * dest)
    {
        expectedArgs.push_back(ExpectedArg{label, 0, [=](Strings ss) {
            *dest = ss;
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
