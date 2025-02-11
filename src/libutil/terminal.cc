#include "terminal.hh"
#include "environment-variables.hh"
#include "sync.hh"

#if _WIN32
# include <io.h>
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define isatty _isatty
#else
# include <sys/ioctl.h>
#endif
#include <unistd.h>
#include <widechar_width.h>

namespace {

inline std::pair<int, size_t> charWidthUTF8Helper(std::string_view s)
{
    size_t bytes = 1;
    uint32_t ch = s[0];
    uint32_t max = 1U << 7;
    if ((ch & 0x80U) == 0U) {
    } else if ((ch & 0xe0U) == 0xc0U) {
        ch &= 0x1fU;
        bytes = 2;
        max = 1U << 11;
    } else if ((ch & 0xf0U) == 0xe0U) {
        ch &= 0x0fU;
        bytes = 3;
        max = 1U << 16;
    } else if ((ch & 0xf8U) == 0xf0U) {
        ch &= 0x07U;
        bytes = 4;
        max = 0x110000U;
    } else {
        return {bytes, bytes}; // invalid UTF-8 start byte
    }
    for (size_t i = 1; i < bytes; i++) {
        if (i < s.size() && (s[i] & 0xc0) == 0x80) {
            ch = (ch << 6) | (s[i] & 0x3f);
        } else {
            return {i, i}; // invalid UTF-8 encoding; assume one character per byte
        }
    }
    int width = bytes; // in case of overlong encoding
    if (ch < max) {
        width = widechar_wcwidth(ch);
        if (width == widechar_ambiguous) {
            width = 1; // just a guess...
        } else if (width == widechar_widened_in_9) {
            width = 2;
        } else if (width < 0) {
            width = 0;
        }
    }
    return {width, bytes};
}

}

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
    std::string t;
    size_t w = 0;
    auto i = s.begin();

    while (i != s.end()) {

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
            } else if (i != s.end() && *i == ']') {
                // OSC
                e += *i++;
                // eat ESC
                while (i != s.end() && *i != '\e') e += *i++;
                // eat backslash
                if (i != s.end() && *i == '\\') e += last = *i++;
            } else {
                if (i != s.end() && *i >= 0x40 && *i <= 0x5f) e += *i++;
            }

            if (!filterAll && last == 'm')
                t += e;
        }

        else if (*i == '\t') {
            do {
                if (++w > (size_t) width)
                    return t;
                t += ' ';
            } while (w % 8);
            i++;
        }

        else if (*i == '\r' || *i == '\a')
            // do nothing for now
            i++;

        else {
            auto [chWidth, bytes] = charWidthUTF8Helper({i, s.end()});
            w += chWidth;
            if (w > (size_t) width) {
                break;
            }
            t += {i, i + bytes};
            i += bytes;
        }
    }
    return t;
}

//////////////////////////////////////////////////////////////////////

static Sync<std::pair<unsigned short, unsigned short>> windowSize{{0, 0}};


void updateWindowSize()
{
    #ifndef _WIN32
    struct winsize ws;
    if (ioctl(2, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize.lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
    #else
    CONSOLE_SCREEN_BUFFER_INFO info;
    // From https://stackoverflow.com/a/12642749
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        auto windowSize_(windowSize.lock());
        // From https://github.com/libuv/libuv/blob/v1.48.0/src/win/tty.c#L1130
        windowSize_->first = info.srWindow.Bottom - info.srWindow.Top + 1;
        windowSize_->second = info.dwSize.X;
    }
    #endif
}


std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize.lock();
}

}
