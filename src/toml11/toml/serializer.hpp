//     Copyright Toru Niina 2019.
// Distributed under the MIT License.
#ifndef TOML11_SERIALIZER_HPP
#define TOML11_SERIALIZER_HPP
#include <cmath>
#include <cstdio>

#include <limits>

#include "lexer.hpp"
#include "value.hpp"

namespace toml
{

// This function serialize a key. It checks a string is a bare key and
// escapes special characters if the string is not compatible to a bare key.
// ```cpp
// std::string k("non.bare.key"); // the key itself includes `.`s.
// std::string formatted = toml::format_key(k);
// assert(formatted == "\"non.bare.key\"");
// ```
//
// This function is exposed to make it easy to write a user-defined serializer.
// Since toml restricts characters available in a bare key, generally a string
// should be escaped. But checking whether a string needs to be surrounded by
// a `"` and escaping some special character is boring.
template<typename charT, typename traits, typename Alloc>
std::basic_string<charT, traits, Alloc>
format_key(const std::basic_string<charT, traits, Alloc>& k)
{
    if(k.empty())
    {
        return std::string("\"\"");
    }

    // check the key can be a bare (unquoted) key
    detail::location loc(k, std::vector<char>(k.begin(), k.end()));
    detail::lex_unquoted_key::invoke(loc);
    if(loc.iter() == loc.end())
    {
        return k; // all the tokens are consumed. the key is unquoted-key.
    }

    //if it includes special characters, then format it in a "quoted" key.
    std::basic_string<charT, traits, Alloc> serialized("\"");
    for(const char c : k)
    {
        switch(c)
        {
            case '\\': {serialized += "\\\\"; break;}
            case '\"': {serialized += "\\\""; break;}
            case '\b': {serialized += "\\b";  break;}
            case '\t': {serialized += "\\t";  break;}
            case '\f': {serialized += "\\f";  break;}
            case '\n': {serialized += "\\n";  break;}
            case '\r': {serialized += "\\r";  break;}
            default  : {serialized += c;      break;}
        }
    }
    serialized += "\"";
    return serialized;
}

template<typename charT, typename traits, typename Alloc>
std::basic_string<charT, traits, Alloc>
format_keys(const std::vector<std::basic_string<charT, traits, Alloc>>& keys)
{
    if(keys.empty())
    {
        return std::string("\"\"");
    }

    std::basic_string<charT, traits, Alloc> serialized;
    for(const auto& ky : keys)
    {
        serialized += format_key(ky);
        serialized += charT('.');
    }
    serialized.pop_back(); // remove the last dot '.'
    return serialized;
}

template<typename Value>
struct serializer
{
    static_assert(detail::is_basic_value<Value>::value,
                  "toml::serializer is for toml::value and its variants, "
                  "toml::basic_value<...>.");

    using value_type           = Value;
    using key_type             = typename value_type::key_type            ;
    using comment_type         = typename value_type::comment_type        ;
    using boolean_type         = typename value_type::boolean_type        ;
    using integer_type         = typename value_type::integer_type        ;
    using floating_type        = typename value_type::floating_type       ;
    using string_type          = typename value_type::string_type         ;
    using local_time_type      = typename value_type::local_time_type     ;
    using local_date_type      = typename value_type::local_date_type     ;
    using local_datetime_type  = typename value_type::local_datetime_type ;
    using offset_datetime_type = typename value_type::offset_datetime_type;
    using array_type           = typename value_type::array_type          ;
    using table_type           = typename value_type::table_type          ;

    serializer(const std::size_t w              = 80u,
               const int         float_prec     = std::numeric_limits<toml::floating>::max_digits10,
               const bool        can_be_inlined = false,
               const bool        no_comment     = false,
               std::vector<toml::key> ks        = {},
               const bool     value_has_comment = false)
        : can_be_inlined_(can_be_inlined), no_comment_(no_comment),
          value_has_comment_(value_has_comment && !no_comment),
          float_prec_(float_prec), width_(w), keys_(std::move(ks))
    {}
    ~serializer() = default;

    std::string operator()(const boolean_type& b) const
    {
        return b ? "true" : "false";
    }
    std::string operator()(const integer_type i) const
    {
        return std::to_string(i);
    }
    std::string operator()(const floating_type f) const
    {
        if(std::isnan(f))
        {
            if(std::signbit(f))
            {
                return std::string("-nan");
            }
            else
            {
                return std::string("nan");
            }
        }
        else if(!std::isfinite(f))
        {
            if(std::signbit(f))
            {
                return std::string("-inf");
            }
            else
            {
                return std::string("inf");
            }
        }

        const auto fmt = "%.*g";
        const auto bsz = std::snprintf(nullptr, 0, fmt, this->float_prec_, f);
        // +1 for null character(\0)
        std::vector<char> buf(static_cast<std::size_t>(bsz + 1), '\0');
        std::snprintf(buf.data(), buf.size(), fmt, this->float_prec_, f);

        std::string token(buf.begin(), std::prev(buf.end()));
        if(!token.empty() && token.back() == '.') // 1. => 1.0
        {
            token += '0';
        }

        const auto e = std::find_if(
            token.cbegin(), token.cend(), [](const char c) noexcept -> bool {
                return c == 'e' || c == 'E';
            });
        const auto has_exponent = (token.cend() != e);
        const auto has_fraction = (token.cend() != std::find(
            token.cbegin(), token.cend(), '.'));

        if(!has_exponent && !has_fraction)
        {
            // the resulting value does not have any float specific part!
            token += ".0";
        }
        return token;
    }
    std::string operator()(const string_type& s) const
    {
        if(s.kind == string_t::basic)
        {
            if((std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend() ||
                std::find(s.str.cbegin(), s.str.cend(), '\"') != s.str.cend()) &&
               this->width_ != (std::numeric_limits<std::size_t>::max)())
            {
                // if linefeed or double-quote is contained,
                // make it multiline basic string.
                const auto escaped = this->escape_ml_basic_string(s.str);
                std::string open("\"\"\"");
                std::string close("\"\"\"");
                if(escaped.find('\n') != std::string::npos ||
                   this->width_ < escaped.size() + 6)
                {
                    // if the string body contains newline or is enough long,
                    // add newlines after and before delimiters.
                    open += "\n";
                    close = std::string("\\\n") + close;
                }
                return open + escaped + close;
            }

            // no linefeed. try to make it oneline-string.
            std::string oneline = this->escape_basic_string(s.str);
            if(oneline.size() + 2 < width_ || width_ < 2)
            {
                const std::string quote("\"");
                return quote + oneline + quote;
            }

            // the line is too long compared to the specified width.
            // split it into multiple lines.
            std::string token("\"\"\"\n");
            while(!oneline.empty())
            {
                if(oneline.size() < width_)
                {
                    token += oneline;
                    oneline.clear();
                }
                else if(oneline.at(width_-2) == '\\')
                {
                    token += oneline.substr(0, width_-2);
                    token += "\\\n";
                    oneline.erase(0, width_-2);
                }
                else
                {
                    token += oneline.substr(0, width_-1);
                    token += "\\\n";
                    oneline.erase(0, width_-1);
                }
            }
            return token + std::string("\\\n\"\"\"");
        }
        else // the string `s` is literal-string.
        {
            if(std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend() ||
               std::find(s.str.cbegin(), s.str.cend(), '\'') != s.str.cend() )
            {
                std::string open("'''");
                if(this->width_ + 6 < s.str.size())
                {
                    open += '\n'; // the first newline is ignored by TOML spec
                }
                const std::string close("'''");
                return open + s.str + close;
            }
            else
            {
                const std::string quote("'");
                return quote + s.str + quote;
            }
        }
    }

    std::string operator()(const local_date_type& d) const
    {
        std::ostringstream oss;
        oss << d;
        return oss.str();
    }
    std::string operator()(const local_time_type& t) const
    {
        std::ostringstream oss;
        oss << t;
        return oss.str();
    }
    std::string operator()(const local_datetime_type& dt) const
    {
        std::ostringstream oss;
        oss << dt;
        return oss.str();
    }
    std::string operator()(const offset_datetime_type& odt) const
    {
        std::ostringstream oss;
        oss << odt;
        return oss.str();
    }

    std::string operator()(const array_type& v) const
    {
        if(v.empty())
        {
            return std::string("[]");
        }
        if(this->is_array_of_tables(v))
        {
            return make_array_of_tables(v);
        }

        // not an array of tables. normal array.
        // first, try to make it inline if none of the elements have a comment.
        if( ! this->has_comment_inside(v))
        {
            const auto inl = this->make_inline_array(v);
            if(inl.size() < this->width_ &&
               std::find(inl.cbegin(), inl.cend(), '\n') == inl.cend())
            {
                return inl;
            }
        }

        // if the length exceeds this->width_, print multiline array.
        // key = [
        //   # ...
        //   42,
        //   ...
        // ]
        std::string token;
        std::string current_line;
        token += "[\n";
        for(const auto& item : v)
        {
            if( ! item.comments().empty() && !no_comment_)
            {
                // if comment exists, the element must be the only element in the line.
                // e.g. the following is not allowed.
                // ```toml
                // array = [
                // # comment for what?
                // 1, 2, 3, 4, 5
                // ]
                // ```
                if(!current_line.empty())
                {
                    if(current_line.back() != '\n')
                    {
                        current_line += '\n';
                    }
                    token += current_line;
                    current_line.clear();
                }
                for(const auto& c : item.comments())
                {
                    token += '#';
                    token += c;
                    token += '\n';
                }
                token += toml::visit(*this, item);
                if(!token.empty() && token.back() == '\n') {token.pop_back();}
                token += ",\n";
                continue;
            }
            std::string next_elem;
            if(item.is_table())
            {
                serializer ser(*this);
                ser.can_be_inlined_ = true;
                ser.width_ = (std::numeric_limits<std::size_t>::max)();
                next_elem += toml::visit(ser, item);
            }
            else
            {
                next_elem += toml::visit(*this, item);
            }

            // comma before newline.
            if(!next_elem.empty() && next_elem.back() == '\n') {next_elem.pop_back();}

            // if current line does not exceeds the width limit, continue.
            if(current_line.size() + next_elem.size() + 1 < this->width_)
            {
                current_line += next_elem;
                current_line += ',';
            }
            else if(current_line.empty())
            {
                // if current line was empty, force put the next_elem because
                // next_elem is not splittable
                token += next_elem;
                token += ",\n";
                // current_line is kept empty
            }
            else // reset current_line
            {
                assert(current_line.back() == ',');
                token += current_line;
                token += '\n';
                current_line = next_elem;
                current_line += ',';
            }
        }
        if(!current_line.empty())
        {
            if(!current_line.empty() && current_line.back() != '\n')
            {
                current_line += '\n';
            }
            token += current_line;
        }
        token += "]\n";
        return token;
    }

    // templatize for any table-like container
    std::string operator()(const table_type& v) const
    {
        // if an element has a comment, then it can't be inlined.
        // table = {# how can we write a comment for this? key = "value"}
        if(this->can_be_inlined_ && !(this->has_comment_inside(v)))
        {
            std::string token;
            if(!this->keys_.empty())
            {
                token += format_key(this->keys_.back());
                token += " = ";
            }
            token += this->make_inline_table(v);
            if(token.size() < this->width_ &&
               token.end() == std::find(token.begin(), token.end(), '\n'))
            {
                return token;
            }
        }

        std::string token;
        if(!keys_.empty())
        {
            token += '[';
            token += format_keys(keys_);
            token += "]\n";
        }
        token += this->make_multiline_table(v);
        return token;
    }

  private:

    std::string escape_basic_string(const std::string& s) const
    {
        //XXX assuming `s` is a valid utf-8 sequence.
        std::string retval;
        for(const char c : s)
        {
            switch(c)
            {
                case '\\': {retval += "\\\\"; break;}
                case '\"': {retval += "\\\""; break;}
                case '\b': {retval += "\\b";  break;}
                case '\t': {retval += "\\t";  break;}
                case '\f': {retval += "\\f";  break;}
                case '\n': {retval += "\\n";  break;}
                case '\r': {retval += "\\r";  break;}
                default  :
                {
                    if((0x00 <= c && c <= 0x08) || (0x0A <= c && c <= 0x1F) || c == 0x7F)
                    {
                        retval += "\\u00";
                        retval += char(48 + (c / 16));
                        retval += char((c % 16 < 10 ? 48 : 55) + (c % 16));
                    }
                    else
                    {
                        retval += c;
                    }
                }
            }
        }
        return retval;
    }

    std::string escape_ml_basic_string(const std::string& s) const
    {
        std::string retval;
        for(auto i=s.cbegin(), e=s.cend(); i!=e; ++i)
        {
            switch(*i)
            {
                case '\\': {retval += "\\\\"; break;}
                // One or two consecutive "s are allowed.
                // Later we will check there are no three consecutive "s.
                //   case '\"': {retval += "\\\""; break;}
                case '\b': {retval += "\\b";  break;}
                case '\t': {retval += "\\t";  break;}
                case '\f': {retval += "\\f";  break;}
                case '\n': {retval += "\n";   break;}
                case '\r':
                {
                    if(std::next(i) != e && *std::next(i) == '\n')
                    {
                        retval += "\r\n";
                        ++i;
                    }
                    else
                    {
                        retval += "\\r";
                    }
                    break;
                }
                default  :
                {
                    const auto c = *i;
                    if((0x00 <= c && c <= 0x08) || (0x0A <= c && c <= 0x1F) || c == 0x7F)
                    {
                        retval += "\\u00";
                        retval += char(48 + (c / 16));
                        retval += char((c % 16 < 10 ? 48 : 55) + (c % 16));
                    }
                    else
                    {
                        retval += c;
                    }
                }

            }
        }
        // Only 1 or 2 consecutive `"`s are allowed in multiline basic string.
        // 3 consecutive `"`s are considered as a closing delimiter.
        // We need to check if there are 3 or more consecutive `"`s and insert
        // backslash to break them down into several short `"`s like the `str6`
        // in the following example.
        // ```toml
        // str4 = """Here are two quotation marks: "". Simple enough."""
        // # str5 = """Here are three quotation marks: """."""  # INVALID
        // str5 = """Here are three quotation marks: ""\"."""
        // str6 = """Here are fifteen quotation marks: ""\"""\"""\"""\"""\"."""
        // ```
        auto found_3_quotes = retval.find("\"\"\"");
        while(found_3_quotes != std::string::npos)
        {
            retval.replace(found_3_quotes, 3, "\"\"\\\"");
            found_3_quotes = retval.find("\"\"\"");
        }
        return retval;
    }

    // if an element of a table or an array has a comment, it cannot be inlined.
    bool has_comment_inside(const array_type& a) const noexcept
    {
        // if no_comment is set, comments would not be written.
        if(this->no_comment_) {return false;}

        for(const auto& v : a)
        {
            if(!v.comments().empty()) {return true;}
        }
        return false;
    }
    bool has_comment_inside(const table_type& t) const noexcept
    {
        // if no_comment is set, comments would not be written.
        if(this->no_comment_) {return false;}

        for(const auto& kv : t)
        {
            if(!kv.second.comments().empty()) {return true;}
        }
        return false;
    }

    std::string make_inline_array(const array_type& v) const
    {
        assert(!has_comment_inside(v));
        std::string token;
        token += '[';
        bool is_first = true;
        for(const auto& item : v)
        {
            if(is_first) {is_first = false;} else {token += ',';}
            token += visit(serializer(
                (std::numeric_limits<std::size_t>::max)(), this->float_prec_,
                /* inlined */ true, /*no comment*/ false, /*keys*/ {},
                /*has_comment*/ !item.comments().empty()), item);
        }
        token += ']';
        return token;
    }

    std::string make_inline_table(const table_type& v) const
    {
        assert(!has_comment_inside(v));
        assert(this->can_be_inlined_);
        std::string token;
        token += '{';
        bool is_first = true;
        for(const auto& kv : v)
        {
            // in inline tables, trailing comma is not allowed (toml-lang #569).
            if(is_first) {is_first = false;} else {token += ',';}
            token += format_key(kv.first);
            token += '=';
            token += visit(serializer(
                (std::numeric_limits<std::size_t>::max)(), this->float_prec_,
                /* inlined */ true, /*no comment*/ false, /*keys*/ {},
                /*has_comment*/ !kv.second.comments().empty()), kv.second);
        }
        token += '}';
        return token;
    }

    std::string make_multiline_table(const table_type& v) const
    {
        std::string token;

        // print non-table elements first.
        // ```toml
        // [foo]         # a table we're writing now here
        // key = "value" # <- non-table element, "key"
        // # ...
        // [foo.bar] # <- table element, "bar"
        // ```
        // because after printing [foo.bar], the remaining non-table values will
        // be assigned into [foo.bar], not [foo]. Those values should be printed
        // earlier.
        for(const auto& kv : v)
        {
            if(kv.second.is_table() || is_array_of_tables(kv.second))
            {
                continue;
            }

            token += write_comments(kv.second);

            const auto key_and_sep    = format_key(kv.first) + " = ";
            const auto residual_width = (this->width_ > key_and_sep.size()) ?
                                        this->width_ - key_and_sep.size() : 0;
            token += key_and_sep;
            token += visit(serializer(residual_width, this->float_prec_,
                /*can be inlined*/ true, /*no comment*/ false, /*keys*/ {},
                /*has_comment*/ !kv.second.comments().empty()), kv.second);

            if(token.back() != '\n')
            {
                token += '\n';
            }
        }

        // normal tables / array of tables

        // after multiline table appeared, the other tables cannot be inline
        // because the table would be assigned into the table.
        // [foo]
        // ...
        // bar = {...} # <- bar will be a member of [foo].
        bool multiline_table_printed = false;
        for(const auto& kv : v)
        {
            if(!kv.second.is_table() && !is_array_of_tables(kv.second))
            {
                continue; // other stuff are already serialized. skip them.
            }

            std::vector<toml::key> ks(this->keys_);
            ks.push_back(kv.first);

            auto tmp = visit(serializer(this->width_, this->float_prec_,
                !multiline_table_printed, this->no_comment_, ks,
                /*has_comment*/ !kv.second.comments().empty()), kv.second);

            // If it is the first time to print a multi-line table, it would be
            // helpful to separate normal key-value pair and subtables by a
            // newline.
            // (this checks if the current key-value pair contains newlines.
            //  but it is not perfect because multi-line string can also contain
            //  a newline. in such a case, an empty line will be written) TODO
            if((!multiline_table_printed) &&
               std::find(tmp.cbegin(), tmp.cend(), '\n') != tmp.cend())
            {
                multiline_table_printed = true;
                token += '\n'; // separate key-value pairs and subtables

                token += write_comments(kv.second);
                token += tmp;

                // care about recursive tables (all tables in each level prints
                // newline and there will be a full of newlines)
                if(tmp.substr(tmp.size() - 2, 2) != "\n\n" &&
                   tmp.substr(tmp.size() - 4, 4) != "\r\n\r\n" )
                {
                    token += '\n';
                }
            }
            else
            {
                token += write_comments(kv.second);
                token += tmp;
                token += '\n';
            }
        }
        return token;
    }

    std::string make_array_of_tables(const array_type& v) const
    {
        // if it's not inlined, we need to add `[[table.key]]`.
        // but if it can be inlined, we can format it as the following.
        // ```
        // table.key = [
        //   {...},
        //   # comment
        //   {...},
        // ]
        // ```
        // This function checks if inlinization is possible or not, and then
        // format the array-of-tables in a proper way.
        //
        // Note about comments:
        //
        // If the array itself has a comment (value_has_comment_ == true), we
        // should try to make it inline.
        // ```toml
        // # comment about array
        // array = [
        //   # comment about table element
        //   {of = "table"}
        // ]
        // ```
        // If it is formatted as a multiline table, the two comments becomes
        // indistinguishable.
        // ```toml
        // # comment about array
        // # comment about table element
        // [[array]]
        // of = "table"
        // ```
        // So we need to try to make it inline, and it force-inlines regardless
        // of the line width limit.
        //     It may fail if the element of a table has comment. In that case,
        // the array-of-tables will be formatted as a multiline table.
        if(this->can_be_inlined_ || this->value_has_comment_)
        {
            std::string token;
            if(!keys_.empty())
            {
                token += format_key(keys_.back());
                token += " = ";
            }

            bool failed = false;
            token += "[\n";
            for(const auto& item : v)
            {
                // if an element of the table has a comment, the table
                // cannot be inlined.
                if(this->has_comment_inside(item.as_table()))
                {
                    failed = true;
                    break;
                }
                // write comments for the table itself
                token += write_comments(item);

                const auto t = this->make_inline_table(item.as_table());

                if(t.size() + 1 > width_ || // +1 for the last comma {...},
                   std::find(t.cbegin(), t.cend(), '\n') != t.cend())
                {
                    // if the value itself has a comment, ignore the line width limit
                    if( ! this->value_has_comment_)
                    {
                        failed = true;
                        break;
                    }
                }
                token += t;
                token += ",\n";
            }

            if( ! failed)
            {
                token += "]\n";
                return token;
            }
            // if failed, serialize them as [[array.of.tables]].
        }

        std::string token;
        for(const auto& item : v)
        {
            token += write_comments(item);
            token += "[[";
            token += format_keys(keys_);
            token += "]]\n";
            token += this->make_multiline_table(item.as_table());
        }
        return token;
    }

    std::string write_comments(const value_type& v) const
    {
        std::string retval;
        if(this->no_comment_) {return retval;}

        for(const auto& c : v.comments())
        {
            retval += '#';
            retval += c;
            retval += '\n';
        }
        return retval;
    }

    bool is_array_of_tables(const value_type& v) const
    {
        if(!v.is_array() || v.as_array().empty()) {return false;}
        return is_array_of_tables(v.as_array());
    }
    bool is_array_of_tables(const array_type& v) const
    {
        // Since TOML v0.5.0, heterogeneous arrays are allowed. So we need to
        // check all the element in an array to check if the array is an array
        // of tables.
        return std::all_of(v.begin(), v.end(), [](const value_type& elem) {
                return elem.is_table();
            });
    }

  private:

    bool        can_be_inlined_;
    bool        no_comment_;
    bool        value_has_comment_;
    int         float_prec_;
    std::size_t width_;
    std::vector<toml::key> keys_;
};

template<typename C,
         template<typename ...> class M, template<typename ...> class V>
std::string
format(const basic_value<C, M, V>& v, std::size_t w = 80u,
       int fprec = std::numeric_limits<toml::floating>::max_digits10,
       bool no_comment = false, bool force_inline = false)
{
    using value_type = basic_value<C, M, V>;
    // if value is a table, it is considered to be a root object.
    // the root object can't be an inline table.
    if(v.is_table())
    {
        std::ostringstream oss;
        if(!v.comments().empty())
        {
            oss << v.comments();
            oss << '\n'; // to split the file comment from the first element
        }
        const auto serialized = visit(serializer<value_type>(w, fprec, false, no_comment), v);
        oss << serialized;
        return oss.str();
    }
    return visit(serializer<value_type>(w, fprec, force_inline), v);
}

namespace detail
{
template<typename charT, typename traits>
int comment_index(std::basic_ostream<charT, traits>&)
{
    static const int index = std::ios_base::xalloc();
    return index;
}
} // detail

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
nocomment(std::basic_ostream<charT, traits>& os)
{
    // by default, it is zero. and by default, it shows comments.
    os.iword(detail::comment_index(os)) = 1;
    return os;
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
showcomment(std::basic_ostream<charT, traits>& os)
{
    // by default, it is zero. and by default, it shows comments.
    os.iword(detail::comment_index(os)) = 0;
    return os;
}

template<typename charT, typename traits, typename C,
         template<typename ...> class M, template<typename ...> class V>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const basic_value<C, M, V>& v)
{
    using value_type = basic_value<C, M, V>;

    // get status of std::setw().
    const auto w     = static_cast<std::size_t>(os.width());
    const int  fprec = static_cast<int>(os.precision());
    os.width(0);

    // by default, iword is initialized by 0. And by default, toml11 outputs
    // comments. So `0` means showcomment. 1 means nocommnet.
    const bool no_comment = (1 == os.iword(detail::comment_index(os)));

    if(!no_comment && v.is_table() && !v.comments().empty())
    {
        os << v.comments();
        os << '\n'; // to split the file comment from the first element
    }
    // the root object can't be an inline table. so pass `false`.
    const auto serialized = visit(serializer<value_type>(w, fprec, no_comment, false), v);
    os << serialized;

    // if v is a non-table value, and has only one comment, then
    // put a comment just after a value. in the following way.
    //
    // ```toml
    // key = "value" # comment.
    // ```
    //
    // Since the top-level toml object is a table, one who want to put a
    // non-table toml value must use this in a following way.
    //
    // ```cpp
    // toml::value v;
    // std::cout << "user-defined-key = " << v << std::endl;
    // ```
    //
    // In this case, it is impossible to put comments before key-value pair.
    // The only way to preserve comments is to put all of them after a value.
    if(!no_comment && !v.is_table() && !v.comments().empty())
    {
        os << " #";
        for(const auto& c : v.comments()) {os << c;}
    }
    return os;
}

} // toml
#endif// TOML11_SERIALIZER_HPP
