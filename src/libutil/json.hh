#pragma once

#include <iostream>
#include <vector>
#include <cassert>

namespace nix {

void toJSON(std::ostream & str, const char * start, const char * end);
void toJSON(std::ostream & str, const char * s);

template<typename T>
void toJSON(std::ostream & str, T n);

// Avoid unintentinoal copying
template<> void toJSON<std::string>(std::ostream & str, std::string s) = delete;

class JSONWriter
{
protected:

    struct JSONState
    {
        std::ostream & str;
        bool indent;
        size_t depth = 0;
        size_t stack = 0;
        JSONState(std::ostream & str, bool indent) : str(str), indent(indent) { }
        ~JSONState()
        {
            assert(stack == 0);
        }
    };

    JSONState * state;

    bool first = true;

    JSONWriter(std::ostream & str, bool indent);

    JSONWriter(JSONState * state);

    ~JSONWriter();

    void assertActive()
    {
        assert(state->stack != 0);
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
    JSONList & elem(const T v)
    {
        comma();
        toJSON(state->str, v);
        return *this;
    }

    JSONList & elem(const std::string v) = delete;

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

    void attr(std::string_view s);

public:

    JSONObject(std::ostream & str, bool indent = false)
        : JSONWriter(str, indent)
    {
        open();
    }

    JSONObject(const JSONObject & obj) = delete;

    JSONObject(JSONObject && obj)
        : JSONWriter(obj.state)
    {
        obj.state = 0;
    }

    ~JSONObject();

    template<typename T>
    JSONObject & attr(std::string_view name, const T v)
    {
        attr(name);
        toJSON(state->str, v);
        return *this;
    }

    JSONObject & attr(std::string_view name, std::string v) = delete;

    JSONObject & attr(std::string_view name, const std::string & v) {
        return attr(name, std::string_view { v });
    };

    JSONList list(std::string_view name);

    JSONObject object(std::string_view name);

    JSONPlaceholder placeholder(std::string_view name);
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

    ~JSONPlaceholder();

    template<typename T>
    void write(const T v)
    {
        assertValid();
        first = false;
        toJSON(state->str, v);
    }

    JSONList list();

    JSONObject object();
};

}
