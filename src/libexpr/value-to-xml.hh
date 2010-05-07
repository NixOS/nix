#ifndef __VALUE_TO_XML_H
#define __VALUE_TO_XML_H

#include <string>
#include <map>

#include "nixexpr.hh"
#include "eval.hh"

namespace nix {

void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, std::ostream & out, PathSet & context);
    
}

#endif /* !__VALUE_TO_XML_H */
