#pragma once

#include <cstddef>

#include "parser-scanner-decls.hh"

namespace nix::lexer::internal {

void initLoc(Parser::location_type * loc);

void adjustLoc(yyscan_t yyscanner, Parser::location_type * loc, const char * s, size_t len);

} // namespace nix::lexer::internal
