#pragma once
/// @file

#include <functional>

namespace nix {

/** DFA that eats terminal escapes
 *
 * See: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 */
class TerminalCodeEater
{
public:
    void feed(char c, std::function<void(char)> on_char);

private:
    enum class State {
        ExpectESC,
        ExpectESCSeq,
        InCSIParams,
        InCSIIntermediates,
    };

    State state = State::ExpectESC;

    void transition(State new_state);
};
};
