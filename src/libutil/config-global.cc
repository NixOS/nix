#include "config-global.hh"

namespace nix {

bool GlobalConfig::set(const std::string & name, const std::string & value)
{
    for (auto & config : *configRegistrations)
        if (config->set(name, value))
            return true;

    unknownSettings.emplace(name, value);

    return false;
}

void GlobalConfig::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly)
{
    for (auto & config : *configRegistrations)
        config->getSettings(res, overriddenOnly);
}

void GlobalConfig::resetOverridden()
{
    for (auto & config : *configRegistrations)
        config->resetOverridden();
}

nlohmann::json GlobalConfig::toJSON()
{
    auto res = nlohmann::json::object();
    for (const auto & config : *configRegistrations)
        res.update(config->toJSON());
    return res;
}

std::string GlobalConfig::toKeyValue()
{
    std::string res;
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings);
    for (const auto & s : settings)
        res += fmt("%s = %s\n", s.first, s.second.value);
    return res;
}

void GlobalConfig::convertToArgs(Args & args, const std::string & category)
{
    for (auto & config : *configRegistrations)
        config->convertToArgs(args, category);
}

GlobalConfig globalConfig;

GlobalConfig::ConfigRegistrations * GlobalConfig::configRegistrations;

GlobalConfig::Register::Register(Config * config)
{
    if (!configRegistrations)
        configRegistrations = new ConfigRegistrations;
    configRegistrations->emplace_back(config);
}

ExperimentalFeatureSettings experimentalFeatureSettings;

static GlobalConfig::Register rSettings(&experimentalFeatureSettings);

}
