#include "parser-scanner-decls.hh"
#include "lexer-helpers.hh"
#include "nix/expr/parser.hh"

void nix::parser::BisonParser::error(const location_type & loc_, const std::string & error)
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

nix::Parser::~Parser() {}

namespace nix {

namespace {

Expr * parseBison(std::span<char> input, ParserState & state, bool repl)
{
    yyscan_t scanner;
    yylex_init_extra(&state.lexerState, &scanner);
    Finally destroy([&] { yylex_destroy(scanner); });
    yy_scan_buffer(input.data(), input.size(), scanner);
    if (repl)
        lexer::internal::setReplBindingsMode(scanner);
    Parser parser(scanner, &state);
    parser.parse();
    assert(state.result);
    return state.result;
}

template<ParserBackend Backend, bool Repl>
auto parseWithBackend(std::span<char> input, ParserState & state)
{
    auto backend = std::make_obj_using_allocator<Backend>(typename Backend::traits_type::allocator_type{});
    auto result = [&] {
        if constexpr (Repl)
            return backend.parseReplBindings(input, state);
        else
            return backend.parseExpr(input, state);
    }();
    if (!result)
        throw std::move(result.error());
    return &result->root();
}

/** Both entry points share parser-state setup. */
template<bool Repl>
auto parseBuffer(
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
    if (length < 2 || text[length - 1] != '\0' || text[length - 2] != '\0')
        throw Error("parser input must end in two NUL bytes");
    LexerState lexerState{
        .positionToDocComment = docComments,
        .positions = positions,
        .origin = positions.addOrigin(std::move(origin), length),
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
    auto input = std::span(text, length);
    return parseWithBackend<BisonParserBackend, Repl>(input, state);
}

} // namespace

std::expected<ParsedAst<Expr>, ParseError> BisonParserBackend::parseExpr(std::span<char> input, ParserState & state)
try {
    return ParsedAst(*parseBison(input, state, false), state.exprs);
} catch (ParseError & error) {
    return std::unexpected(std::move(error));
}

std::expected<ParsedAst<ExprAttrs>, ParseError>
BisonParserBackend::parseReplBindings(std::span<char> input, ParserState & state)
try {
    auto bindings = dynamic_cast<ExprAttrs *>(parseBison(input, state, true));
    assert(bindings);
    return ParsedAst(*bindings, state.exprs);
} catch (ParseError & error) {
    return std::unexpected(std::move(error));
}

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
    return parseBuffer<false>(
        text, length, std::move(origin), basePath, exprs, symbols, settings, positions, docComments, rootFS);
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
    return parseBuffer<true>(
        text, length, std::move(origin), basePath, exprs, symbols, settings, positions, docComments, rootFS);
}

} // namespace nix
