#include "parser.hh"

#include "parser-scanner-decls.hh"
#include "lexer-helpers.hh"
#include "parser-state.hh"

#include "nix/util/finally.hh"
#include "nix/util/source-accessor.hh"

namespace nix {

void parser::BisonParser::error(const location_type & loc_, const std::string & error)
{
    auto loc = loc_;
    /* We can do better with parse.error custom. */
    bool isUnexpectedEOF = std::string_view(error).starts_with("syntax error, unexpected end of file");
    if (isUnexpectedEOF)
        loc.beginOffset = loc.endOffset;

    throw ParseError({.msg = HintFmt(error), .pos = state->positions[state->at(loc)]})
        /* REPL is the primary consumer of this distinction. */
        .setIncomplete(isUnexpectedEOF);
}

Parser::~Parser() {}

Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    Exprs & exprs,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS)
{
    yyscan_t scanner;
    LexerState lexerState{
        .positionToDocComment = docComments,
        .positions = positions,
        .origin = positions.addOrigin(origin, length),
    };
    ParserState state{
        .lexerState = lexerState,
        .exprs = exprs,
        .symbols = symbols,
        .positions = positions,
        .basePath = basePath,
        .origin = lexerState.origin,
        .rootFS = rootFS,
        .settings = settings,
    };

    yylex_init_extra(&lexerState, &scanner);
    Finally _destroy([&] { yylex_destroy(scanner); });

    yy_scan_buffer(text, length, scanner);
    Parser parser(scanner, &state);
    parser.parse();

    return state.result;
}

ExprAttrs * parseReplBindingsFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    Exprs & exprs,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS)
{
    yyscan_t scanner;
    LexerState lexerState{
        .positionToDocComment = docComments,
        .positions = positions,
        .origin = positions.addOrigin(origin, length),
    };
    ParserState state{
        .lexerState = lexerState,
        .exprs = exprs,
        .symbols = symbols,
        .positions = positions,
        .basePath = basePath,
        .origin = lexerState.origin,
        .rootFS = rootFS,
        .settings = settings,
    };

    yylex_init_extra(&lexerState, &scanner);
    Finally _destroy([&] { yylex_destroy(scanner); });

    yy_scan_buffer(text, length, scanner);
    lexer::internal::setReplBindingsMode(scanner);
    Parser parser(scanner, &state);
    parser.parse();

    assert(state.result);
    // state.result is Expr *, but the REPL_BINDINGS grammar rule
    // always produces an ExprAttrs via the binds1 production.
    auto bindings = dynamic_cast<ExprAttrs *>(state.result);
    assert(bindings);
    return bindings;
}

} // namespace nix
