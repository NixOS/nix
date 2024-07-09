#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_expr_internal.h"

#include "tests/nix_api_expr.hh"
#include "tests/string_callback.hh"

#include "gmock/gmock.h"
#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_expr_test, as_nix_value_ptr)
{
    // nix_alloc_value casts nix::Value to nix_value
    // It should be obvious from the decl that that works, but if it doesn't,
    // the whole implementation would be utterly broken.
    ASSERT_EQ(sizeof(nix::Value), sizeof(nix_value));
}

TEST_F(nix_api_expr_test, nix_value_get_int_invalid)
{
    ASSERT_EQ(0, nix_get_int(ctx, nullptr));
    assert_ctx_err();
    ASSERT_EQ(0, nix_get_int(ctx, value));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_value_set_get_int)
{
    int myInt = 1;
    nix_init_int(ctx, value, myInt);

    ASSERT_EQ(myInt, nix_get_int(ctx, value));
    ASSERT_STREQ("an integer", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_INT, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_float_invalid)
{
    ASSERT_DOUBLE_EQ(0.0, nix_get_float(ctx, nullptr));
    assert_ctx_err();
    ASSERT_DOUBLE_EQ(0.0, nix_get_float(ctx, value));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_value_set_get_float)
{
    double myDouble = 1.0;
    nix_init_float(ctx, value, myDouble);

    ASSERT_DOUBLE_EQ(myDouble, nix_get_float(ctx, value));
    ASSERT_STREQ("a float", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_FLOAT, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_bool_invalid)
{
    ASSERT_EQ(false, nix_get_bool(ctx, nullptr));
    assert_ctx_err();
    ASSERT_EQ(false, nix_get_bool(ctx, value));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_value_set_get_bool)
{
    bool myBool = true;
    nix_init_bool(ctx, value, myBool);

    ASSERT_EQ(myBool, nix_get_bool(ctx, value));
    ASSERT_STREQ("a Boolean", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_BOOL, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_string_invalid)
{
    std::string string_value;
    ASSERT_EQ(NIX_ERR_UNKNOWN, nix_get_string(ctx, nullptr, OBSERVE_STRING(string_value)));
    assert_ctx_err();
    ASSERT_EQ(NIX_ERR_UNKNOWN, nix_get_string(ctx, value, OBSERVE_STRING(string_value)));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_value_set_get_string)
{
    std::string string_value;
    const char * myString = "some string";
    nix_init_string(ctx, value, myString);

    nix_get_string(ctx, value, OBSERVE_STRING(string_value));
    ASSERT_STREQ(myString, string_value.c_str());
    ASSERT_STREQ("a string", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_STRING, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_null_invalid)
{
    ASSERT_EQ(NULL, nix_get_typename(ctx, value));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_value_set_get_null)
{
    nix_init_null(ctx, value);

    ASSERT_STREQ("null", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_NULL, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_path_invalid)
{
    ASSERT_EQ(nullptr, nix_get_path_string(ctx, nullptr));
    assert_ctx_err();
    ASSERT_EQ(nullptr, nix_get_path_string(ctx, value));
    assert_ctx_err();
}
TEST_F(nix_api_expr_test, nix_value_set_get_path)
{
    const char * p = "/nix/store/40s0qmrfb45vlh6610rk29ym318dswdr-myname";
    nix_init_path_string(ctx, state, value, p);

    ASSERT_STREQ(p, nix_get_path_string(ctx, value));
    ASSERT_STREQ("a path", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_PATH, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_build_and_init_list_invalid)
{
    ASSERT_EQ(nullptr, nix_get_list_byidx(ctx, nullptr, state, 0));
    assert_ctx_err();
    ASSERT_EQ(0, nix_get_list_size(ctx, nullptr));
    assert_ctx_err();

    ASSERT_EQ(nullptr, nix_get_list_byidx(ctx, value, state, 0));
    assert_ctx_err();
    ASSERT_EQ(0, nix_get_list_size(ctx, value));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_build_and_init_list)
{
    int size = 10;
    ListBuilder * builder = nix_make_list_builder(ctx, state, size);

    nix_value * intValue = nix_alloc_value(ctx, state);
    nix_value * intValue2 = nix_alloc_value(ctx, state);

    // `init` and `insert` can be called in any order
    nix_init_int(ctx, intValue, 42);
    nix_list_builder_insert(ctx, builder, 0, intValue);
    nix_list_builder_insert(ctx, builder, 1, intValue2);
    nix_init_int(ctx, intValue2, 43);

    nix_make_list(ctx, builder, value);
    nix_list_builder_free(builder);

    ASSERT_EQ(42, nix_get_int(ctx, nix_get_list_byidx(ctx, value, state, 0)));
    ASSERT_EQ(43, nix_get_int(ctx, nix_get_list_byidx(ctx, value, state, 1)));
    ASSERT_EQ(nullptr, nix_get_list_byidx(ctx, value, state, 2));
    ASSERT_EQ(10, nix_get_list_size(ctx, value));

    ASSERT_STREQ("a list", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_LIST, nix_get_type(ctx, value));

    // Clean up
    nix_gc_decref(ctx, intValue);
}

TEST_F(nix_api_expr_test, nix_build_and_init_attr_invalid)
{
    ASSERT_EQ(nullptr, nix_get_attr_byname(ctx, nullptr, state, 0));
    assert_ctx_err();
    ASSERT_EQ(nullptr, nix_get_attr_byidx(ctx, nullptr, state, 0, nullptr));
    assert_ctx_err();
    ASSERT_EQ(nullptr, nix_get_attr_name_byidx(ctx, nullptr, state, 0));
    assert_ctx_err();
    ASSERT_EQ(0, nix_get_attrs_size(ctx, nullptr));
    assert_ctx_err();
    ASSERT_EQ(false, nix_has_attr_byname(ctx, nullptr, state, "no-value"));
    assert_ctx_err();

    ASSERT_EQ(nullptr, nix_get_attr_byname(ctx, value, state, 0));
    assert_ctx_err();
    ASSERT_EQ(nullptr, nix_get_attr_byidx(ctx, value, state, 0, nullptr));
    assert_ctx_err();
    ASSERT_EQ(nullptr, nix_get_attr_name_byidx(ctx, value, state, 0));
    assert_ctx_err();
    ASSERT_EQ(0, nix_get_attrs_size(ctx, value));
    assert_ctx_err();
    ASSERT_EQ(false, nix_has_attr_byname(ctx, value, state, "no-value"));
    assert_ctx_err();
}

TEST_F(nix_api_expr_test, nix_build_and_init_attr)
{
    int size = 10;
    const char ** out_name = (const char **) malloc(sizeof(char *));

    BindingsBuilder * builder = nix_make_bindings_builder(ctx, state, size);

    nix_value * intValue = nix_alloc_value(ctx, state);
    nix_init_int(ctx, intValue, 42);

    nix_value * stringValue = nix_alloc_value(ctx, state);
    nix_init_string(ctx, stringValue, "foo");

    nix_bindings_builder_insert(ctx, builder, "a", intValue);
    nix_bindings_builder_insert(ctx, builder, "b", stringValue);
    nix_make_attrs(ctx, value, builder);
    nix_bindings_builder_free(builder);

    ASSERT_EQ(2, nix_get_attrs_size(ctx, value));

    nix_value * out_value = nix_get_attr_byname(ctx, value, state, "a");
    ASSERT_EQ(42, nix_get_int(ctx, out_value));
    nix_gc_decref(ctx, out_value);

    out_value = nix_get_attr_byidx(ctx, value, state, 0, out_name);
    ASSERT_EQ(42, nix_get_int(ctx, out_value));
    ASSERT_STREQ("a", *out_name);
    nix_gc_decref(ctx, out_value);

    ASSERT_STREQ("a", nix_get_attr_name_byidx(ctx, value, state, 0));

    ASSERT_EQ(true, nix_has_attr_byname(ctx, value, state, "b"));
    ASSERT_EQ(false, nix_has_attr_byname(ctx, value, state, "no-value"));

    out_value = nix_get_attr_byname(ctx, value, state, "b");
    std::string string_value;
    nix_get_string(ctx, out_value, OBSERVE_STRING(string_value));
    ASSERT_STREQ("foo", string_value.c_str());
    nix_gc_decref(nullptr, out_value);

    out_value = nix_get_attr_byidx(ctx, value, state, 1, out_name);
    nix_get_string(ctx, out_value, OBSERVE_STRING(string_value));
    ASSERT_STREQ("foo", string_value.c_str());
    ASSERT_STREQ("b", *out_name);
    nix_gc_decref(nullptr, out_value);

    ASSERT_STREQ("b", nix_get_attr_name_byidx(ctx, value, state, 1));

    ASSERT_STREQ("a set", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_ATTRS, nix_get_type(ctx, value));

    // Clean up
    nix_gc_decref(ctx, intValue);
    nix_gc_decref(ctx, stringValue);
    free(out_name);
}

TEST_F(nix_api_expr_test, nix_value_init)
{
    // Setup

    // two = 2;
    // f = a: a * a;

    nix_value * two = nix_alloc_value(ctx, state);
    nix_init_int(ctx, two, 2);

    nix_value * f = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(
        ctx,
        state,
        R"(
        a: a * a
    )",
        "<test>",
        f);

    // Test

    // r = f two;

    nix_value * r = nix_alloc_value(ctx, state);
    nix_init_apply(ctx, r, f, two);
    assert_ctx_ok();

    ValueType t = nix_get_type(ctx, r);
    assert_ctx_ok();

    ASSERT_EQ(t, NIX_TYPE_THUNK);

    nix_value_force(ctx, state, r);

    t = nix_get_type(ctx, r);
    assert_ctx_ok();

    ASSERT_EQ(t, NIX_TYPE_INT);

    int n = nix_get_int(ctx, r);
    assert_ctx_ok();

    ASSERT_EQ(n, 4);

    // Clean up
    nix_gc_decref(ctx, two);
    nix_gc_decref(ctx, f);
    nix_gc_decref(ctx, r);
}

TEST_F(nix_api_expr_test, nix_value_init_apply_error)
{
    nix_value * some_string = nix_alloc_value(ctx, state);
    nix_init_string(ctx, some_string, "some string");
    assert_ctx_ok();

    nix_value * v = nix_alloc_value(ctx, state);
    nix_init_apply(ctx, v, some_string, some_string);
    assert_ctx_ok();

    // All ok. Call has not been evaluated yet.

    // Evaluate it
    nix_value_force(ctx, state, v);
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_THAT(ctx->last_err.value(), testing::HasSubstr("attempt to call something which is not a function but"));

    // Clean up
    nix_gc_decref(ctx, some_string);
    nix_gc_decref(ctx, v);
}

TEST_F(nix_api_expr_test, nix_value_init_apply_lazy_arg)
{
    // f is a lazy function: it does not evaluate its argument before returning its return value
    // g is a helper to produce e
    // e is a thunk that throws an exception
    //
    // r = f e
    // r should not throw an exception, because e is not evaluated

    nix_value * f = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(
        ctx,
        state,
        R"(
        a: { foo = a; }
    )",
        "<test>",
        f);
    assert_ctx_ok();

    nix_value * e = nix_alloc_value(ctx, state);
    {
        nix_value * g = nix_alloc_value(ctx, state);
        nix_expr_eval_from_string(
            ctx,
            state,
            R"(
            _ignore: throw "error message for test case nix_value_init_apply_lazy_arg"
        )",
            "<test>",
            g);
        assert_ctx_ok();

        nix_init_apply(ctx, e, g, g);
        assert_ctx_ok();
        nix_gc_decref(ctx, g);
    }

    nix_value * r = nix_alloc_value(ctx, state);
    nix_init_apply(ctx, r, f, e);
    assert_ctx_ok();

    nix_value_force(ctx, state, r);
    assert_ctx_ok();

    auto n = nix_get_attrs_size(ctx, r);
    assert_ctx_ok();
    ASSERT_EQ(1, n);

    // nix_get_attr_byname isn't lazy (it could have been) so it will throw the exception
    nix_value * foo = nix_get_attr_byname(ctx, r, state, "foo");
    ASSERT_EQ(nullptr, foo);
    ASSERT_THAT(ctx->last_err.value(), testing::HasSubstr("error message for test case nix_value_init_apply_lazy_arg"));

    // Clean up
    nix_gc_decref(ctx, f);
    nix_gc_decref(ctx, e);
}

TEST_F(nix_api_expr_test, nix_copy_value)
{
    nix_value * source = nix_alloc_value(ctx, state);

    nix_init_int(ctx, source, 42);
    nix_copy_value(ctx, value, source);

    ASSERT_EQ(42, nix_get_int(ctx, value));

    // Clean up
    nix_gc_decref(ctx, source);
}

}
