#ifndef __PARSER_H
#define __PARSER_H

#include <string>
#include <aterm2.h>

#include "util.hh"


typedef ATerm Expr;

Expr parseExprFromFile(const Path & path);


#endif /* !__PARSER_H */
