#ifndef __EVAL_H
#define __EVAL_H

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* Abstract syntax of Nix values:

   e := Deref(e) -- external expression
      | Hash(h) -- value reference
      | Str(s) -- string constant
      | Bool(b) -- boolean constant
      | Var(x) -- variable
      | App(e, e) -- application
      | Lam(x, e) -- lambda abstraction
      | Exec(platform, e, [Arg(e, e)])
          -- primitive; execute e with args e* on platform
      ;

   TODO: Deref(e) allows computed external expressions, which might be
   too expressive; perhaps this should be Deref(h).

   Semantics

   Each rule given as eval(e) => e', i.e., expression e has a normal
   form e'.

   eval(Deref(Hash(h))) => eval(loadExpr(h))

   eval(Hash(h)) => Hash(h) # idem for Str, Bool

   eval(App(e1, e2)) => eval(App(e1', e2))
     where e1' = eval(e1)

   eval(App(Lam(var, body), arg)) =>
     eval(subst(var, arg, body))

   eval(Exec(platform, prog, args)) => Hash(h)
     where
       fn = ... name of the output (random or by hashing expr) ...
       h =
         if exec( fn 
                , eval(platform) => Str(...)
                , getFile(eval(prog))
                , map(makeArg . eval, args)
                ) then
           hashPath(fn)
         else
           undef
       ... register ...

   makeArg(Arg(Str(nm), (Hash(h), h))) => (nm, getFile(h))
   makeArg(Arg(Str(nm), (Str(s), _))) => (nm, s)
   makeArg(Arg(Str(nm), (Bool(True), _))) => (nm, "1")
   makeArg(Arg(Str(nm), (Bool(False), _))) => (nm, undef)

   getFile :: Hash -> FileName
   loadExpr :: Hash -> FileName
   hashExpr :: Expr -> Hash 
   hashPath :: FileName -> Hash
   exec :: FileName -> Platform -> FileName -> [(String, String)] -> Status
*/

typedef ATerm Expr;


/* Evaluate an expression. */
Expr evalValue(Expr e);

/* Return a canonical textual representation of an expression. */
string printExpr(Expr e);

/* Hash an expression. */
Hash hashExpr(Expr e);


#endif /* !__EVAL_H */
