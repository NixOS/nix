#include "nix/store/derivations.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/unix-domain-socket.hh"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

static nlohmann::json callMethod(int fd, const std::string & method, const nlohmann::json & parameters)
{
    nlohmann::json j = {{"method", method}, {"parameters", parameters}};
    auto s = j.dump();
    nix::write(fd, {(const std::byte *) s.c_str(), s.size() + 1}, false);

    auto resp = nix::readLine(fd, false, '\0');
    return nlohmann::json::parse(resp)["parameters"];
}

int main(int argc, char ** argv)
{
    if (argc != 4)
        return 1;

    std::string system = argv[1];
    std::string shell = argv[2];
    std::string path = argv[3];

    auto placeholder = nix::hashPlaceholder("out");

    auto makeDrv = [&](const std::string & name, const std::vector<std::string> & deps) -> nlohmann::json {
        nlohmann::json drvDeps = nlohmann::json::object();
        for (const auto & dep : deps)
            drvDeps[dep] = {{"outputs", {"out"}}, {"dynamicOutputs", nlohmann::json::object()}};

        return {
            {"version", 4},
            {"name", "build-" + name},
            {"outputs", {{"out", {{"hashAlgo", "sha256"}, {"method", "nar"}}}}},
            {"inputs", {{"drvs", drvDeps}, {"srcs", nlohmann::json::array()}}},
            {"system", system},
            {"builder", shell},
            {"args", {"-c", "set -eu; echo \"word env var " + name + " is $" + name + "\" >> \"$out\""}},
            {"env", {{"out", placeholder}, {name, "hello, from " + name + "!"}, {"PATH", path}}}};
    };

    auto remote = nix::getEnv("NIX_VARLINK_REMOTE");
    if (!remote.has_value())
        return 1;
    auto fd = nix::connect(remote.value());

    auto addDerivation = [&](const nlohmann::json & drv) -> std::string {
        auto resp = callMethod(fd.get(), "org.nix.derivation-builder.AddDerivation", {{"derivation", drv}});
        return resp["path"].get<std::string>();
    };

    auto a = addDerivation(makeDrv("a", {}));
    auto b = addDerivation(makeDrv("b", {a}));
    auto c = addDerivation(makeDrv("c", {a}));
    auto d = addDerivation(makeDrv("d", {b, c}));
    auto e = addDerivation(makeDrv("e", {b, c, d}));

    // Submit e's drv path as our sole output, leaving much of it free
    callMethod(fd.get(), "org.nix.derivation-builder.SubmitOutput", {{"name", "out"}, {"path", e}});

    return 0;
}
