#include <gtest/gtest.h>
#include <string>

#include "nix_api_fetchers.h"
#include "nix/store/tests/nix_api_store.hh"
#include "nix/util/tests/nix_api_util.hh"

namespace nixC {

TEST_F(nix_api_store_test, nix_api_fetchers_new_free)
{
    nix_fetchers_settings * settings = nix_fetchers_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, settings);

    nix_fetchers_settings_free(settings);
}

} // namespace nixC
