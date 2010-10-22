#ifndef __ATTR_PATH_H
#define __ATTR_PATH_H

#include "eval.hh"

#include <string>
#include <map>


namespace nix {

    
void findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Expr * e, Value & v);

    
}


#endif /* !__ATTR_PATH_H */
