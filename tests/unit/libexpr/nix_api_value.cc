#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_store.hh"

#include <gtest/gtest.h>

namespace nixC {

class nix_api_value_test : public nix_api_store_test
{
public:
    nix_api_value_test()
    {
        state = nix_state_create(nullptr, nullptr, store);
        value = nix_alloc_value(nullptr, state);
    }
    ~nix_api_value_test()
    {
        nix_gc_decref(nullptr, value);
        nix_state_free(state);
    }

    EvalState * state;
    Value * value;
};

TEST_F(nix_api_value_test, nix_value_set_get_int)
{
    int myInt = 1;
    nix_set_int(nullptr, value, myInt);

    ASSERT_EQ(myInt, nix_get_int(nullptr, value));
}

TEST_F(nix_api_value_test, nix_value_make_list)
{
    int size = 10;
    nix_make_list(nullptr, state, value, size);
    ASSERT_EQ(size, nix_get_list_size(nullptr, value));
}

TEST_F(nix_api_value_test, nix_value_set_get_list)
{
    int size = 10;
    nix_make_list(nullptr, state, value, size);

    Value * intValue = nix_alloc_value(nullptr, state);
    nix_set_int(nullptr, intValue, 42);
    nix_set_list_byidx(nullptr, value, 0, intValue);

    ASSERT_EQ(42, nix_get_int(nullptr, nix_get_list_byidx(nullptr, value, state, 0)));

    // Clean up
    nix_gc_decref(nullptr, intValue);
}
}
