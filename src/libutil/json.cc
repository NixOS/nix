#include "json.hh"

#include <iomanip>
#include <cstring>

namespace nix {

void toJSON(std::ostream & str, const char * start, const char * end)
{
    str << '"';
    for (auto i = start; i != end; i++)
        if (*i == '\"' || *i == '\\') str << '\\' << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else if (*i >= 0 && *i < 32)
            str << "\\u" << std::setfill('0') << std::setw(4) << std::hex << (uint16_t) *i << std::dec;
        else str << *i;
    str << '"';
}

void toJSON(std::ostream & str, const std::string & s)
{
    toJSON(str, s.c_str(), s.c_str() + s.size());
}

void toJSON(std::ostream & str, const char * s)
{
    if (!s) str << "null"; else toJSON(str, s, s + strlen(s));
}

void toJSON(std::ostream & str, unsigned long long n)
{
    str << n;
}

void toJSON(std::ostream & str, unsigned long n)
{
    str << n;
}

void toJSON(std::ostream & str, long n)
{
    str << n;
}

void toJSON(std::ostream & str, unsigned int n)
{
    str << n;
}

void toJSON(std::ostream & str, int n)
{
    str << n;
}

void toJSON(std::ostream & str, double f)
{
    str << f;
}

void toJSON(std::ostream & str, bool b)
{
    str << (b ? "true" : "false");
}

JSONWriter::JSONWriter(std::ostream & str, bool indent)
    : state(new JSONState(str, indent))
{
    state->stack.push_back(this);
}

JSONWriter::JSONWriter(JSONState * state)
    : state(state)
{
    state->stack.push_back(this);
}

JSONWriter::~JSONWriter()
{
    assertActive();
    state->stack.pop_back();
    if (state->stack.empty()) delete state;
}

void JSONWriter::comma()
{
    assertActive();
    if (first) {
        first = false;
    } else {
        state->str << ',';
    }
    if (state->indent) indent();
}

void JSONWriter::indent()
{
    state->str << '\n' << std::string(state->depth * 2, ' ');
}

void JSONList::open()
{
    state->depth++;
    state->str << '[';
}

JSONList::~JSONList()
{
    state->depth--;
    if (state->indent && !first) indent();
    state->str << "]";
}

JSONList JSONList::list()
{
    comma();
    return JSONList(state);
}

JSONObject JSONList::object()
{
    comma();
    return JSONObject(state);
}

JSONPlaceholder JSONList::placeholder()
{
    comma();
    return JSONPlaceholder(state);
}

void JSONObject::open()
{
    state->depth++;
    state->str << '{';
}

JSONObject::~JSONObject()
{
    state->depth--;
    if (state->indent && !first) indent();
    state->str << "}";
}

void JSONObject::attr(const std::string & s)
{
    comma();
    toJSON(state->str, s);
    state->str << ':';
    if (state->indent) state->str << ' ';
}

JSONList JSONObject::list(const std::string & name)
{
    attr(name);
    return JSONList(state);
}

JSONObject JSONObject::object(const std::string & name)
{
    attr(name);
    return JSONObject(state);
}

JSONPlaceholder JSONObject::placeholder(const std::string & name)
{
    attr(name);
    return JSONPlaceholder(state);
}

JSONList JSONPlaceholder::list()
{
    assertValid();
    first = false;
    return JSONList(state);
}

JSONObject JSONPlaceholder::object()
{
    assertValid();
    first = false;
    return JSONObject(state);
}

}
