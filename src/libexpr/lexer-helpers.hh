#pragma once

namespace nix::lexer::internal {

void initLoc(YYLTYPE * loc);

void adjustLoc(yyscan_t yyscanner, YYLTYPE * loc, const char * s, size_t len);

} // namespace nix::lexer
