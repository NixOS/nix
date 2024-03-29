#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_expr.hh"

#include <cstdlib>
#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_expr_test, nix_value_set_get_int)
{
    ASSERT_EQ(0, nix_get_int(ctx, nullptr));
    ASSERT_DEATH(nix_get_int(ctx, value), "");

    int myInt = 1;
    nix_init_int(ctx, value, myInt);

    ASSERT_EQ(myInt, nix_get_int(ctx, value));
    ASSERT_STREQ("an integer", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_INT, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_float)
{
    ASSERT_FLOAT_EQ(0.0, nix_get_float(ctx, nullptr));
    ASSERT_DEATH(nix_get_float(ctx, value), "");

    float myDouble = 1.0;
    nix_init_float(ctx, value, myDouble);

    ASSERT_FLOAT_EQ(myDouble, nix_get_float(ctx, value));
    ASSERT_STREQ("a float", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_FLOAT, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_bool)
{
    ASSERT_EQ(false, nix_get_bool(ctx, nullptr));
    ASSERT_DEATH(nix_get_bool(ctx, value), "");

    bool myBool = true;
    nix_init_bool(ctx, value, myBool);

    ASSERT_EQ(myBool, nix_get_bool(ctx, value));
    ASSERT_STREQ("a Boolean", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_BOOL, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_string)
{
    ASSERT_EQ(nullptr, nix_get_string(ctx, nullptr));
    ASSERT_DEATH(nix_get_string(ctx, value), "");

    const char * myString = "some string";
    nix_init_string(ctx, value, myString);

    ASSERT_STREQ(myString, nix_get_string(ctx, value));
    ASSERT_STREQ("a string", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_STRING, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_null)
{
    ASSERT_DEATH(nix_get_typename(ctx, value), "");

    nix_init_null(ctx, value);

    ASSERT_STREQ("null", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_NULL, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_path)
{
    ASSERT_EQ(nullptr, nix_get_path_string(ctx, nullptr));
    ASSERT_DEATH(nix_get_path_string(ctx, value), "");

    const char * p = "/nix/store/40s0qmrfb45vlh6610rk29ym318dswdr-myname";
    nix_init_path_string(ctx, state, value, p);

    ASSERT_STREQ(p, nix_get_path_string(ctx, value));
    ASSERT_STREQ("a path", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_PATH, nix_get_type(ctx, value));
}

TEST_F(nix_api_expr_test, nix_build_and_init_list)
{
    ASSERT_EQ(nullptr, nix_get_list_byidx(ctx, nullptr, state, 0));
    ASSERT_EQ(0, nix_get_list_size(ctx, nullptr));

    ASSERT_DEATH(nix_get_list_byidx(ctx, value, state, 0), "");
    ASSERT_DEATH(nix_get_list_size(ctx, value), "");

    int size = 10;
    ListBuilder * builder = nix_make_list_builder(ctx, state, size);

    Value * intValue = nix_alloc_value(ctx, state);
    nix_init_int(ctx, intValue, 42);
    nix_list_builder_insert(ctx, builder, 0, intValue);
    nix_make_list(ctx, builder, value);
    nix_list_builder_free(builder);

    ASSERT_EQ(42, nix_get_int(ctx, nix_get_list_byidx(ctx, value, state, 0)));
    ASSERT_EQ(nullptr, nix_get_list_byidx(ctx, value, state, 1));
    ASSERT_EQ(10, nix_get_list_size(ctx, value));

    ASSERT_STREQ("a list", nix_get_typename(ctx, value));
    ASSERT_EQ(NIX_TYPE_LIST, nix_get_type(ctx, value));

    // Clean up
    nix_gc_decref(ctx, intValue);
}

TEST_F(nix_api_expr_test, nix_build_and_init_attr)
{
    ASSERT_EQ(nullptr, nix_get_attr_byname(ctx, nullptr, state, 0));
    ASSERT_EQ(nullptr, nix_get_attr_byidx(ctx, nullptr, state, 0, nullptr));
    ASSERT_EQ(nullptr, nix_get_attr_name_byidx(ctx, nullptr, state, 0));
    ASSERT_EQ(0, nix_get_attrs_size(ctx, nullptr));
    ASSERT_EQ(false, nix_has_attr_byname(ctx, nullptr, state, "no-value"));

    ASSERT_DEATH(nix_get_attr_byname(ctx, value, state, 0), "");
    ASSERT_DEATH(nix_get_attr_byidx(ctx, value, state, 0, nullptr), "");
    ASSERT_DEATH(nix_get_attr_name_byidx(ctx, value, state, 0), "");
    ASSERT_DEATH(nix_get_attrs_size(ctx, value), "");
    ASSERT_DEATH(nix_has_attr_byname(ctx, value, state, "no-value"), "");

    int size = 10;
    const char ** out_name = (const char **) malloc(sizeof(char *));

    BindingsBuilder * builder = nix_make_bindings_builder(ctx, state, size);

    Value * intValue = nix_alloc_value(ctx, state);
    nix_init_int(ctx, intValue, 42);

    Value * stringValue = nix_alloc_value(ctx, state);
    nix_init_string(ctx, stringValue, "foo");

    nix_bindings_builder_insert(ctx, builder, "a", intValue);
    nix_bindings_builder_insert(ctx, builder, "b", stringValue);
    nix_make_attrs(ctx, value, builder);
    nix_bindings_builder_free(builder);

    ASSERT_EQ(2, nix_get_attrs_size(ctx, value));

    Value * out_value = nix_get_attr_byname(ctx, value, state, "a");
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
    ASSERT_STREQ("foo", nix_get_string(ctx, out_value));
    nix_gc_decref(nullptr, out_value);

    out_value = nix_get_attr_byidx(ctx, value, state, 1, out_name);
    ASSERT_STREQ("foo", nix_get_string(ctx, out_value));
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

}
