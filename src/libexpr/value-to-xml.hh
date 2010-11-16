#ifndef __VALUE_TO_XML_H
#define __VALUE_TO_XML_H

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, std::ostream & out, PathSet & context);
    
}

#endif /* !__VALUE_TO_XML_H */
