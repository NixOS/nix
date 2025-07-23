#pragma once

#include <cstddef>

// including the generated headers twice leads to errors
#ifndef BISON_HEADER
#  include "lexer-tab.hh"
#  include "parser-tab.hh"
#endif

namespace nix::lexer::internal {

void initLoc(YYLTYPE * loc);

void adjustLoc(yyscan_t yyscanner, YYLTYPE * loc, const char * s, size_t len);

} // namespace nix::lexer::internal
