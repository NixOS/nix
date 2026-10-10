#include "lexer-helpers.hh"

void nix::lexer::internal::initLoc(Parser::location_type * loc)
{
    loc->beginOffset = loc->endOffset = 0;
}

void nix::lexer::internal::adjustLoc(yyscan_t yyscanner, Parser::location_type * loc, const char * s, size_t len)
{
    loc->stash();

    LexerState & lexerState = *yyget_extra(yyscanner);

    if (lexerState.docCommentDistance == 1) {
        // Preceding token was a doc comment.
        ParserLocation doc;
        doc.beginOffset = lexerState.lastDocCommentLoc.beginOffset;
        ParserLocation docEnd;
        docEnd.beginOffset = lexerState.lastDocCommentLoc.endOffset;
        DocComment docComment{lexerState.at(doc), lexerState.at(docEnd)};
        PosIdx locPos = lexerState.at(*loc);
        lexerState.positionToDocComment.emplace(locPos, docComment);
    }
    lexerState.docCommentDistance++;

    loc->beginOffset = loc->endOffset;
    loc->endOffset += len;
}

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
