#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "tests/nix_api_util.hh"

#define STORE_DIR "/nix/store/"
#define HASH_PART "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q"
const char * validPath = STORE_DIR HASH_PART "-x";

namespace nixC {

class nix_api_store_test : public nix_api_util_context
{
public:
    void SetUp() override
    {
        nix_api_util_context::SetUp();
        nix_libstore_init(ctx);
        store = nix_store_open(ctx, "dummy://", NULL);
    };
    void TearDown() override
    {
        nix_store_unref(store);
        nix_api_util_context::TearDown();
    }

    Store * store;
};

TEST_F(nix_api_util_context, nix_libstore_init)
{
    auto ret = nix_libstore_init(ctx);
    ASSERT_EQ(NIX_OK, ret);
}

TEST_F(nix_api_store_test, nix_store_get_uri)
{
    char value[256];
    auto ret = nix_store_get_uri(ctx, store, value, 256);
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ("dummy", value);
}

TEST_F(nix_api_store_test, InvalidPathFails)
{
    nix_store_parse_path(ctx, store, "invalid-path");
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
}

TEST_F(nix_api_store_test, ReturnsValidStorePath)
{
    StorePath * result = nix_store_parse_path(ctx, store, validPath);
    ASSERT_NE(result, nullptr);
    ASSERT_STREQ("x", result->path.name().data());
    ASSERT_STREQ(HASH_PART "-x", result->path.to_string().data());
}

TEST_F(nix_api_store_test, SetsLastErrCodeToNixOk)
{
    nix_store_parse_path(ctx, store, validPath);
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
}

TEST_F(nix_api_store_test, DoesNotCrashWhenContextIsNull)
{
    ASSERT_NO_THROW(nix_store_parse_path(nullptr, store, validPath));
}

}
