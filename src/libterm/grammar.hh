// This file contains macro used to define the grammar.  You should use
// these to define the macro named TRM_GRAMMAR_NODES(Interface, Final)
// before inluding any of the libterm files.

#ifndef _LIBTERM_GRAMMAR_HH
# define _LIBTERM_GRAMMAR_HH

# include "pp.hh"


// These macro are used to define how to manipulate each argument.  If the
// argument should be given by reference or by copy.  You should use
// references when the element goes over a specific size and if you accept
// to see it living on the stack.  You should use copies when the element is
// small and if you prefer to have it inside a register.

# define TRM_TYPE_REF(Type, Name) (const Type&, Type &, Name)
# define TRM_TYPE_COPY(Type, Name) (const Type, Type &, Name)
# define TRM_TYPE_TERM(Type, Name) TRM_TYPE_COPY(A ## Type, Name)


// These macro are used as shortcuts for the declaration of common type of
// grammar nodes.

# define TRM_GRAMMAR_NODE_BINOP(Final, Name)                    \
  Final(                                                        \
    Name, Expr,                                                 \
    (2, (TRM_TYPE_TERM(Expr, lhs), TRM_TYPE_TERM(Expr, rhs))),  \
    (0,())                                                      \
  )

# define TRM_GRAMMAR_NODE_SINGLETON(Final, Name)        \
  Final(Name, Expr, (0,()), (0,()))


// Handle the different usage of the variables and attributes.  Arguments
// are suffixed with a '_' and attributes are not.  These macro are
// expecting the result of the TRM_TYPE macros as argument.

# define TRM_CONST_ABSTRACT_COPY_DECL_(Copy, Ref, Name) Copy Name;
# define TRM_CONST_ABSTRACT_COPY_ARG_(Copy, Ref, Name)  Copy Name ## _
# define TRM_ABSTRACT_REF_ARG_(Copy, Ref, Name)  Ref Name ## _
# define TRM_INIT_ATTRIBUTES_(Copy, Ref, Name)  Name (Name ## _)
# define TRM_ARGUMENTS_(Copy, Ref, Name)  Name ## _
# define TRM_COPY_PTR_ATTR_IN_ARG_(Copy, Ref, Name)  Name ## _ = ptr-> Name;
# define TRM_LESS_RHS_OR_(Copy, Ref, Name)  Name < arg_rhs. Name ||

// These macro are shortcuts used to remove extra parenthesies added by
// TRM_TYPE_* macros.  Without such parenthesies TRM_APPLY_HELPER won't be
// able to give the argument to these macros and arrays won't be well
// formed.

# define TRM_CONST_ABSTRACT_COPY_DECL(Elt) TRM_CONST_ABSTRACT_COPY_DECL_ Elt
# define TRM_CONST_ABSTRACT_COPY_ARG(Elt) TRM_CONST_ABSTRACT_COPY_ARG_ Elt
# define TRM_ABSTRACT_REF_ARG(Elt) TRM_ABSTRACT_REF_ARG_ Elt
# define TRM_INIT_ATTRIBUTES(Elt) TRM_INIT_ATTRIBUTES_ Elt
# define TRM_ARGUMENTS(Elt) TRM_ARGUMENTS_ Elt
# define TRM_COPY_PTR_ATTR_IN_ARG(Elt) TRM_COPY_PTR_ATTR_IN_ARG_ Elt
# define TRM_LESS_RHS_OR(Elt) TRM_LESS_RHS_OR_ Elt



/*
// Test case.  This should be moved inside the libexpr or generated in
// another manner.
# define TRM_GAMMAR_NODES(Interface, Final)
  Interface(Loc, Term, TRM_NIL, TRM_NIL)
  Interface(Pattern, Term, TRM_NIL, TRM_NIL)
  Interface(Expr, Term, TRM_NIL, TRM_NIL)
  Final(Pos, Loc, (TRM_TYPE_REF(std::string, file)
                   TRM_TYPE_COPY(int, line)
                   TRM_TYPE_COPY(int, column)), TRM_NIL)
  Final(NoPos, Loc, TRM_NIL, TRM_NIL)
  Final(String, Expr, (TRM_TYPE_REF(std::string, value)), TRM_NIL)
  Final(Var, Expr, (TRM_TYPE_REF(std::string, name)), TRM_NIL)
  Final(Path, Expr, (TRM_TYPE_REF(std::string, filename)), TRM_NIL)
  Final(Int, Expr, (TRM_TYPE_COPY(int, value)), TRM_NIL)
  Final(Function, Expr, (TRM_TYPE_TERM(Pattern, pattern)
                         TRM_TYPE_TERM(Expr, body)
                         TRM_TYPE_TERM(Pos, position)), TRM_NIL)
  Final(Assert, Expr, (TRM_TYPE_TERM(Expr, cond)
                       TRM_TYPE_TERM(Expr, body)
                       TRM_TYPE_TERM(Pos, position)), TRM_NIL)
  Final(With, Expr, (TRM_TYPE_TERM(Expr, set)
                     TRM_TYPE_TERM(Expr, body)
                     TRM_TYPE_TERM(Pos, position)), TRM_NIL)
  Final(If, Expr, (TRM_TYPE_TERM(Expr, cond)
                   TRM_TYPE_TERM(Expr, thenPart)
                   TRM_TYPE_TERM(Expr, elsePart)), TRM_NIL)
  Final(OpNot, Expr, (TRM_TYPE_TERM(Expr, cond)), TRM_NIL)
  TRM_GRAMMAR_NODE_BINOP(Final, OpEq)
  TRM_GRAMMAR_NODE_BINOP(Final, OpNEq)
  TRM_GRAMMAR_NODE_BINOP(Final, OpAnd)
  TRM_GRAMMAR_NODE_BINOP(Final, OpOr)
  TRM_GRAMMAR_NODE_BINOP(Final, OpImpl)
  TRM_GRAMMAR_NODE_BINOP(Final, OpUpdate)
  TRM_GRAMMAR_NODE_BINOP(Final, SubPath)
  TRM_GRAMMAR_NODE_BINOP(Final, OpHasAttr)
  TRM_GRAMMAR_NODE_BINOP(Final, OpPlus)
  TRM_GRAMMAR_NODE_BINOP(Final, OpConcat)
  Final(Call, Expr, (TRM_TYPE_TERM(Expr, function)
                     TRM_TYPE_TERM(Expr, argument)), TRM_NIL)
  Final(Select, Expr, (TRM_TYPE_TERM(Expr, set)
                       TRM_TYPE_TERM(Var, var)), TRM_NIL)
  Final(BlackHole, Expr, TRM_NIL, TRM_NIL)
  Final(Undefined, Expr, TRM_NIL, TRM_NIL)
  Final(Removed, Expr, TRM_NIL, TRM_NIL)

*/


#endif
