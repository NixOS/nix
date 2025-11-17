#include "nix/store/derivations.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/source-path.hh"
#include "nix/util/unix-domain-socket.hh"
#include <nlohmann/json.hpp>
#include <sys/socket.h>

int main(int argc, char ** argv)
{
    if (argc != 2)
        return 1;

    auto derivation = R"(
    {
        "version": 4,
        "name": "varlink-dynamic",
        "outputs": {
            "out": {
                "hashAlgo": "sha256",
                "method": "nar"
            }
        },
        "inputs": {
            "drvs": {},
            "srcs": []
        },
        "builder": "/bin/sh",
        "args": [
            "-c",
            "echo foo > $out"
        ],
        "env": {}
    }
    )"_json;

    derivation["system"] = argv[1];
    derivation["env"]["out"] = nix::hashPlaceholder("out");

    nlohmann::json addRequest = {
        {"method", "org.nix.derivation-builder.AddDerivation"},
        {"parameters",
         {
             {"derivation", derivation},
         }},

    };

    auto remote = nix::getEnv("NIX_VARLINK_REMOTE");
    if (!remote.has_value())
        return 1;
    auto varlinkSocket = nix::connect(remote.value());

    auto addRequestStr = addRequest.dump();

    nix::write(varlinkSocket.get(), {(const std::byte *) addRequestStr.c_str(), addRequestStr.size() + 1}, false);

    auto responseLine = nix::readLine(varlinkSocket.get(), false, '\0');
    auto response = nlohmann::json::parse(responseLine);

    std::string path = response["parameters"]["path"];

    nlohmann::json submitOutputRequest = {
        {"method", "org.nix.derivation-builder.SubmitOutput"},
        {"parameters",
         {
             {"name", "out"},
             {"path", path},
         }},
    };

    {
        auto requestStr = submitOutputRequest.dump();
        // Add 1 byte to length to include the trailing null
        nix::write(varlinkSocket.get(), {(std::byte *) requestStr.c_str(), requestStr.length() + 1}, true);
        // Prevents a warning in the server
        nix::readLine(varlinkSocket.get(), false, '\0');
    }

    return 0;
}
