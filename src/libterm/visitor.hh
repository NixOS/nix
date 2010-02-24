
#ifndef _LIBTERM_VISITOR_CLASS_FWD_HH
# define _LIBTERM_VISITOR_CLASS_FWD_HH

# include "term.hh"

namespace term
{
  // base class for visitor implementation.
  class ATermVisitor
  {
  public:
# define TRM_VISITOR(Name, Base, Attributes, BaseArgs)  \
    virtual ATerm visit(const A ## Name);

    TRM_VISITOR(Term, TRM_NIL, TRM_NIL, TRM_NIL)
    TRM_GRAMMAR_NODES(TRM_VISITOR, TRM_VISITOR)
# undef TRM_VISITOR
  };
}

#endif

