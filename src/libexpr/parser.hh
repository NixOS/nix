#ifndef __PARSER_H
#define __PARSER_H

#include "eval.hh"


namespace nix {


/* Parse a Nix expression from the specified file.  If `path' refers
   to a directory, then "/default.nix" is appended. */
Expr * parseExprFromFile(EvalState & state, Path path);

/* Parse a Nix expression from the specified string. */
Expr * parseExprFromString(EvalState & state, const string & s, const Path & basePath);


}


#endif /* !__PARSER_H */
