#ifndef __PARSER_H
#define __PARSER_H

#include "nixexpr.hh"


/* Parse a Nix expression from the specified file.  If `path' refers
   to a directory, the "/default.nix" is appended. */
Expr parseExprFromFile(Path path);

/* Parse a Nix expression from the specified string. */
Expr parseExprFromString(const string & s,
    const Path & basePath);


#endif /* !__PARSER_H */
