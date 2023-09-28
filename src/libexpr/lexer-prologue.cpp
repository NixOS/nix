#ifdef __clang__
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

#include <boost/lexical_cast.hpp>

#include "nixexpr.hh"
#include "parser-tab.hh"

using namespace nix;

namespace nix {

static inline PosIdx makeCurPos(const YYLTYPE & loc, ParseData * data)
{
    return data->state.positions.add(data->origin, loc.first_line, loc.first_column);
}

#define CUR_POS makeCurPos(*yylloc, data)

// backup to recover from yyless(0)
thread_local YYLTYPE prev_yylloc;

static void initLoc(YYLTYPE * loc)
{
    loc->first_line = loc->last_line = 1;
    loc->first_column = loc->last_column = 1;
}

static void adjustLoc(YYLTYPE * loc, const char * s, size_t len)
{
    prev_yylloc = *loc;

    loc->first_line = loc->last_line;
    loc->first_column = loc->last_column;

    for (size_t i = 0; i < len; i++) {
        switch (*s++) {
        case '\r':
            if (*s == '\n') { /* cr/lf */
                i++;
                s++;
            }
            /* fall through */
        case '\n':
            ++loc->last_line;
            loc->last_column = 1;
            break;
        default:
            ++loc->last_column;
        }
    }
}

// we make use of the fact that the parser receives a private copy of the input
// string and can munge around in it.
static StringToken unescapeStr(SymbolTable & symbols, char * s, size_t length)
{
    char * result = s;
    char * t = s;
    char c;
    // the input string is terminated with *two* NULs, so we can safely take
    // *one* character after the one being checked against.
    while ((c = *s++)) {
        if (c == '\\') {
            c = *s++;
            if (c == 'n')
                *t = '\n';
            else if (c == 'r')
                *t = '\r';
            else if (c == 't')
                *t = '\t';
            else
                *t = c;
        } else if (c == '\r') {
            /* Normalise CR and CR/LF into LF. */
            *t = '\n';
            if (*s == '\n')
                s++; /* cr/lf */
        } else
            *t = c;
        t++;
    }
    return {result, size_t(t - result)};
}

}

#define YY_USER_INIT initLoc(yylloc)
#define YY_USER_ACTION adjustLoc(yylloc, yytext, yyleng);

#define PUSH_STATE(state) yy_push_state(state, yyscanner)
#define POP_STATE() yy_pop_state(yyscanner)
