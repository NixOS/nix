
#ifndef _LIBTERM_VISITOR_HH
# define _LIBTERM_VISITOR_HH

# include "visitor.hh"
# include "term.hh"

namespace term
{

// definition of the ATermVisitor visit functions.
# define TRM_VISITOR(Name, Base, Attributes, BaseArgs)  \
  ATerm                                                 \
  ATermVisitor::visit(const A ## Name t) {              \
    return t;                                           \
  }

  TRM_VISITOR(Term, TRM_NIL, TRM_NIL, TRM_NIL)
  TRM_GRAMMAR_NODES(TRM_VISITOR, TRM_VISITOR)

# undef TRM_VISITOR

  namespace impl
  {
    template <typename T>
    class asVisitor : ATermVisitor
    {
    public:
      asVisitor(ATerm t)
        : res()
      {
        t.accept(*this);
      }

      ATerm visit(const T t)
      {
        return res = t;
      }

    public:
      T res;
    };
  }

  // This function will return a zero ATerm if the element does not have the
  // expected type.
  template <typename T>
  inline
  T as(ATerm t)
  {
    impl::asVisitor<T> v(t);
    return v.res;
  }

}

#endif
