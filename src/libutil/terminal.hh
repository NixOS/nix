#pragma once
///@file

#include <limits>
#include <string>

namespace nix {

enum class StandardOutputStream {
    Stdout = 1,
    Stderr = 2,
};

/**
 * Determine whether the output is a real terminal (i.e. not dumb, not a pipe).
 *
 * This is probably not what you want, you may want shouldANSI() or something
 * more specific. Think about how the output should work with a pager or
 * entirely non-interactive scripting use.
 *
 * The user may be redirecting the Lix output to a pager, but have stderr
 * connected to a terminal. Think about where you are outputting the text when
 * deciding whether to use STDERR_FILENO or STDOUT_FILENO.
 *
 * \param fileno file descriptor number to check if it is a tty
 */
bool isOutputARealTerminal(StandardOutputStream fileno);

/**
 * Determine whether ANSI escape sequences are appropriate for the
 * present output.
 *
 * This follows the rules described on https://bixense.com/clicolors/
 * with CLICOLOR defaulted to enabled (and thus ignored).
 *
 * That is to say, the following procedure is followed in order:
 * - NO_COLOR or NOCOLOR set             -> always disable colour
 * - CLICOLOR_FORCE or FORCE_COLOR set   -> enable colour
 * - The output is a tty; TERM != "dumb" -> enable colour
 * - Otherwise                           -> disable colour
 *
 * \param fileno which file descriptor number to consider. Use the one you are outputting to
 */
bool shouldANSI(StandardOutputStream fileno = StandardOutputStream::Stderr);

/**
 * Truncate a string to 'width' printable characters. If 'filterAll'
 * is true, all ANSI escape sequences are filtered out. Otherwise,
 * some escape sequences (such as colour setting) are copied but not
 * included in the character count. Also, tabs are expanded to
 * spaces.
 */
std::string filterANSIEscapes(std::string_view s,
    bool filterAll = false,
    unsigned int width = std::numeric_limits<unsigned int>::max());

/**
 * Recalculate the window size, updating a global variable.
 *
 * Used in the `SIGWINCH` signal handler on Unix, for example.
 */
void updateWindowSize();

/**
 * @return the number of rows and columns of the terminal.
 *
 * The value is cached so this is quick. The cached result is computed
 * by `updateWindowSize()`.
 */
std::pair<unsigned short, unsigned short> getWindowSize();

}
