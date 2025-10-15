#pragma once

#ifndef BISON_HEADER
#  include "parser-tab.hh"
using YYSTYPE = nix::parser::BisonParser::value_type;
using YYLTYPE = nix::parser::BisonParser::location_type;
#  include "lexer-tab.hh" // IWYU pragma: export
#endif

namespace nix {

class Parser : public parser::BisonParser
{
    using BisonParser::BisonParser;
};

} // namespace nix
