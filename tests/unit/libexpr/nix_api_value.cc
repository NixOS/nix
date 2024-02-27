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
    int myInt = 1;
    nix_init_int(nullptr, value, myInt);

    ASSERT_EQ(myInt, nix_get_int(nullptr, value));
    ASSERT_STREQ("an integer", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_INT, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_float)
{
    float myDouble = 1.0;
    nix_init_float(nullptr, value, myDouble);

    ASSERT_EQ(myDouble, nix_get_float(nullptr, value));
    ASSERT_STREQ("a float", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_FLOAT, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_bool)
{
    bool myBool = true;
    nix_init_bool(nullptr, value, myBool);

    ASSERT_EQ(myBool, nix_get_bool(nullptr, value));
    ASSERT_STREQ("a Boolean", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_BOOL, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_string)
{
    const char * myString = "some string";
    nix_init_string(nullptr, value, myString);

    ASSERT_STREQ(myString, nix_get_string(nullptr, value));
    ASSERT_STREQ("a string", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_STRING, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_null)
{
    nix_init_null(nullptr, value);

    ASSERT_STREQ("null", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_NULL, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_value_set_get_path)
{
    const char * p = "/nix/store/40s0qmrfb45vlh6610rk29ym318dswdr-myname";
    nix_init_path_string(nullptr, state, value, p);

    ASSERT_STREQ(p, nix_get_path_string(nullptr, value));
    ASSERT_STREQ("a path", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_PATH, nix_get_type(nullptr, value));
}

TEST_F(nix_api_expr_test, nix_build_and_init_list)
{
    int size = 10;
    ListBuilder * builder = nix_make_list_builder(nullptr, state, size);

    Value * intValue = nix_alloc_value(nullptr, state);
    nix_init_int(nullptr, intValue, 42);
    nix_list_builder_insert(nullptr, builder, intValue);
    nix_make_list(nullptr, state, builder, value);
    nix_list_builder_free(builder);

    ASSERT_EQ(42, nix_get_int(nullptr, nix_get_list_byidx(nullptr, value, state, 0)));
    ASSERT_EQ(1, nix_get_list_size(nullptr, value));

    ASSERT_STREQ("a list", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_LIST, nix_get_type(nullptr, value));

    // Clean up
    nix_gc_decref(nullptr, intValue);
}

TEST_F(nix_api_expr_test, nix_build_and_init_attr)
{
    int size = 10;
    const char ** out_name = (const char **) malloc(sizeof(char *));

    BindingsBuilder * builder = nix_make_bindings_builder(nullptr, state, size);

    Value * intValue = nix_alloc_value(nullptr, state);
    nix_init_int(nullptr, intValue, 42);

    Value * stringValue = nix_alloc_value(nullptr, state);
    nix_init_string(nullptr, stringValue, "foo");

    nix_bindings_builder_insert(nullptr, builder, "a", intValue);
    nix_bindings_builder_insert(nullptr, builder, "b", stringValue);
    nix_make_attrs(nullptr, value, builder);
    nix_bindings_builder_free(builder);

    ASSERT_EQ(2, nix_get_attrs_size(nullptr, value));

    Value * out_value = nix_get_attr_byname(nullptr, value, state, "a");
    ASSERT_EQ(42, nix_get_int(nullptr, out_value));
    nix_gc_decref(nullptr, out_value);

    out_value = nix_get_attr_byidx(nullptr, value, state, 0, out_name);
    ASSERT_EQ(42, nix_get_int(nullptr, out_value));
    ASSERT_STREQ("a", *out_name);
    nix_gc_decref(nullptr, out_value);

    ASSERT_STREQ("a", nix_get_attr_name_byidx(nullptr, value, state, 0));

    out_value = nix_get_attr_byname(nullptr, value, state, "b");
    ASSERT_STREQ("foo", nix_get_string(nullptr, out_value));
    nix_gc_decref(nullptr, out_value);

    out_value = nix_get_attr_byidx(nullptr, value, state, 1, out_name);
    ASSERT_STREQ("foo", nix_get_string(nullptr, out_value));
    ASSERT_STREQ("b", *out_name);
    nix_gc_decref(nullptr, out_value);

    ASSERT_STREQ("b", nix_get_attr_name_byidx(nullptr, value, state, 1));

    ASSERT_STREQ("a set", nix_get_typename(nullptr, value));
    ASSERT_EQ(NIX_TYPE_ATTRS, nix_get_type(nullptr, value));

    // Clean up
    nix_gc_decref(nullptr, intValue);
    nix_gc_decref(nullptr, stringValue);
    free(out_name);
}

}
