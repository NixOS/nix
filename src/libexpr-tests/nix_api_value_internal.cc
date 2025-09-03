#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_expr_internal.h"

#include "nix/expr/tests/nix_api_expr.hh"
#include "nix/util/tests/string_callback.hh"

#include <gmock/gmock.h>
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

} // namespace nixC
