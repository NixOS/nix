/* This file provides a variant of std::istringstream that doesn't
   copy its string argument. This is useful for large strings. The
   caller must ensure that the string object is not destroyed while
   it's referenced by this object. */

#pragma once

#include <string>
#include <iostream>

template <class CharT, class Traits = std::char_traits<CharT>, class Allocator = std::allocator<CharT>>
class basic_istringbuf_nocopy : public std::basic_streambuf<CharT, Traits>
{
public:
    typedef std::basic_string<CharT, Traits, Allocator> string_type;

    typedef typename std::basic_streambuf<CharT, Traits>::off_type off_type;

    typedef typename std::basic_streambuf<CharT, Traits>::pos_type pos_type;

    typedef typename std::basic_streambuf<CharT, Traits>::int_type int_type;

    typedef typename std::basic_streambuf<CharT, Traits>::traits_type traits_type;

private:
    const string_type & s;

    off_type off;

public:
    basic_istringbuf_nocopy(const string_type & s) : s{s}, off{0}
    {
    }

private:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
    {
        if (which & std::ios_base::in) {
            this->off = dir == std::ios_base::beg
                ? off
                : (dir == std::ios_base::end
                    ? s.size() + off
                    : this->off + off);
        }
        return pos_type(this->off);
    }

    pos_type seekpos(pos_type pos, std::ios_base::openmode which)
    {
        return seekoff(pos, std::ios_base::beg, which);
    }

    std::streamsize showmanyc()
    {
        return s.size() - off;
    }

    int_type underflow()
    {
        if (typename string_type::size_type(off) == s.size())
            return traits_type::eof();
        return traits_type::to_int_type(s[off]);
    }

    int_type uflow()
    {
        if (typename string_type::size_type(off) == s.size())
            return traits_type::eof();
        return traits_type::to_int_type(s[off++]);
    }

    int_type pbackfail(int_type ch)
    {
        if (off == 0 || (ch != traits_type::eof() && ch != s[off - 1]))
            return traits_type::eof();

        return traits_type::to_int_type(s[--off]);
    }

};

template <class CharT, class Traits = std::char_traits<CharT>, class Allocator = std::allocator<CharT>>
class basic_istringstream_nocopy : public std::basic_iostream<CharT, Traits>
{
    typedef basic_istringbuf_nocopy<CharT, Traits, Allocator> buf_type;
    buf_type buf;
public:
    basic_istringstream_nocopy(const typename buf_type::string_type & s) :
        std::basic_iostream<CharT, Traits>(&buf), buf(s) {};
};

typedef basic_istringstream_nocopy<char> istringstream_nocopy;
