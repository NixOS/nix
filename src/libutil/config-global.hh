#pragma once
///@file

#include "config.hh"

namespace nix {

struct GlobalConfig : public AbstractConfig
{
    typedef std::vector<Config *> ConfigRegistrations;
    static ConfigRegistrations * configRegistrations;

    bool set(const std::string & name, const std::string & value) override;

    void getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly = false) override;

    void resetOverridden() override;

    nlohmann::json toJSON() override;

    std::string toKeyValue() override;

    void convertToArgs(Args & args, const std::string & category) override;

    struct Register
    {
        Register(Config * config);
    };
};

extern GlobalConfig globalConfig;

}
