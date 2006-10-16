#ifndef __EXPR_TO_XML_H
#define __EXPR_TO_XML_H

#include <string>
#include <map>

#include "nixexpr.hh"
#include "aterm.hh"

namespace nix {

void printTermAsXML(Expr e, std::ostream & out, PathSet & context);
    
}

#endif /* !__EXPR_TO_XML_H */
