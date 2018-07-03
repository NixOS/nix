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

void toJSON(std::ostream & str, const char * s)
{
    if (!s) str << "null"; else toJSON(str, s, s + strlen(s));
}

template<> void toJSON<int>(std::ostream & str, const int & n) { str << n; }
template<> void toJSON<unsigned int>(std::ostream & str, const unsigned int & n) { str << n; }
template<> void toJSON<long>(std::ostream & str, const long & n) { str << n; }
template<> void toJSON<unsigned long>(std::ostream & str, const unsigned long & n) { str << n; }
template<> void toJSON<long long>(std::ostream & str, const long long & n) { str << n; }
template<> void toJSON<unsigned long long>(std::ostream & str, const unsigned long long & n) { str << n; }
template<> void toJSON<float>(std::ostream & str, const float & n) { str << n; }
template<> void toJSON<double>(std::ostream & str, const double & n) { str << n; }

template<> void toJSON<std::string>(std::ostream & str, const std::string & s)
{
    toJSON(str, s.c_str(), s.c_str() + s.size());
}

template<> void toJSON<bool>(std::ostream & str, const bool & b)
{
    str << (b ? "true" : "false");
}

template<> void toJSON<std::nullptr_t>(std::ostream & str, const std::nullptr_t & b)
{
    str << "null";
}

JSONWriter::JSONWriter(std::ostream & str, bool indent)
    : state(new JSONState(str, indent))
{
    state->stack++;
}

JSONWriter::JSONWriter(JSONState * state)
    : state(state)
{
    state->stack++;
}

JSONWriter::~JSONWriter()
{
    if (state) {
        assertActive();
        state->stack--;
        if (state->stack == 0) delete state;
    }
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
    if (state) {
        state->depth--;
        if (state->indent && !first) indent();
        state->str << "}";
    }
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
