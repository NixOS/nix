#include "lexer-helpers.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/expr/parser-state.hh"
#include "nix/util/pos-idx.hh"

#include <cstddef>

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
