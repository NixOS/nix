#include "terminal-code-eater.hh"
#include "debug-char.hh"
#include <assert.h>
#include <cstdint>
#include <iostream>

namespace nix {

static constexpr const bool DEBUG_EATER = false;

void TerminalCodeEater::feed(char c, std::function<void(char)> on_char)
{
    auto isParamChar = [](char v) -> bool { return v >= 0x30 && v <= 0x3f; };
    auto isIntermediateChar = [](char v) -> bool { return v >= 0x20 && v <= 0x2f; };
    auto isFinalChar = [](char v) -> bool { return v >= 0x40 && v <= 0x7e; };
    if constexpr (DEBUG_EATER) {
        std::cerr << "eater" << DebugChar{c} << "\n";
    }

    switch (state) {
    case State::ExpectESC:
        switch (c) {
        case '\e':
            transition(State::ExpectESCSeq);
            return;
        // Just eat \r, since it is part of clearing a line
        case '\r':
            return;
        }
        if constexpr (DEBUG_EATER) {
            std::cerr << "eater uneat" << DebugChar{c} << "\n";
        }
        on_char(c);
        break;
    case State::ExpectESCSeq:
        switch (c) {
        // CSI
        case '[':
            transition(State::InCSIParams);
            return;
        default:
            transition(State::ExpectESC);
            return;
        }
        break;
    // https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
    // A CSI sequence is: CSI [\x30-\x3f]* [\x20-\x2f]* [\x40-\x7e]
    //                        ^ params     ^ intermediates ^ final byte
    case State::InCSIParams:
        if (isFinalChar(c)) {
            transition(State::ExpectESC);
            return;
        } else if (isIntermediateChar(c)) {
            transition(State::InCSIIntermediates);
            return;
        } else if (isParamChar(c)) {
            return;
        } else {
            // Corrupt escape sequence? Throw an assert, for now.
            // transition(State::ExpectESC);
            assert(false && "Corrupt terminal escape sequence");
            return;
        }
        break;
    case State::InCSIIntermediates:
        if (isFinalChar(c)) {
            transition(State::ExpectESC);
            return;
        } else if (isIntermediateChar(c)) {
            return;
        } else {
            // Corrupt escape sequence? Throw an assert, for now.
            // transition(State::ExpectESC);
            assert(false && "Corrupt terminal escape sequence in intermediates");
            return;
        }
        break;
    }
}

void TerminalCodeEater::transition(State new_state)
{
    state = new_state;
}
};
