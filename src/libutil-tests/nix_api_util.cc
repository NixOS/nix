#include "nix/util/config-global.hh"
#include "nix/util/args.hh"
#include "nix/util/finally.hh"
#include "nix/util/logging.hh"
#include "nix_api_util.h"
#include "nix/util/tests/nix_api_util.hh"
#include "nix/util/tests/string_callback.hh"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "util-tests-config.hh"

namespace nixC {

TEST(nix_api_util, nix_version_get)
{
    ASSERT_EQ(std::string(nix_version_get()), PACKAGE_VERSION);
}

struct MySettings : nix::Config
{
    nix::Setting<std::string> settingSet{this, "empty", "setting-name", "Description"};
};

MySettings mySettings;
static nix::GlobalConfig::Register rs(&mySettings);

TEST_F(nix_api_util_context, nix_setting_get)
{
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    std::string setting_value;
    nix_err result = nix_setting_get(ctx, "invalid-key", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_ERR_KEY);

    result = nix_setting_get(ctx, "setting-name", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_OK);
    ASSERT_STREQ("empty", setting_value.c_str());
}

TEST_F(nix_api_util_context, nix_setting_set)
{
    nix_err result = nix_setting_set(ctx, "invalid-key", "new-value");
    ASSERT_EQ(result, NIX_ERR_KEY);

    result = nix_setting_set(ctx, "setting-name", "new-value");
    ASSERT_EQ(result, NIX_OK);

    std::string setting_value;
    result = nix_setting_get(ctx, "setting-name", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_OK);
    ASSERT_STREQ("new-value", setting_value.c_str());
}

TEST_F(nix_api_util_context, nix_err_msg)
{
    // no error
    EXPECT_THROW(nix_err_msg(nullptr, ctx, NULL), nix::Error);

    // set error
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");

    // basic usage
    std::string err_msg = nix_err_msg(NULL, ctx, NULL);
    ASSERT_EQ(err_msg, "unknown test error");

    // advanced usage
    unsigned int sz;
    auto new_ctx = createOwnedNixContext();
    err_msg = nix_err_msg(new_ctx.get(), ctx, &sz);
    ASSERT_EQ(sz, err_msg.size());
}

TEST_F(nix_api_util_context, nix_err_code)
{
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_UNKNOWN);
}

struct LogCapture
{
    std::vector<std::pair<nix_verbosity, std::string>> logs;
    std::vector<std::tuple<nix_activity_id, nix_verbosity, nix_activity_type, std::string, nix_activity_id>> starts;
    std::vector<nix_activity_id> stops;
    std::vector<std::tuple<nix_activity_id, nix_result_type, std::string>> results;
    bool destroyed = false;
};

extern "C" void capture_log(void * userdata, nix_verbosity level, const char * msg)
{
    auto * c = static_cast<LogCapture *>(userdata);
    c->logs.emplace_back(level, msg);
}

extern "C" void capture_start_activity(
    void * userdata,
    nix_activity_id id,
    nix_verbosity level,
    nix_activity_type type,
    const char * s,
    nix_activity_id parent)
{
    auto * c = static_cast<LogCapture *>(userdata);
    c->starts.emplace_back(id, level, type, s, parent);
}

extern "C" void capture_stop_activity(void * userdata, nix_activity_id id)
{
    auto * c = static_cast<LogCapture *>(userdata);
    c->stops.push_back(id);
}

extern "C" void capture_result_string(void * userdata, nix_activity_id id, nix_result_type type, const char * msg)
{
    auto * c = static_cast<LogCapture *>(userdata);
    c->results.emplace_back(id, type, msg);
}

extern "C" void capture_destroy(void * userdata)
{
    auto * c = static_cast<LogCapture *>(userdata);
    c->destroyed = true;
}

const nix_logger captureLoggerVtable{
    .log = capture_log,
    .start_activity = capture_start_activity,
    .stop_activity = capture_stop_activity,
    .result_string = capture_result_string,
    .destroy = capture_destroy,
};

TEST_F(nix_api_util_context, nix_set_logger_rejects_null_vtable)
{
    ASSERT_EQ(nix_set_logger(ctx, nullptr, nullptr), NIX_ERR_UNKNOWN);
}

TEST_F(nix_api_util_context, nix_set_logger_routes_log_calls)
{
    LogCapture capture;
    // Restore the default logger before `capture` goes out of scope so
    // the destroy callback runs while `capture` is still alive.
    Finally restoreLogger([] { nix::logger = nix::makeSimpleLogger().release(); });

    ASSERT_EQ(nix_set_logger(ctx, &captureLoggerVtable, &capture), NIX_OK);

    nix::logger->log(nix::lvlInfo, "hello world");
    nix::logger->warn("be careful");

    ASSERT_EQ(capture.logs.size(), 2u);
    EXPECT_EQ(capture.logs[0].first, NIX_LVL_INFO);
    EXPECT_EQ(capture.logs[0].second, "hello world");
    EXPECT_EQ(capture.logs[1].first, NIX_LVL_WARN);
    // warn() prefixes "warning: " (with ANSI escapes).
    EXPECT_NE(capture.logs[1].second.find("be careful"), std::string::npos);
}

TEST_F(nix_api_util_context, nix_set_logger_routes_activities_and_results)
{
    LogCapture capture;
    Finally restoreLogger([] { nix::logger = nix::makeSimpleLogger().release(); });

    ASSERT_EQ(nix_set_logger(ctx, &captureLoggerVtable, &capture), NIX_OK);

    nix::ActivityId actId;
    {
        nix::Activity act(*nix::logger, nix::lvlInfo, nix::actBuild, "building foo");
        actId = act.id;
        nix::logger->result(act.id, nix::resBuildLogLine, nix::Logger::Fields{"line of build output"});
        // Integer-valued result; should be filtered out by result_string.
        act.progress(10, 100);
    }

    ASSERT_EQ(capture.starts.size(), 1u);
    EXPECT_EQ(std::get<0>(capture.starts[0]), actId);
    EXPECT_EQ(std::get<1>(capture.starts[0]), NIX_LVL_INFO);
    EXPECT_EQ(std::get<2>(capture.starts[0]), NIX_ACTIVITY_TYPE_BUILD);
    EXPECT_EQ(std::get<3>(capture.starts[0]), "building foo");

    ASSERT_EQ(capture.results.size(), 1u) << "result_string should only fire for string-valued results";
    EXPECT_EQ(std::get<0>(capture.results[0]), actId);
    EXPECT_EQ(std::get<1>(capture.results[0]), NIX_RESULT_TYPE_BUILD_LOG_LINE);
    EXPECT_EQ(std::get<2>(capture.results[0]), "line of build output");

    ASSERT_EQ(capture.stops.size(), 1u);
    EXPECT_EQ(capture.stops[0], actId);
}

TEST_F(nix_api_util_context, nix_set_logger_invokes_destroy_when_replaced)
{
    // Both captures must outlive the Finally so that the logger held
    // by `nix::logger` at teardown can call destroy on a live capture.
    LogCapture capture;
    LogCapture capture2;
    Finally restoreLogger([] { nix::logger = nix::makeSimpleLogger().release(); });

    ASSERT_EQ(nix_set_logger(ctx, &captureLoggerVtable, &capture), NIX_OK);
    EXPECT_FALSE(capture.destroyed);

    // Replacing the logger must fire destroy on the old userdata.
    ASSERT_EQ(nix_set_logger(ctx, &captureLoggerVtable, &capture2), NIX_OK);
    EXPECT_TRUE(capture.destroyed);
    EXPECT_FALSE(capture2.destroyed);
}

} // namespace nixC
