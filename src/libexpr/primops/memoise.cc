#include "primops.hh"
#include "eval-inline.hh"

#include <cstring>

namespace nix {

bool EvalState::MemoArgComparator::operator()(Value * v1, Value * v2)
{
    if (v1 == v2) return false;

    state.forceValue(*v1);
    state.forceValue(*v2);

    if (v1->type == v2->type) {
        switch (v1->type) {

        case tInt:
            return v1->integer < v2->integer;

        case tBool:
            return v1->boolean < v2->boolean;

        case tString:
            return strcmp(v1->string.s, v2->string.s);

        case tPath:
            return strcmp(v1->path, v2->path);

        case tNull:
            return false;

        case tList1:
        case tList2:
        case tListN:
            unsigned int n;
            for (n = 0; n < v1->listSize() && n < v2->listSize(); ++n) {
                if ((*this)(v1->listElems()[n], v2->listElems()[n])) return true;
                if ((*this)(v2->listElems()[n], v1->listElems()[n])) return false;
            }

            return n == v1->listSize() && n < v2->listSize();

        case tAttrs:
            Bindings::iterator i, j;
            for (i = v1->attrs->begin(), j = v2->attrs->begin(); i != v1->attrs->end() && j != v2->attrs->end(); ++i, ++j) {
                if (i->name < j->name) return true;
                if (j->name < i->name) return false;
                if ((*this)(i->value, j->value)) return true;
                if ((*this)(j->value, i->value)) return false;
            }
            return i == v1->attrs->end() && j != v2->attrs->end();

        case tLambda:
            return std::make_pair(v1->lambda.env, v1->lambda.fun) < std::make_pair(v2->lambda.env, v2->lambda.fun);

        case tFloat:
            return v1->fpoint < v2->fpoint;

        default:
            break;
        }
    }

    // As a fallback, use pointer equality.
    return v1 < v2;
}

void prim_memoise(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);

    EvalState::PerLambdaMemo foo(state);

    auto & memo = state.memos.emplace(std::make_pair(args[0]->lambda.env, args[0]->lambda.fun), state).first->second;

    auto result = memo.find(args[1]);

    if (result != memo.end()) {
        state.nrMemoiseHits++;
        v = result->second;
        return;
    }

    state.nrMemoiseMisses++;

    state.callFunction(*args[0], *args[1], v, pos);

    memo[args[1]] = v;

}

static RegisterPrimOp r("memoise", 2, prim_memoise);

}
