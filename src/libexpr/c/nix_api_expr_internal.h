#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

#include "eval.hh"
#include "attr-set.hh"
#include "nix_api_value.h"

class CListBuilder
{
private:
    std::vector<nix::Value *> values;

public:
    CListBuilder(size_t capacity)
    {
        values.reserve(capacity);
    }

    void push_back(nix::Value * value)
    {
        values.push_back(value);
    }

    Value * finish(nix::EvalState * state, nix::Value * list)
    {
        state->mkList(*list, values.size());
        for (size_t n = 0; n < list->listSize(); ++n) {
            list->listElems()[n] = values[n];
        }
        return list;
    }
};

struct EvalState
{
    nix::EvalState state;
};

struct BindingsBuilder
{
    nix::BindingsBuilder builder;
};

struct ListBuilder
{
    CListBuilder builder;
};

struct nix_string_return
{
    std::string str;
};

struct nix_printer
{
    std::ostream & s;
};

struct nix_string_context
{
    nix::NixStringContext & ctx;
};

#endif // NIX_API_EXPR_INTERNAL_H
