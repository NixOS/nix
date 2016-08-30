#pragma once

#include <iostream>
#include <vector>
#include <cassert>

namespace nix {

void toJSON(std::ostream & str, const char * start, const char * end);
void toJSON(std::ostream & str, const std::string & s);
void toJSON(std::ostream & str, const char * s);
void toJSON(std::ostream & str, unsigned long long n);
void toJSON(std::ostream & str, unsigned long n);
void toJSON(std::ostream & str, long n);
void toJSON(std::ostream & str, double f);
void toJSON(std::ostream & str, bool b);

class JSONWriter
{
protected:

    struct JSONState
    {
        std::ostream & str;
        bool indent;
        size_t depth = 0;
        std::vector<JSONWriter *> stack;
        JSONState(std::ostream & str, bool indent) : str(str), indent(indent) { }
        ~JSONState()
        {
            assert(stack.empty());
        }
    };

    JSONState * state;

    bool first = true;

    JSONWriter(std::ostream & str, bool indent);

    JSONWriter(JSONState * state);

    ~JSONWriter();

    void assertActive()
    {
        assert(!state->stack.empty() && state->stack.back() == this);
    }

    void comma();

    void indent();
};

class JSONObject;
class JSONPlaceholder;

class JSONList : JSONWriter
{
private:

    friend class JSONObject;
    friend class JSONPlaceholder;

    void open();

    JSONList(JSONState * state)
        : JSONWriter(state)
    {
        open();
    }

public:

    JSONList(std::ostream & str, bool indent = false)
        : JSONWriter(str, indent)
    {
        open();
    }

    ~JSONList();

    template<typename T>
    JSONList & elem(const T & v)
    {
        comma();
        toJSON(state->str, v);
        return *this;
    }

    JSONList list();

    JSONObject object();

    JSONPlaceholder placeholder();
};

class JSONObject : JSONWriter
{
private:

    friend class JSONList;
    friend class JSONPlaceholder;

    void open();

    JSONObject(JSONState * state)
        : JSONWriter(state)
    {
        open();
    }

    void attr(const std::string & s);

public:

    JSONObject(std::ostream & str, bool indent = false)
        : JSONWriter(str, indent)
    {
        open();
    }

    ~JSONObject();

    template<typename T>
    JSONObject & attr(const std::string & name, const T & v)
    {
        attr(name);
        toJSON(state->str, v);
        return *this;
    }

    JSONList list(const std::string & name);

    JSONObject object(const std::string & name);

    JSONPlaceholder placeholder(const std::string & name);
};

class JSONPlaceholder : JSONWriter
{

private:

    friend class JSONList;
    friend class JSONObject;

    JSONPlaceholder(JSONState * state)
        : JSONWriter(state)
    {
    }

    void assertValid()
    {
        assertActive();
        assert(first);
    }

public:

    JSONPlaceholder(std::ostream & str, bool indent = false)
        : JSONWriter(str, indent)
    {
    }

    ~JSONPlaceholder()
    {
        assert(!first || std::uncaught_exception());
    }

    template<typename T>
    void write(const T & v)
    {
        assertValid();
        first = false;
        toJSON(state->str, v);
    }

    JSONList list();

    JSONObject object();
};

}
