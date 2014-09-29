#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & out, PathSet & context);

void escapeJSON(std::ostream & str, const string & s);

struct JSONObject
{
    std::ostream & str;
    bool first;
    JSONObject(std::ostream & str) : str(str), first(true)
    {
        str << "{";
    }
    ~JSONObject()
    {
        str << "}";
    }
    void attr(const string & s)
    {
        if (!first) str << ","; else first = false;
        escapeJSON(str, s);
        str << ":";
    }
    void attr(const string & s, const string & t)
    {
        attr(s);
        escapeJSON(str, t);
    }
    void attr(const string & s, int n)
    {
        attr(s);
        str << n;
    }
};

struct JSONList
{
    std::ostream & str;
    bool first;
    JSONList(std::ostream & str) : str(str), first(true)
    {
        str << "[";
    }
    ~JSONList()
    {
        str << "]";
    }
    void elem()
    {
        if (!first) str << ","; else first = false;
    }
    void elem(const string & s)
    {
        elem();
        escapeJSON(str, s);
    }
};

}
