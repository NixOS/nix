#ifndef TOML11_COLOR_HPP
#define TOML11_COLOR_HPP
#include <cstdint>
#include <ostream>

#ifdef TOML11_COLORIZE_ERROR_MESSAGE
#define TOML11_ERROR_MESSAGE_COLORIZED true
#else
#define TOML11_ERROR_MESSAGE_COLORIZED false
#endif

namespace toml
{

// put ANSI escape sequence to ostream
namespace color_ansi
{
namespace detail
{
inline int colorize_index()
{
    static const int index = std::ios_base::xalloc();
    return index;
}
} // detail

inline std::ostream& colorize(std::ostream& os)
{
    // by default, it is zero.
    os.iword(detail::colorize_index()) = 1;
    return os;
}
inline std::ostream& nocolorize(std::ostream& os)
{
    os.iword(detail::colorize_index()) = 0;
    return os;
}
inline std::ostream& reset  (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[00m";} return os;}
inline std::ostream& bold   (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[01m";} return os;}
inline std::ostream& grey   (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[30m";} return os;}
inline std::ostream& red    (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[31m";} return os;}
inline std::ostream& green  (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[32m";} return os;}
inline std::ostream& yellow (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[33m";} return os;}
inline std::ostream& blue   (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[34m";} return os;}
inline std::ostream& magenta(std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[35m";} return os;}
inline std::ostream& cyan   (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[36m";} return os;}
inline std::ostream& white  (std::ostream& os)
{if(os.iword(detail::colorize_index()) == 1) {os << "\033[37m";} return os;}
} // color_ansi

// ANSI escape sequence is the only and default colorization method currently
namespace color = color_ansi;

} // toml
#endif// TOML11_COLOR_HPP
