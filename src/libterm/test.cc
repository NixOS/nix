
#include <vector>
#include <iostream>

#define TRM_GRAMMAR_NODES(Interface, Final)                     \
  Interface(Expr, Term, (0, ()), (0, ()))                       \
  TRM_GRAMMAR_NODE_BINOP(Final, Plus)                           \
  Final(Int, Expr, (1, (TRM_TYPE_COPY(int, value))), (0, ()))

#include "term_impl.hh"
#include "visitor_impl.hh"
#undef TRM_GRAMMAR_NODES

using namespace term;

struct Eval : public ATermVisitor
{
  int run(const ATerm t)
  {
    return as<AInt>(t.accept(*this))->value;
  }

  ATerm visit(const APlus p)
  {
    return Int::make(run(p->lhs) + run(p->rhs));
  }
};

#define CHECK(Cond, Msg)                        \
  if (Cond)                                     \
  {                                             \
    good++;                                     \
    std::cout << "Ok: " << Msg << std::endl;    \
  }                                             \
  else                                          \
  {                                             \
    std::cout << "Ko: " << Msg << std::endl;    \
  }                                             \
  tests++

int main()
{
  unsigned good, tests;

  using namespace term;
  AInt a = makeInt(1);
  AInt b = makeInt(2);
  AInt c = makeInt(1);
  Eval e;

  CHECK(a == c, "Terms are shared.");
  CHECK(!as<APlus>(a), "Bad convertion returns a zero ATerm.");
  CHECK(as<AInt>(a) == a, "Good convertion returns the same ATerm.");
  CHECK(e.run(makePlus(a, makePlus(b, c))) == 4, "Visitors are working.");
  return tests - good;
}
