#include "lexer-tab.hh"
#include "lexer-helpers.hh"
#include "parser-tab.hh"

void nix::lexer::internal::initLoc(YYLTYPE * loc)
{
    loc->first_line = loc->last_line = 0;
    loc->first_column = loc->last_column = 0;
}

void nix::lexer::internal::adjustLoc(yyscan_t yyscanner, YYLTYPE * loc, const char * s, size_t len)
{
    loc->stash();

    LexerState & lexerState = *yyget_extra(yyscanner);

    if (lexerState.docCommentDistance == 1) {
        // Preceding token was a doc comment.
        ParserLocation doc;
        doc.first_column = lexerState.lastDocCommentLoc.first_column;
        ParserLocation docEnd;
        docEnd.first_column = lexerState.lastDocCommentLoc.last_column;
        DocComment docComment{lexerState.at(doc), lexerState.at(docEnd)};
        PosIdx locPos = lexerState.at(*loc);
        lexerState.positionToDocComment.emplace(locPos, docComment);
    }
    lexerState.docCommentDistance++;

    loc->first_column = loc->last_column;
    loc->last_column += len;
}
