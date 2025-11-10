#include "nix/util/users.hh"
#include "nix/store/globals.hh"
#include "nix/store/profiles.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"

namespace nix {

/* Very hacky way to parse $NIX_PATH, which is colon-separated, but
   can contain URLs (e.g. "nixpkgs=https://bla...:foo=https://"). */
Strings EvalSettings::parseNixPath(const std::string & s)
{
    Strings res;

    auto p = s.begin();

    while (p != s.end()) {
        auto start = p;
        auto start2 = p;

        while (p != s.end() && *p != ':') {
            if (*p == '=')
                start2 = p + 1;
            ++p;
        }

        if (p == s.end()) {
            if (p != start)
                res.push_back(std::string(start, p));
            break;
        }

        if (*p == ':') {
            auto prefix = std::string(start2, s.end());
            if (EvalSettings::isPseudoUrl(prefix) || hasPrefix(prefix, "flake:")) {
                ++p;
                while (p != s.end() && *p != ':')
                    ++p;
            }
            res.push_back(std::string(start, p));
            if (p == s.end())
                break;
        }

        ++p;
    }

    return res;
}

EvalSettings::EvalSettings(bool & readOnlyMode, EvalSettings::LookupPathHooks lookupPathHooks)
    : readOnlyMode{readOnlyMode}
    , lookupPathHooks{lookupPathHooks}
{
    auto var = getEnv("NIX_ABORT_ON_WARN");
    if (var && (var == "1" || var == "yes" || var == "true"))
        builtinsAbortOnWarn = true;
}

Strings EvalSettings::getDefaultNixPath()
{
    Strings res;
    auto add = [&](const Path & p, const std::string & s = std::string()) {
        if (std::filesystem::exists(p)) {
            if (s.empty()) {
                res.push_back(p);
            } else {
                res.push_back(s + "=" + p);
            }
        }
    };

    add(getNixDefExpr() + "/channels");
    add(rootChannelsDir() + "/nixpkgs", "nixpkgs");
    add(rootChannelsDir());

    return res;
}

bool EvalSettings::isPseudoUrl(std::string_view s)
{
    if (s.compare(0, 8, "channel:") == 0)
        return true;
    size_t pos = s.find("://");
    if (pos == std::string::npos)
        return false;
    std::string scheme(s, 0, pos);
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "channel" || scheme == "git"
           || scheme == "s3" || scheme == "ssh";
}

std::string EvalSettings::resolvePseudoUrl(std::string_view url)
{
    if (hasPrefix(url, "channel:"))
        return "https://channels.nixos.org/" + std::string(url.substr(8)) + "/nixexprs.tar.xz";
    else
        return std::string(url);
}

const std::string & EvalSettings::getCurrentSystem() const
{
    const auto & evalSystem = currentSystem.get();
    return evalSystem != "" ? evalSystem : settings.thisSystem.get();
}

Path getNixDefExpr()
{
    return settings.useXDGBaseDirectories ? getStateDir() + "/defexpr" : getHome() + "/.nix-defexpr";
}

} // namespace nix