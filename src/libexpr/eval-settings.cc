#include "globals.hh"
#include "profiles.hh"
#include "eval.hh"
#include "eval-settings.hh"

namespace nix {

Strings EvalSettings::getDefaultNixPath()
{
    Strings res;
    auto add = [&](const Path & p, const std::string & s = std::string()) {
        if (pathAccessible(p)) {
            if (s.empty()) {
                res.push_back(p);
            } else {
                res.push_back(s + "=" + p);
            }
        }
    };

    if (!evalSettings.restrictEval && !evalSettings.pureEval) {
        add(getNixDefExpr() + "/channels");
        add(rootChannelsDir() + "/nixpkgs", "nixpkgs");
        add(rootChannelsDir());
    }

    return res;
}

bool EvalSettings::isPseudoUrl(std::string_view s)
{
    if (s.compare(0, 8, "channel:") == 0) return true;
    size_t pos = s.find("://");
    if (pos == std::string::npos) return false;
    std::string scheme(s, 0, pos);
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "channel" || scheme == "git" || scheme == "s3" || scheme == "ssh";
}

std::string EvalSettings::resolvePseudoUrl(std::string_view url)
{
    if (hasPrefix(url, "channel:"))
        return "https://nixos.org/channels/" + std::string(url.substr(8)) + "/nixexprs.tar.xz";
    else
        return std::string(url);
}

EvalSettings evalSettings;

static GlobalConfig::Register rEvalSettings(&evalSettings);

Path getNixDefExpr()
{
    return settings.useXDGBaseDirectories
        ? getStateDir() + "/nix/defexpr"
        : getHome() + "/.nix-defexpr";
}

}
