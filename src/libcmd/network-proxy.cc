#include "network-proxy.hh"

#include <algorithm>
#include <boost/algorithm/string.hpp>

#include "environment-variables.hh"

namespace nix {

static const StringSet lowercaseVariables{"http_proxy", "https_proxy", "ftp_proxy", "all_proxy", "no_proxy"};

static StringSet getAllVariables()
{
    StringSet variables = lowercaseVariables;
    for (const auto & variable : lowercaseVariables) {
        variables.insert(boost::to_upper_copy(variable));
    }
    return variables;
}

const StringSet networkProxyVariables = getAllVariables();

static StringSet getExcludingNoProxyVariables()
{
    static const StringSet excludeVariables{"no_proxy", "NO_PROXY"};
    StringSet variables;
    std::set_difference(
        networkProxyVariables.begin(),
        networkProxyVariables.end(),
        excludeVariables.begin(),
        excludeVariables.end(),
        std::inserter(variables, variables.begin()));
    return variables;
}

static const StringSet excludingNoProxyVariables = getExcludingNoProxyVariables();

bool haveNetworkProxyConnection()
{
    for (const auto & variable : excludingNoProxyVariables) {
        if (getEnv(variable).has_value()) {
            return true;
        }
    }
    return false;
}

}
