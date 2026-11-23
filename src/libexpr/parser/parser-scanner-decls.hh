#pragma once

#ifndef BISON_HEADER
#  include "nix/util/util.hh"
NIX_INCOMPLETE_SWITCH_BEGIN
#  include "parser-tab.hh"
NIX_INCOMPLETE_SWITCH_END
#  include "lexer.hh"
#endif

namespace nix {

class Parser : public parser::BisonParser
{
public:
    using BisonParser::BisonParser;
    ~Parser() override;
};

} // namespace nix
