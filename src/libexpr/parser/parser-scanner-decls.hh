#pragma once

#ifndef BISON_HEADER
#  include "nix/util/util.hh"
NIX_INCOMPLETE_SWITCH_BEGIN
#  include "parser-tab.hh"
NIX_INCOMPLETE_SWITCH_END
using YYSTYPE = nix::parser::BisonParser::value_type;
using YYLTYPE = nix::parser::BisonParser::location_type;
#  include "lexer-tab.hh" // IWYU pragma: export
#endif

namespace nix {

class Parser : public parser::BisonParser
{
public:
    using BisonParser::BisonParser;
    ~Parser() override;
};

} // namespace nix
