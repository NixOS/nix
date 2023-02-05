#include "store-command.hh"
#include "installable-derived-path.hh"

namespace nix {

struct ParseStoreInstallableCommmand : ParseInstallableArgs
{
    AbstractArgs & args;

    ParseStoreInstallableCommmand(AbstractArgs & args)
        : args(args)
    {
    }
    virtual ~ParseStoreInstallableCommmand() = default;

    Installables parseInstallables(
        ref<Store> store, std::vector<std::string> ss) override;

    ref<Installable> parseInstallable(
        ref<Store> store, const std::string & installable) override;

    void completeInstallable(AddCompletions & completions, std::string_view prefix) override;

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override;
};

Installables ParseStoreInstallableCommmand::parseInstallables(
    ref<Store> store, std::vector<std::string> ss)
{
    Installables result;

    for (auto & s : ss)
        result.push_back(parseInstallable(store, s));

    return result;
}

ref<Installable> ParseStoreInstallableCommmand::parseInstallable(
    ref<Store> store, const std::string & installable)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(installable);
    return make_ref<InstallableDerivedPath>(
        InstallableDerivedPath::parse(store, prefix, extendedOutputsSpec));
}

void ParseStoreInstallableCommmand::applyDefaultInstallables(std::vector<std::string> & rawInstallables)
{

}

void ParseStoreInstallableCommmand::completeInstallable(AddCompletions & completions, std::string_view prefix)
{

}

ParseInstallableArgs::RegisterDefault rParseStoreInstallableCommmand {

    [](GetRawInstallables & args) -> ref<ParseInstallableArgs>
    {
        return make_ref<ParseStoreInstallableCommmand>(args);
    }
};

}
