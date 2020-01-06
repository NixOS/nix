#include "json-to-value.hh"

#include <cstring>

namespace nix {


static void skipWhitespace(const char * & s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
}


/*
  Parse an unicode escape sequence (4 hex characters following \u) in JSON string
*/
static string parseUnicodeEscapeSequence(const char * & s)
{
    int codepoint = 0;

    const auto factors = { 12u, 8u, 4u, 0u };
    for (const auto factor : factors)
    {
        if (!*s) throw JSONParseError("got end-of-string in JSON string while parsing \\u sequence");

        if (*s >= '0' and *s <= '9') {
            codepoint += static_cast<int>((static_cast<unsigned int>(*s) - 0x30u) << factor);
        } else if (*s >= 'A' and *s <= 'F') {
            codepoint += static_cast<int>((static_cast<unsigned int>(*s) - 0x37u) << factor);
        } else if (*s >= 'a' and *s <= 'f') {
            codepoint += static_cast<int>((static_cast<unsigned int>(*s) - 0x57u) << factor);
        } else {
            throw JSONParseError(format("illegal character '%1%' in \\u escape sequence.") % *s);
        }
        s++;
    }

    if ((codepoint > 0xd7ff && codepoint < 0xe000) || codepoint > 0x10ffff) {
        throw JSONParseError("Unicode escape sequence is not a Unicode scalar value");
    }

    // taken from cpptoml.h
    std::string result;
    // See Table 3-6 of the Unicode standard
    if (codepoint <= 0x7f)
    {
        // 1-byte codepoints: 00000000 0xxxxxxx
        // repr: 0xxxxxxx
        result += static_cast<char>(codepoint & 0x7f);
    }
    else if (codepoint <= 0x7ff)
    {
        // 2-byte codepoints: 00000yyy yyxxxxxx
        // repr: 110yyyyy 10xxxxxx
        //
        // 0x1f = 00011111
        // 0xc0 = 11000000
        //
        result += static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f));
        //
        // 0x80 = 10000000
        // 0x3f = 00111111
        //
        result += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint <= 0xffff)
    {
        // 3-byte codepoints: zzzzyyyy yyxxxxxx
        // repr: 1110zzzz 10yyyyyy 10xxxxxx
        //
        // 0xe0 = 11100000
        // 0x0f = 00001111
        //
        result += static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x1f));
        result += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else
    {
        // 4-byte codepoints: 000uuuuu zzzzyyyy yyxxxxxx
        // repr: 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx
        //
        // 0xf0 = 11110000
        // 0x07 = 00000111
        //
        result += static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07));
        result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    return result;
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
            else if (*s == 'b') res += '\b';
            else if (*s == 'f') res += '\f';
            else if (*s == 'n') res += '\n';
            else if (*s == 'r') res += '\r';
            else if (*s == 't') res += '\t';
            else if (*s == 'u') {
                res += parseUnicodeEscapeSequence(++s);
                // to neuter the outside s++
                s--;
            } else throw JSONParseError("invalid escaped character in JSON string");
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
