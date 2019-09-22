#include "json-to-value.hh"

#include <cstring>

namespace nix {


static void skipWhitespace(const char * & s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
}


static string parseJSONString(const char * & s)
{
    string res;
    if (*s++ != '"') throw JSONParseError("expected JSON string");
    while (*s != '"') {
        if (!*s) throw JSONParseError("got end-of-string in JSON string");
        if (*s == '\\') {
            s++;
            if (*s == '"') res += '"';
            else if (*s == '\\') res += '\\';
            else if (*s == '/') res += '/';
            else if (*s == '/') res += '/';
            else if (*s == 'b') res += '\b';
            else if (*s == 'f') res += '\f';
            else if (*s == 'n') res += '\n';
            else if (*s == 'r') res += '\r';
            else if (*s == 't') res += '\t';
            else if (*s == 'u') throw JSONParseError("\\u characters in JSON strings are currently not supported");
            else throw JSONParseError("invalid escaped character in JSON string");
            s++;
        } else
            res += *s++;
    }
    s++;
    return res;
}


static void parseJSON(EvalState & state, const char * & s, Value & v)
{
    skipWhitespace(s);

    if (!*s) throw JSONParseError("expected JSON value");

    if (*s == '[') {
        s++;
        ValueVector values;
        values.reserve(128);
        skipWhitespace(s);
        while (1) {
            if (values.empty() && *s == ']') break;
            Value * v2 = state.allocValue();
            parseJSON(state, s, *v2);
            values.push_back(v2);
            skipWhitespace(s);
            if (*s == ']') break;
            if (*s != ',') throw JSONParseError("expected ',' or ']' after JSON array element");
            s++;
        }
        s++;
        state.mkList(v, values.size());
        for (size_t n = 0; n < values.size(); ++n)
            v.listElems()[n] = values[n];
    }

    else if (*s == '{') {
        s++;
        ValueMap attrs;
        while (1) {
            skipWhitespace(s);
            if (attrs.empty() && *s == '}') break;
            string name = parseJSONString(s);
            skipWhitespace(s);
            if (*s != ':') throw JSONParseError("expected ':' in JSON object");
            s++;
            Value * v2 = state.allocValue();
            parseJSON(state, s, *v2);
            attrs[state.symbols.create(name)] = v2;
            skipWhitespace(s);
            if (*s == '}') break;
            if (*s != ',') throw JSONParseError("expected ',' or '}' after JSON member");
            s++;
        }
        state.mkAttrs(v, attrs.size());
        for (auto & i : attrs)
            v.attrs->push_back(Attr(i.first, i.second));
        v.attrs->sort();
        s++;
    }

    else if (*s == '"') {
        mkString(v, parseJSONString(s));
    }

    else if (isdigit(*s) || *s == '-' || *s == '.' ) {
        // Buffer into a string first, then use built-in C++ conversions
        std::string tmp_number;
        ValueType number_type = tInt;

        while (isdigit(*s) || *s == '-' || *s == '.' || *s == 'e' || *s == 'E') {
            if (*s == '.' || *s == 'e' || *s == 'E')
                number_type = tFloat;
            tmp_number += *s++;
        }

        try {
            if (number_type == tFloat)
                mkFloat(v, stod(tmp_number));
            else
                mkInt(v, stol(tmp_number));
        } catch (std::invalid_argument & e) {
            throw JSONParseError("invalid JSON number");
        } catch (std::out_of_range & e) {
            throw JSONParseError("out-of-range JSON number");
        }
    }

    else if (strncmp(s, "true", 4) == 0) {
        s += 4;
        mkBool(v, true);
    }

    else if (strncmp(s, "false", 5) == 0) {
        s += 5;
        mkBool(v, false);
    }

    else if (strncmp(s, "null", 4) == 0) {
        s += 4;
        mkNull(v);
    }

    else throw JSONParseError("unrecognised JSON value");
}


void parseJSON(EvalState & state, const string & s_, Value & v)
{
    const char * s = s_.c_str();
    parseJSON(state, s, v);
    skipWhitespace(s);
    if (*s) throw JSONParseError(format("expected end-of-string while parsing JSON value: %1%") % s);
}


}
