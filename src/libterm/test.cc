
#include <vector>

#define TRM_GRAMMAR_NODES(Interface, Final)                     \
  Interface(Expr, Term, (0, ()), (0, ()))                       \
  TRM_GRAMMAR_NODE_BINOP(Final, Plus)                           \
  Final(Int, Expr, (1, (TRM_TYPE_COPY(int, value))), (0, ()))

#include "term.hh"
#undef TRM_GRAMMAR_NODES

struct Eval : public term::ATermVisitor
{
  int run(const term::ATerm t)
  {
    return term::as<term::AInt>(t.accept(*this))().value;
  }

  term::ATerm visit(const term::APlus p)
  {
    return term::Int::make(run(p().lhs) + run(p().rhs));
  }
};

int main()
{
  using namespace term;
  AInt a = Int::make(1);
  AInt b = Int::make(2);
  AInt c = Int::make(1);

  // assert(a == c);
  Eval e;
  return e.run(Plus::make(a, Plus::make(b, c)));
}
