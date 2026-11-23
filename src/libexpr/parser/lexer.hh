#pragma once

#ifndef BISON_HEADER
#  include "nix/util/util.hh"
NIX_INCOMPLETE_SWITCH_BEGIN
#  include "parser-tab.hh"
NIX_INCOMPLETE_SWITCH_END
#endif

#include <vector>

namespace nix::lexer {

/**
 * @brief Tokeniser for the nix language.
 *
 * Consumed by BisonParser.
 */
class Lexer final
{
    /**
     * Various state we use when tokenising.
     * @todo Ideally this should just be folded into this class directly.
     */
    LexerState & state;

    /**
     * Where in the input we are currently.
     */
    char * cur;

    /**
     * For YYLIMIT and bounds checking against matches with '\0' bytes.
     * Just like with flex, the NUL sentinel is the EOF indicator although
     * we don't need 2 trailing NULs Just one suffices and YYLIMIT serves
     * as the real EOF indicator.
     */
    const char * const limit;

    enum class Cond {
        DEFAULT,
        STRING,     /**< Inside ""-string literals. */
        IND_STRING, /**< Inside ''''-string literals. */
        INPATH,
        INPATH_SLASH, /**< Path interpolation. */
        PATH_START,
        REPL_BINDINGS_MODE,
    };

    /**
     * State stack for handling context-dependent scanning.
     * Used for handling string and path interpolation.
     */
    std::vector<Cond> condStack;

    Cond currentCond = Cond::DEFAULT;

    void pushState(Cond cond)
    {
        condStack.push_back(cond);
        currentCond = cond;
    }

    void popState()
    {
        assert(!condStack.empty());
        condStack.pop_back();
        currentCond = condStack.back();
    }

public:
    /**
     * @param state Lexer bookkeeping state.
     * @param cur Pointer to a NUL-terminated string. The lexer will mutate it.
     * @param size Length of the string, not including the NUL terminator.
     */
    Lexer(LexerState & state, char * cur, std::size_t size)
        : state(state)
        , cur(cur)
        , limit(cur + size)
    {
        pushState(Cond::DEFAULT);
    }

    LexerState & getState() const
    {
        return state;
    }

    /**
     * Puts the lexer in REPL bindings mode before the first token. This causes
     * the parser to accept REPL bindings (attribute definitions).
     */
    void setReplBindingsMode()
    {
        pushState(Cond::REPL_BINDINGS_MODE);
    }

    friend parser::BisonParser::token_kind_type yylex(
        parser::BisonParser::value_type * yylval,    /* Semantic value. */
        parser::BisonParser::location_type * yylloc, /* Location of the token. */
        Lexer * lexer,                               /* Reference to us, passed by bison. */
        ParserState * state                          /* TODO: Is this actually needed? */
    );
};

} // namespace nix::lexer
