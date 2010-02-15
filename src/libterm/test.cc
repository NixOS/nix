
#include <vector>

#define TRM_GRAMMAR_NODES(Interface, Final)                     \
  Interface(Expr, Term, (0, ()), (0, ()))                       \
  TRM_GRAMMAR_NODE_BINOP(Final, Plus)                           \
  Final(Int, Expr, (1, (TRM_TYPE_COPY(int, value))), (0, ()))

#include "term.hh"
#undef TRM_GRAMMAR_NODES

class Eval : public term::ATermVisitor
{
  int run(const ATerm t)
  {
    return term::as<AInt>(t.accept(*this))().value;
  }

  ATerm visit(const APlus p)
  {
    return makeInt(run(p().lhs) + run(p().rhs));
  }
};

int main()
{
  AInt a = makeInt(1);
  AInt b = makeInt(2);
  AInt c = makeInt(1);

  assert(a == c);
  Eval e();
  return e.run(makePlus(a, makePlus(b, c)));
}
