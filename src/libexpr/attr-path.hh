#ifndef __ATTR_PATH_H
#define __ATTR_PATH_H

#include <string>
#include <map>

#include "eval.hh"


Expr findAlongAttrPath(EvalState & state, const string & attrPath, Expr e);


#endif /* !__ATTR_PATH_H */
