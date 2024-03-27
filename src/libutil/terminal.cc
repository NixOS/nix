#include "terminal.hh"
#include "environment-variables.hh"
#include "sync.hh"

#include <sys/ioctl.h>
#include <unistd.h>

namespace nix {

bool isTTY()
{
    static const bool tty =
        isatty(STDERR_FILENO)
        && getEnv("TERM").value_or("dumb") != "dumb"
        && !(getEnv("NO_COLOR").has_value() || getEnv("NOCOLOR").has_value());

    return tty;
}

std::string filterANSIEscapes(std::string_view s, bool filterAll, unsigned int width)
{
    std::string t, e;
    size_t w = 0;
    auto i = s.begin();

    while (w < (size_t) width && i != s.end()) {

        if (*i == '\e') {
            std::string e;
            e += *i++;
            char last = 0;

            if (i != s.end() && *i == '[') {
                e += *i++;
                // eat parameter bytes
                while (i != s.end() && *i >= 0x30 && *i <= 0x3f) e += *i++;
                // eat intermediate bytes
                while (i != s.end() && *i >= 0x20 && *i <= 0x2f) e += *i++;
                // eat final byte
                if (i != s.end() && *i >= 0x40 && *i <= 0x7e) e += last = *i++;
            } else {
                if (i != s.end() && *i >= 0x40 && *i <= 0x5f) e += *i++;
            }

            if (!filterAll && last == 'm')
                t += e;
        }

        else if (*i == '\t') {
            i++; t += ' '; w++;
            while (w < (size_t) width && w % 8) {
                t += ' '; w++;
            }
        }

        else if (*i == '\r' || *i == '\a')
            // do nothing for now
            i++;

        else {
            w++;
            // Copy one UTF-8 character.
            if ((*i & 0xe0) == 0xc0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
            } else if ((*i & 0xf0) == 0xe0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                }
            } else if ((*i & 0xf8) == 0xf0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                        t += *i++;
                        if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                    }
                }
            } else
                t += *i++;
        }
    }

    return t;
}


//////////////////////////////////////////////////////////////////////

static Sync<std::pair<unsigned short, unsigned short>> windowSize{{0, 0}};


void updateWindowSize()
{
    struct winsize ws;
    if (ioctl(2, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize.lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
}


std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize.lock();
}

}
