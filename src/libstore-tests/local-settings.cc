#include <gtest/gtest.h>

#include "nix/store/local-settings.hh"

// Needed for template specialisations, as in local-store.cc.
#include "nix/util/args.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * SandboxNetworkMode
 *
 * The enum and its parse/to_string are platform-neutral even though only the
 * Windows builder acts on the value, so these run everywhere. That is
 * deliberate: it is the Linux CI that will catch a fourth mode being added
 * without teaching `to_string` about it.
 * --------------------------------------------------------------------------*/

TEST(SandboxNetworkMode, defaultsToNone)
{
    Config config;
    Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};
    ASSERT_EQ(setting.get(), snmNone);
}

TEST(SandboxNetworkMode, everyModeRoundTrips)
{
    /* Every enumerator must survive string -> enum -> string. A mode that
       parses but has no `to_string` arm would otherwise only be caught by
       `unreachable()` at runtime. */
    for (auto & name : {"none", "wfp", "container"}) {
        Config config;
        Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};
        ASSERT_EQ(config.set("sandbox-network", name), true) << "failed to set " << name;
        EXPECT_EQ(setting.to_string(), name);
    }
}

TEST(SandboxNetworkMode, parsesEachName)
{
    Config config;
    Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};

    setting.set("none");
    EXPECT_EQ(setting.get(), snmNone);
    setting.set("wfp");
    EXPECT_EQ(setting.get(), snmWfp);
    setting.set("container");
    EXPECT_EQ(setting.get(), snmContainer);
}

TEST(SandboxNetworkMode, rejectsUnknownValue)
{
    Config config;
    Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};
    EXPECT_THROW(setting.set("bogus"), UsageError);
}

TEST(SandboxNetworkMode, rejectsBooleanSpelling)
{
    /* `sandbox` is boolean-ish, and someone will reasonably try the same
       spellings here. They must be rejected rather than silently meaning
       something, since "true" says nothing about *which* mechanism to use. */
    Config config;
    Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};
    EXPECT_THROW(setting.set("true"), UsageError);
    EXPECT_THROW(setting.set("false"), UsageError);
    EXPECT_THROW(setting.set("relaxed"), UsageError);
}

TEST(SandboxNetworkMode, reportsTheModeNameToConfigShow)
{
    /* What `nix config show` prints. This goes through the virtual
       `to_string()`, not through `NLOHMANN_JSON_SERIALIZE_ENUM` — that macro is
       expanded in globals.cc, so its `to_json` is not visible by ADL outside
       that translation unit and nlohmann would fall back to the underlying
       integer. Asserting on the enum's JSON conversion here would therefore be
       testing something the setting does not actually promise. */
    Config config;
    Setting<SandboxNetworkMode> setting{&config, snmNone, "sandbox-network", "description"};
    ASSERT_EQ(config.set("sandbox-network", "wfp"), true);

    std::map<std::string, Config::SettingInfo> settings;
    config.getSettings(settings, /* overriddenOnly = */ false);

    auto i = settings.find("sandbox-network");
    ASSERT_NE(i, settings.end());
    EXPECT_EQ(i->second.value, "wfp");
}

} // namespace nix
