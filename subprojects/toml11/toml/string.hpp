//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_STRING_HPP
#define TOML11_STRING_HPP

#include "version.hpp"

#include <cstdint>

#include <algorithm>
#include <string>

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
#if __has_include(<string_view>)
#define TOML11_USING_STRING_VIEW 1
#include <string_view>
#endif
#endif

namespace toml
{

enum class string_t : std::uint8_t
{
    basic   = 0,
    literal = 1,
};

struct string
{
    string()  = default;
    ~string() = default;
    string(const string& s) = default;
    string(string&& s)      = default;
    string& operator=(const string& s) = default;
    string& operator=(string&& s)      = default;

    string(const std::string& s): kind(string_t::basic), str(s){}
    string(const std::string& s, string_t k):   kind(k), str(s){}
    string(const char* s):        kind(string_t::basic), str(s){}
    string(const char* s,        string_t k):   kind(k), str(s){}

    string(std::string&& s): kind(string_t::basic), str(std::move(s)){}
    string(std::string&& s, string_t k):   kind(k), str(std::move(s)){}

    string& operator=(const std::string& s)
    {kind = string_t::basic; str = s; return *this;}
    string& operator=(std::string&& s)
    {kind = string_t::basic; str = std::move(s); return *this;}

    operator std::string&       () &      noexcept {return str;}
    operator std::string const& () const& noexcept {return str;}
    operator std::string&&      () &&     noexcept {return std::move(str);}

    string& operator+=(const char*        rhs) {str += rhs; return *this;}
    string& operator+=(const char         rhs) {str += rhs; return *this;}
    string& operator+=(const std::string& rhs) {str += rhs; return *this;}
    string& operator+=(const string&      rhs) {str += rhs.str; return *this;}

#if defined(TOML11_USING_STRING_VIEW) && TOML11_USING_STRING_VIEW>0
    explicit string(std::string_view s): kind(string_t::basic), str(s){}
    string(std::string_view s, string_t k): kind(k), str(s){}

    string& operator=(std::string_view s)
    {kind = string_t::basic; str = s; return *this;}

    explicit operator std::string_view() const noexcept
    {return std::string_view(str);}

    string& operator+=(const std::string_view& rhs) {str += rhs; return *this;}
#endif

    string_t    kind;
    std::string str;
};

inline bool operator==(const string& lhs, const string& rhs)
{
    return lhs.kind == rhs.kind && lhs.str == rhs.str;
}
inline bool operator!=(const string& lhs, const string& rhs)
{
    return !(lhs == rhs);
}
inline bool operator<(const string& lhs, const string& rhs)
{
    return (lhs.kind == rhs.kind) ? (lhs.str < rhs.str) : (lhs.kind < rhs.kind);
}
inline bool operator>(const string& lhs, const string& rhs)
{
    return rhs < lhs;
}
inline bool operator<=(const string& lhs, const string& rhs)
{
    return !(rhs < lhs);
}
inline bool operator>=(const string& lhs, const string& rhs)
{
    return !(lhs < rhs);
}

inline bool
operator==(const string& lhs, const std::string& rhs) {return lhs.str == rhs;}
inline bool
operator!=(const string& lhs, const std::string& rhs) {return lhs.str != rhs;}
inline bool
operator< (const string& lhs, const std::string& rhs) {return lhs.str <  rhs;}
inline bool
operator> (const string& lhs, const std::string& rhs) {return lhs.str >  rhs;}
inline bool
operator<=(const string& lhs, const std::string& rhs) {return lhs.str <= rhs;}
inline bool
operator>=(const string& lhs, const std::string& rhs) {return lhs.str >= rhs;}

inline bool
operator==(const std::string& lhs, const string& rhs) {return lhs == rhs.str;}
inline bool
operator!=(const std::string& lhs, const string& rhs) {return lhs != rhs.str;}
inline bool
operator< (const std::string& lhs, const string& rhs) {return lhs <  rhs.str;}
inline bool
operator> (const std::string& lhs, const string& rhs) {return lhs >  rhs.str;}
inline bool
operator<=(const std::string& lhs, const string& rhs) {return lhs <= rhs.str;}
inline bool
operator>=(const std::string& lhs, const string& rhs) {return lhs >= rhs.str;}

inline bool
operator==(const string& lhs, const char* rhs) {return lhs.str == std::string(rhs);}
inline bool
operator!=(const string& lhs, const char* rhs) {return lhs.str != std::string(rhs);}
inline bool
operator< (const string& lhs, const char* rhs) {return lhs.str <  std::string(rhs);}
inline bool
operator> (const string& lhs, const char* rhs) {return lhs.str >  std::string(rhs);}
inline bool
operator<=(const string& lhs, const char* rhs) {return lhs.str <= std::string(rhs);}
inline bool
operator>=(const string& lhs, const char* rhs) {return lhs.str >= std::string(rhs);}

inline bool
operator==(const char* lhs, const string& rhs) {return std::string(lhs) == rhs.str;}
inline bool
operator!=(const char* lhs, const string& rhs) {return std::string(lhs) != rhs.str;}
inline bool
operator< (const char* lhs, const string& rhs) {return std::string(lhs) <  rhs.str;}
inline bool
operator> (const char* lhs, const string& rhs) {return std::string(lhs) >  rhs.str;}
inline bool
operator<=(const char* lhs, const string& rhs) {return std::string(lhs) <= rhs.str;}
inline bool
operator>=(const char* lhs, const string& rhs) {return std::string(lhs) >= rhs.str;}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const string& s)
{
    if(s.kind == string_t::basic)
    {
        if(std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend())
        {
            // it contains newline. make it multiline string.
            os << "\"\"\"\n";
            for(auto i=s.str.cbegin(), e=s.str.cend(); i!=e; ++i)
            {
                switch(*i)
                {
                    case '\\': {os << "\\\\"; break;}
                    case '\"': {os << "\\\""; break;}
                    case '\b': {os << "\\b";  break;}
                    case '\t': {os << "\\t";  break;}
                    case '\f': {os << "\\f";  break;}
                    case '\n': {os << '\n';   break;}
                    case '\r':
                    {
                        // since it is a multiline string,
                        // CRLF is not needed to be escaped.
                        if(std::next(i) != e && *std::next(i) == '\n')
                        {
                            os << "\r\n";
                            ++i;
                        }
                        else
                        {
                            os << "\\r";
                        }
                        break;
                    }
                    default: {os << *i; break;}
                }
            }
            os << "\\\n\"\"\"";
            return os;
        }
        // no newline. make it inline.
        os << "\"";
        for(const auto c : s.str)
        {
            switch(c)
            {
                case '\\': {os << "\\\\"; break;}
                case '\"': {os << "\\\""; break;}
                case '\b': {os << "\\b";  break;}
                case '\t': {os << "\\t";  break;}
                case '\f': {os << "\\f";  break;}
                case '\n': {os << "\\n";  break;}
                case '\r': {os << "\\r";  break;}
                default  : {os << c;      break;}
            }
        }
        os << "\"";
        return os;
    }
    // the string `s` is literal-string.
    if(std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend() ||
       std::find(s.str.cbegin(), s.str.cend(), '\'') != s.str.cend() )
    {
        // contains newline or single quote. make it multiline.
        os << "'''\n" << s.str << "'''";
        return os;
    }
    // normal literal string
    os << '\'' << s.str << '\'';
    return os;
}

} // toml
#endif// TOML11_STRING_H
