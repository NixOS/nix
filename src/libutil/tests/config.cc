#include "json.hh"
#include "config.hh"
#include "args.hh"

#include <sstream>
#include <gtest/gtest.h>

namespace nix {

    /* ----------------------------------------------------------------------------
     * Config
     * --------------------------------------------------------------------------*/

    TEST(Config, setUndefinedSetting) {
        Config config;
        ASSERT_EQ(config.set("undefined-key", "value"), false);
    }

    TEST(Config, setDefinedSetting) {
        Config config;
        std::string value;
        Setting<std::string> foo{&config, value, "name-of-the-setting", "description"};
        ASSERT_EQ(config.set("name-of-the-setting", "value"), true);
    }

    TEST(Config, getDefinedSetting) {
        Config config;
        std::string value;
        std::map<std::string, Config::SettingInfo> settings;
        Setting<std::string> foo{&config, value, "name-of-the-setting", "description"};

        config.getSettings(settings, /* overridenOnly = */ false);
        const auto iter = settings.find("name-of-the-setting");
        ASSERT_NE(iter, settings.end());
        ASSERT_EQ(iter->second.value, "");
        ASSERT_EQ(iter->second.description, "description");
    }

    TEST(Config, getDefinedOverridenSettingNotSet) {
        Config config;
        std::string value;
        std::map<std::string, Config::SettingInfo> settings;
        Setting<std::string> foo{&config, value, "name-of-the-setting", "description"};

        config.getSettings(settings, /* overridenOnly = */ true);
        const auto e = settings.find("name-of-the-setting");
        ASSERT_EQ(e, settings.end());
    }

    TEST(Config, getDefinedSettingSet1) {
        Config config;
        std::string value;
        std::map<std::string, Config::SettingInfo> settings;
        Setting<std::string> setting{&config, value, "name-of-the-setting", "description"};

        setting.assign("value");

        config.getSettings(settings, /* overridenOnly = */ false);
        const auto iter = settings.find("name-of-the-setting");
        ASSERT_NE(iter, settings.end());
        ASSERT_EQ(iter->second.value, "value");
        ASSERT_EQ(iter->second.description, "description");
    }

    TEST(Config, getDefinedSettingSet2) {
        Config config;
        std::map<std::string, Config::SettingInfo> settings;
        Setting<std::string> setting{&config, "", "name-of-the-setting", "description"};

        ASSERT_TRUE(config.set("name-of-the-setting", "value"));

        config.getSettings(settings, /* overridenOnly = */ false);
        const auto e = settings.find("name-of-the-setting");
        ASSERT_NE(e, settings.end());
        ASSERT_EQ(e->second.value, "value");
        ASSERT_EQ(e->second.description, "description");
    }

    TEST(Config, addSetting) {
        class TestSetting : public AbstractSetting {
            public:
            TestSetting() : AbstractSetting("test", "test", {}) {}
            void set(const std::string & value) {}
            std::string to_string() const { return {}; }
        };

        Config config;
        TestSetting setting;

        ASSERT_FALSE(config.set("test", "value"));
        config.addSetting(&setting);
        ASSERT_TRUE(config.set("test", "value"));
    }

    TEST(Config, withInitialValue) {
        const StringMap initials = {
            { "key", "value" },
        };
        Config config(initials);

        {
            std::map<std::string, Config::SettingInfo> settings;
            config.getSettings(settings, /* overridenOnly = */ false);
            ASSERT_EQ(settings.find("key"), settings.end());
        }

        Setting<std::string> setting{&config, "default-value", "key", "description"};

        {
            std::map<std::string, Config::SettingInfo> settings;
            config.getSettings(settings, /* overridenOnly = */ false);
            ASSERT_EQ(settings["key"].value, "value");
        }
    }

    TEST(Config, resetOverriden) {
        Config config;
        config.resetOverriden();
    }

    TEST(Config, resetOverridenWithSetting) {
        Config config;
        Setting<std::string> setting{&config, "", "name-of-the-setting", "description"};

        {
            std::map<std::string, Config::SettingInfo> settings;

            setting.set("foo");
            ASSERT_EQ(setting.get(), "foo");
            config.getSettings(settings, /* overridenOnly = */ true);
            ASSERT_TRUE(settings.empty());
        }

        {
            std::map<std::string, Config::SettingInfo> settings;

            setting.override("bar");
            ASSERT_TRUE(setting.overriden);
            ASSERT_EQ(setting.get(), "bar");
            config.getSettings(settings, /* overridenOnly = */ true);
            ASSERT_FALSE(settings.empty());
        }

        {
            std::map<std::string, Config::SettingInfo> settings;

            config.resetOverriden();
            ASSERT_FALSE(setting.overriden);
            config.getSettings(settings, /* overridenOnly = */ true);
            ASSERT_TRUE(settings.empty());
        }
    }

    TEST(Config, toJSONOnEmptyConfig) {
        std::stringstream out;
        { // Scoped to force the destructor of JSONObject to write the final `}`
            JSONObject obj(out);
            Config config;
            config.toJSON(obj);
        }

        ASSERT_EQ(out.str(), "{}");
    }

    TEST(Config, toJSONOnNonEmptyConfig) {
        std::stringstream out;
        { // Scoped to force the destructor of JSONObject to write the final `}`
            JSONObject obj(out);

            Config config;
            std::map<std::string, Config::SettingInfo> settings;
            Setting<std::string> setting{&config, "", "name-of-the-setting", "description"};
            setting.assign("value");

            config.toJSON(obj);
        }
        ASSERT_EQ(out.str(), R"#({"name-of-the-setting":{"description":"description","value":"value"}})#");
    }

    TEST(Config, setSettingAlias) {
        Config config;
        Setting<std::string> setting{&config, "", "some-int", "best number", { "another-int" }};
        ASSERT_TRUE(config.set("some-int", "1"));
        ASSERT_EQ(setting.get(), "1");
        ASSERT_TRUE(config.set("another-int", "2"));
        ASSERT_EQ(setting.get(), "2");
        ASSERT_TRUE(config.set("some-int", "3"));
        ASSERT_EQ(setting.get(), "3");
    }

    /* FIXME: The reapplyUnknownSettings method doesn't seem to do anything
     * useful (these days).  Whenever we add a new setting to Config the
     * unknown settings are always considered.  In which case is this function
     * actually useful? Is there some way to register a Setting without calling
     * addSetting? */
    TEST(Config, DISABLED_reapplyUnknownSettings) {
        Config config;
        ASSERT_FALSE(config.set("name-of-the-setting", "unknownvalue"));
        Setting<std::string> setting{&config, "default", "name-of-the-setting", "description"};
        ASSERT_EQ(setting.get(), "default");
        config.reapplyUnknownSettings();
        ASSERT_EQ(setting.get(), "unknownvalue");
    }
}
