#include "lexer.hh"

#include "nix/util/pos-idx.hh"
#include "nix/util/pos-table.hh"

#include <cstdint> // IWYU pragma: keep

namespace nix::lexer {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"

[[gnu::noinline]]
static void addDocComment(LexerState & lexerState, parser::BisonParser::location_type * loc)
{
    ParserLocation doc;
    doc.beginOffset = lexerState.lastDocCommentLoc.beginOffset;
    ParserLocation docEnd;
    docEnd.beginOffset = lexerState.lastDocCommentLoc.endOffset;
    DocComment docComment{lexerState.at(doc), lexerState.at(docEnd)};
    PosIdx locPos = lexerState.at(*loc);
    lexerState.positionToDocComment.emplace(locPos, docComment);
}

static void adjustLoc(LexerState & lexerState, parser::BisonParser::location_type * loc, size_t len)
{
    loc->stash();

    if (lexerState.docCommentDistance == 1) [[unlikely]]
        addDocComment(lexerState, loc);
    lexerState.docCommentDistance++;

    loc->beginOffset = loc->endOffset;
    loc->endOffset += len;
}

/**
 * We make use of the fact that the parser receives a private copy of the input
 * string and can munge around in it.
 *
 * @note resolving a position is expensive, so @p pos is only resolved on error.
 */
static StringToken unescapeStr(char * const s, size_t length, const PosTable & positions, PosIdx pos)
{
    bool noNullByte = true;
    char * t = s;
    // The input string is NUL-terminated, so we can safely take
    // *one* character after the one being checked against.
    for (size_t i = 0; i < length; t++) {
        char c = s[i++];
        noNullByte &= c != '\0';
        if (c == '\\') {
            c = s[i++];
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
            if (s[i] == '\n')
                i++; /* cr/lf */
        } else
            *t = c;
    }
    if (!noNullByte)
        throwNullByteError({s, size_t(t - s)}, &positions, pos);
    return {s, size_t(t - s)};
}

[[noreturn, gnu::cold, gnu::noinline]] static void
throwExperimentalFeatureDisabled(const ExperimentalFeature & feature, const PosTable & positions, PosIdx pos)
{
    throw ParseError(
        ErrorInfo{
            .msg = HintFmt(
                "experimental Nix feature '%1%' is disabled; add '--extra-experimental-features %1%' to enable it",
                showExperimentalFeature(feature)),
            .pos = positions[pos],
        });
}

/**
 * Resolving a position is expensive, so @p pos is only resolved on error.
 */
static void requireExperimentalFeature(const ExperimentalFeature & feature, const PosTable & positions, PosIdx pos)
{
    if (!experimentalFeatureSettings.isEnabled(feature)) [[unlikely]]
        throwExperimentalFeatureDisabled(feature, positions, pos);
}

parser::BisonParser::token_kind_type yylex(
    parser::BisonParser::value_type * yylval,
    parser::BisonParser::location_type * yylloc,
    Lexer * lexer,
    ParserState * state)
{
    using enum parser::BisonParser::token_kind_type;
    using Cond = Lexer::Cond;

    const char * const limit = lexer->limit;
    char * cur = lexer->cur;

    /* For rollbacks. No initialisation needed. */
    char * YYMARKER;

    using TokenType = parser::BisonParser::token_kind_type;

#define CUR_POS state->at(*yylloc)
#define yyleng static_cast<std::size_t>(lexer->cur - yytext)
#define PUSH_STATE(STATE) (lexer->pushState(Cond::STATE))
#define POP_STATE() (lexer->popState())

    LexerState & lexerState = lexer->getState();

    while (true) {
        /* Start of the potential match. Inside the actions YYCURSOR is already incremented.
           Each action iteration gets a fresh match, so that token skipping (e.g. comments)
           gets the right position information. */
        char * yytext = cur;

        /*!re2c
            re2c:api:style         = free-form;
            re2c:case-ranges       = 1;
            re2c:define:YYCTYPE    = uint8_t;
            re2c:define:YYCURSOR   = cur;
            re2c:define:YYLIMIT    = limit;
            re2c:define:YYGETCOND  = (lexer->currentCond);
            // We read whole files into memory.
            re2c:yyfill:enable     = 0;
            // The lexer is context-dependent so we maintain a separate "condition" stack.
            re2c:cond:enumprefix   = "Cond::";
            // NUL in addition to YYLIMIT is the EOF indicator.
            re2c:eof               = 0;

            ANY           = [^];
            ID            = [a-zA-Z_][a-zA-Z0-9_'-]*;
            INT           = [0-9]+;
            FLOAT         = (([1-9][0-9]* "." [0-9]*) | ("0"? "." [0-9]+)) ([Ee][+-]?[0-9]+)?;
            PATH_CHAR     = [a-zA-Z0-9._\-+];
            PATH          = PATH_CHAR* ("/" PATH_CHAR+)+ "/"?;
            PATH_SEG      = PATH_CHAR* "/";
            HPATH         = "~" ("/" PATH_CHAR+)+ "/"?;
            HPATH_START   = "~/";
            SPATH         = "<" PATH_CHAR+ ("/" PATH_CHAR+)* ">";
            URI           = [a-zA-Z][a-zA-Z0-9+\-.]* ":" [a-zA-Z0-9%/?:@&=+$,\-_.!~*']+;

            STR_CHAR      = [^$"\\] | "$" [^{"\\] | "\\" ANY | "$\\" ANY;
            IND_STR_CHAR  = [^$'] | "$" [^{'] | "'" [^'$];
            SCOM          = "#" [^\r\n]*; // single-line comments
            LCOM          = "/*" ([^*] | "*"+ [^/*])* "*"+ "/"; // long comments
            DCOM          = "/**" [^/*] ([^*] | "*"+ [^/*])* "*"+ "/"; // doc comments

            // Setting position information for each matched token.
            // TODO: Can we be more efficient?
            <*> !pre_rule { lexer->cur = cur; adjustLoc(lexerState, yylloc, yyleng); }

            // Keywords, though "or" is a bit special and can be used as an identifier sometimes.

            <DEFAULT> "if"          { return IF;       }
            <DEFAULT> "then"        { return THEN;     }
            <DEFAULT> "else"        { return ELSE;     }
            <DEFAULT> "assert"      { return ASSERT;   }
            <DEFAULT> "with"        { return WITH;     }
            <DEFAULT> "let"         { return LET;      }
            <DEFAULT> "in"          { return IN_KW;    }
            <DEFAULT> "rec"         { return REC;      }
            <DEFAULT> "inherit"     { return INHERIT;  }
            <DEFAULT> "or"          { return OR_KW;    }
            <DEFAULT> "..."         { return ELLIPSIS; }

            // Operators

            <DEFAULT> "=="          { return EQ;       }
            <DEFAULT> "!="          { return NEQ;      }
            <DEFAULT> "<="          { return LEQ;      }
            <DEFAULT> ">="          { return GEQ;      }
            <DEFAULT> "&&"          { return AND;      }
            <DEFAULT> "||"          { return OR;       }
            <DEFAULT> "->"          { return IMPL;     }
            <DEFAULT> "//"          { return UPDATE;   }
            <DEFAULT> "++"          { return CONCAT;   }
            <DEFAULT> "<|"          { requireExperimentalFeature(Xp::PipeOperators, state->positions, CUR_POS);
                                      return PIPE_FROM;
                                    }
            <DEFAULT> "|>"          { requireExperimentalFeature(Xp::PipeOperators, state->positions, CUR_POS);
                                      return PIPE_INTO;
                                    }

            // Literals with value attached.

            <DEFAULT> ID        { yylval->emplace<StringToken>(yytext, yyleng); return ID; }

            <DEFAULT> INT       { std::string_view view = std::string_view(yytext, yyleng);
                                  if (std::optional<int64_t> numMay = string2Int<int64_t>(view); numMay.has_value()) {
                                      yylval->emplace<NixInt>(*numMay);
                                      return INT_LIT;
                                  }
                                  throw ParseError(ErrorInfo{
                                      .msg = HintFmt("invalid integer '%1%'", view),
                                      .pos = state->positions[CUR_POS],
                                  });
                                }
            <DEFAULT> FLOAT     { std::string val = std::string(yytext, yyleng);
                                  errno = 0;
                                  double value = strtod(val.c_str(), nullptr);
                                  if (errno == 0) {
                                      yylval->emplace<NixFloat>(value);
                                      return FLOAT_LIT;
                                  }
                                  throw ParseError(ErrorInfo{
                                      .msg = HintFmt("invalid float '%1%'", val),
                                      .pos = state->positions[CUR_POS],
                                  });
                                }

            <DEFAULT> "${"      { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
            <DEFAULT> "}"       { if (lexer->condStack.size() > 1) lexer->popState(); return TokenType('}'); }
            <DEFAULT> "{"       { PUSH_STATE(DEFAULT); return TokenType('{'); }

            // Handle string rules and state transitions.

            <DEFAULT> '"'       { PUSH_STATE(STRING); return TokenType('"'); }
            // Common prefix for the rules below.
            <STRING> [^"$\\]+   { goto unescape_yytext; }
            <STRING> STR_CHAR+  { goto unescape_yytext; }
            <STRING> STR_CHAR* "$" / '"' { goto unescape_yytext; }

            // This can only occur when we reach EOF, otherwise the above
            // STR_CHAR+ would have triggered.
            // This is technically invalid, but we leave the problem to the
            // parser who fails with exact location.
            <STRING> "$" | "\\" | "$\\" { return YYEOF; }

            // String interpolation.
            <STRING> "${"       { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
            <STRING> '"'        { POP_STATE(); return TokenType('"'); }

            // Indented string states and rules.

            <DEFAULT> "''" (" "* "\n")? { PUSH_STATE(IND_STRING); return IND_STRING_OPEN; }
            <IND_STRING> IND_STR_CHAR+  { yylval->emplace<StringToken>(yytext, yyleng, true);
                                          forceNoNullByte(yylval->as<StringToken>(), &state->positions, CUR_POS);
                                          return IND_STR;
                                        }
            <IND_STRING> "''$" | "$"    { yylval->emplace<StringToken>("$", 1); return IND_STR; }
            <IND_STRING> "'''"          { yylval->emplace<StringToken>("''", 2); return IND_STR; }
            <IND_STRING> "''\\" ANY     { yylval->emplace<StringToken>(
                                            unescapeStr(yytext + 2, yyleng - 2, state->positions, CUR_POS)
                                          );
                                          return IND_STR;
                                        }
            <IND_STRING> "${"           { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
            <IND_STRING> "''"           { POP_STATE(); return IND_STRING_CLOSE; }
            <IND_STRING> "'"            { yylval->emplace<StringToken>("'", 1); return IND_STR; }

            <DEFAULT> PATH_SEG "${" | HPATH_START "${"
                                        { PUSH_STATE(PATH_START);
                                          cur = yytext;
                                          yylloc->unstash();
                                          continue;
                                        }

            <PATH_START> PATH_SEG       { POP_STATE();
                                          PUSH_STATE(INPATH_SLASH);
                                          yylval->emplace<StringToken>(yytext, yyleng);
                                          return PATH;
                                        }

            <PATH_START> HPATH_START    { POP_STATE();
                                          PUSH_STATE(INPATH_SLASH);
                                          yylval->emplace<StringToken>(yytext, yyleng);
                                          return HPATH;
                                        }

            // This should be unreachable: PATH_START is only entered after matching
            // PATH_SEG or HPATH_START, and we rewind to re-parse those same patterns.
            // This rule exists to satisfy flex's %option nodefault requirement.
            <PATH_START> ANY            { unreachable(); }
            <PATH_START> $              { unreachable(); }

            <DEFAULT> PATH              { if (yytext[yyleng - 1] == '/')
                                            PUSH_STATE(INPATH_SLASH);
                                          else
                                            PUSH_STATE(INPATH);
                                          yylval->emplace<StringToken>(yytext, yyleng);
                                          return PATH;
                                        }

            <DEFAULT> HPATH             { if (yytext[yyleng - 1] == '/')
                                            PUSH_STATE(INPATH_SLASH);
                                          else
                                            PUSH_STATE(INPATH);
                                          yylval->emplace<StringToken>(yytext, yyleng);
                                          return HPATH;
                                        }

            <INPATH,INPATH_SLASH> "${"  { POP_STATE(); PUSH_STATE(INPATH); PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
            <INPATH,INPATH_SLASH> PATH | PATH_SEG | PATH_CHAR+
                                        { POP_STATE();
                                          if (yytext[yyleng - 1] == '/')
                                            PUSH_STATE(INPATH_SLASH);
                                          else
                                            PUSH_STATE(INPATH);
                                          yylval->emplace<StringToken>(yytext, yyleng);
                                          return STR;
                                        }

            // If we encounter a non-path character we inform the parser that the path has
            // ended with a PATH_END token and re-parse this character in the default
            // context (it may be ')', ';', or something of that sort)
            <INPATH> ANY  { goto path_end; }
            <INPATH> $    { goto path_end; }

            <INPATH_SLASH> ANY  { goto path_trailing_slash; }
            <INPATH_SLASH> $    { goto path_trailing_slash; }

            <DEFAULT> SPATH     { yylval->emplace<StringToken>(yytext, yyleng); return SPATH; }
            <DEFAULT> URI       { yylval->emplace<StringToken>(yytext, yyleng); return URI; }

            // The following rules have docCommentDistance--
            // This compensates for the docCommentDistance++ which happens by default to
            // make all the other rules invalidate the doc comment.

            <DEFAULT> [ \t\r\n]+ { lexer->state.docCommentDistance--; continue; }
            <DEFAULT> SCOM       { lexer->state.docCommentDistance--; continue; }
            <DEFAULT> DCOM       { lexerState.docCommentDistance = 0;
                                   lexerState.lastDocCommentLoc.beginOffset = yylloc->beginOffset;
                                   lexerState.lastDocCommentLoc.endOffset = yylloc->endOffset;
                                   continue;
                                 }
            <DEFAULT> LCOM       { lexer->state.docCommentDistance--; continue; }

            <*> $         { return YYEOF; }
            <DEFAULT> *   { // Bison reserves 0-255 range of token enum values for this sort
                            // sort of thing. Since the underlying type is an int, make sure that
                            // negative char values are first converted to unsigned char so that
                            // they are faithfully represented in the token enum.
                            return static_cast<TokenType>(static_cast<unsigned char>(*yytext));
                          }

            // REPL bindings mode: inject REPL_BINDINGS token at start, then switch to normal lexing.
            <REPL_BINDINGS_MODE> ANY { lexer->cur = yytext;
                                       yylloc->unstash();
                                       POP_STATE();
                                       return REPL_BINDINGS;
                                     }
        */
    /* Handle simple string literals here. */
    unescape_yytext: {
        yylval->emplace<StringToken>(unescapeStr(yytext, yyleng, state->positions, CUR_POS));
        return STR;
    }
    path_trailing_slash: {
        throw ParseError(
            ErrorInfo{
                .msg = HintFmt("path has a trailing slash"),
                .pos = state->positions[CUR_POS],
            });
    }
    path_end: {
        POP_STATE();
        lexer->cur = yytext;
        yylloc->unstash();
        return PATH_END;
    }
    }

#pragma GCC diagnostic pop

#undef POP_STATE
#undef PUSH_STATE
#undef yyleng
#undef CUR_POS
}

} // namespace nix::lexer
