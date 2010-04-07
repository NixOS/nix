#ifndef __EXPR_TO_XML_H
#define __EXPR_TO_XML_H

#include <string>
#include <map>

#include "nixexpr.hh"
#include "eval.hh"

namespace nix {

void printValueAsXML(EvalState & state, bool strict,
    Value & v, std::ostream & out, PathSet & context);
    
}

#endif /* !__EXPR_TO_XML_H */
