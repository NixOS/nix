///@file
#include <ostream>
#include <boost/io/ios_state.hpp>

namespace nix {

struct DebugChar
{
    char c;
};

inline std::ostream & operator<<(std::ostream & s, DebugChar c)
{
    boost::io::ios_flags_saver _ifs(s);

    if (isprint(c.c)) {
        s << static_cast<char>(c.c);
    } else {
        s << std::hex << "0x" << (static_cast<unsigned int>(c.c) & 0xff);
    }
    return s;
}

}
