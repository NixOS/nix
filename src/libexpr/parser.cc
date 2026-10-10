#include "parser-scanner-decls.hh"

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
