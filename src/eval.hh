#ifndef __EVAL_H
#define __EVAL_H

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* Abstract syntax of Nix values:

   e := Hash(h) -- reference to expression value
      | External(h) -- reference to non-expression value
      | Str(s) -- string constant
      | Bool(b) -- boolean constant
      | App(e, e) -- application
      | Lam(x, e) -- lambda abstraction
      | Exec(platform, e, [(s, e)])
          -- primitive; execute e with args e* on platform
      ;

   Semantics

   Each rules given as eval(e) => (e', h'), i.e., expression e has a
   normal form e' with hash code h'.  evalE = fst . eval.  evalH = snd
   . eval.

   eval(Hash(h)) => eval(loadExpr(h))

   eval(External(h)) => (External(h), h)

   eval(Str(s)@e) => (e, 0) # idem for Bool

   eval(App(e1, e2)) => eval(App(e1', e2))
     where e1' = evalE(e1)

   eval(App(Lam(var, body), arg)@in) =>
     eval(subst(var, arg, body))@out
     [AND write out to storage, and dbNFs[hash(in)] = hash(out) ???]

   eval(Exec(platform, prog, args)@e) =>
     (External(h), h)
     where
       hIn = hashExpr(e)

       fn = ... form name involving hIn ...

       h =
         if exec(evalE(platform) => Str(...)
                , getFile(evalH(prog))
                , map(makeArg . eval, args)
                ) then
           hashExternal(fn)
         else
           undef

   makeArg((argn, (External(h), h))) => (argn, getFile(h))
   makeArg((argn, (Str(s), _))) => (argn, s)
   makeArg((argn, (Bool(True), _))) => (argn, "1")
   makeArg((argn, (Bool(False), _))) => (argn, undef)

   getFile :: Hash -> FileName
   loadExpr :: Hash -> FileName
   hashExpr :: Expr -> Hash 
   hashExternal :: FileName -> Hash
   exec :: Platform -> FileName -> [(String, String)] -> Status
*/

typedef ATerm Expr;


struct EvalResult 
{
    Expr e;
    Hash h;
};


/* Evaluate an expression. */
EvalResult evalValue(Expr e);


#endif /* !__EVAL_H */
