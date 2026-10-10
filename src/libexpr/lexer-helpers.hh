#pragma once

#include <cstddef>

#include "parser-scanner-decls.hh"

namespace nix::lexer::internal {

void initLoc(Parser::location_type * loc);

void adjustLoc(yyscan_t yyscanner, Parser::location_type * loc, const char * s, size_t len);

/**
 * Puts the lexer in REPL bindings mode before the first token. This causes
 * the parser to accept REPL bindings (attribute definitions).
 */
void setReplBindingsMode(yyscan_t scanner);

} // namespace nix::lexer::internal
