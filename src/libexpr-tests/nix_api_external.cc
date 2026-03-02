#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_external.h"

#include "nix/expr/tests/nix_api_expr.hh"
#include "nix/util/tests/string_callback.hh"

#include <gtest/gtest.h>

namespace nixC {

class MyExternalValueDesc : public NixCExternalValueDesc
{
public:
    MyExternalValueDesc(int x)
        : _x(x)
    {
        print = print_function;
        showType = show_type_function;
        typeOf = type_of_function;
    }

private:
    int _x;

    static void print_function(void * self, nix_printer * printer) {}

    static void show_type_function(void * self, nix_string_return * res) {}

    static void type_of_function(void * self, nix_string_return * res)
    {
        MyExternalValueDesc * obj = static_cast<MyExternalValueDesc *>(self);

        std::string type_string = "nix-external<MyExternalValueDesc( ";
        type_string += std::to_string(obj->_x);
        type_string += " )>";
        nix_set_string_return(res, &*type_string.begin());
    }
};

TEST_F(nix_api_expr_test, nix_expr_eval_external)
{
    MyExternalValueDesc * external = new MyExternalValueDesc(42);
    ExternalValue * val = nix_create_external_value(ctx, external, external);
    nix_init_external(ctx, value, val);

    EvalState * stateResult = nix_state_create(nullptr, nullptr, store);
    nix_value * valueResult = nix_alloc_value(nullptr, stateResult);

    EvalState * stateFn = nix_state_create(nullptr, nullptr, store);
    nix_value * valueFn = nix_alloc_value(nullptr, stateFn);

    nix_expr_eval_from_string(nullptr, state, "builtins.typeOf", ".", valueFn);

    ASSERT_EQ(NIX_TYPE_EXTERNAL, nix_get_type(nullptr, value));

    nix_value_call(ctx, state, valueFn, value, valueResult);

    std::string string_value;
    nix_get_string(nullptr, valueResult, OBSERVE_STRING(string_value));
    ASSERT_STREQ("nix-external<MyExternalValueDesc( 42 )>", string_value.c_str());

    nix_state_free(stateResult);
    nix_state_free(stateFn);
}

static void print_value_as_json_using_state(
    void * self, EvalState * state, bool strict, nix_string_context * c, bool copyToStore, nix_string_return * res)
{
    // Regression test: same cast bug as in nix_c_primop_wrapper (see primop_alloc_value).
    nix_value * v = nix_alloc_value(nullptr, state);
    assert(v != nullptr);
    nix_gc_decref(nullptr, v);

    nix_set_string_return(res, "42");
}

TEST_F(nix_api_expr_test, nix_external_printValueAsJSON_can_use_state)
{
    NixCExternalValueDesc desc{};
    desc.print = [](void *, nix_printer *) {};
    desc.showType = [](void *, nix_string_return *) {};
    desc.typeOf = [](void *, nix_string_return *) {};
    desc.printValueAsJSON = print_value_as_json_using_state;

    ExternalValue * val = nix_create_external_value(ctx, &desc, nullptr);
    assert_ctx_ok();
    nix_init_external(ctx, value, val);
    assert_ctx_ok();

    nix_value * toJsonFn = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(ctx, state, "builtins.toJSON", ".", toJsonFn);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    nix_value_call(ctx, state, toJsonFn, value, result);
    assert_ctx_ok();

    std::string json_str;
    nix_get_string(ctx, result, OBSERVE_STRING(json_str));
    ASSERT_EQ("42", json_str);

    nix_gc_decref(ctx, result);
    nix_gc_decref(ctx, toJsonFn);
}

} // namespace nixC
