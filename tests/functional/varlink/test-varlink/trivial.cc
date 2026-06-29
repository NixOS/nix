#include "nix/util/environment-variables.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/source-path.hh"
#include "nix/util/unix-domain-socket.hh"
#include <nlohmann/json.hpp>
#include <sys/socket.h>

static const std::string addToStoreRequest = R"(
{
    "method": "org.nix.derivation-builder.AddToStore",
    "parameters": {
        "name": "example-out",
        "method": "nar",
        "descriptor": 0
    }
}
)";

int main(int argc, char ** argv)
{
    if (argc != 2)
        return 1;

    auto accessor = nix::makeFSSourceAccessor(std::filesystem::absolute(argv[1]));
    nix::SourcePath src(accessor);

    auto remote = nix::getEnv("NIX_VARLINK_REMOTE");
    if (!remote.has_value())
        return 1;
    auto varlinkSocket = nix::connect(remote.value());

    int narSockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, narSockets) != 0)
        return 1;
    nix::AutoCloseFD narSender(narSockets[0]);
    std::vector<nix::Descriptor> fdsToSend{narSockets[1]};

    nix::unix::sendMessageWithFds(
        varlinkSocket.get(), {(const std::byte *) addToStoreRequest.c_str(), addToStoreRequest.size() + 1}, fdsToSend);

    {
        nix::FdSink narSink(narSender.get());

        src.dumpPath(narSink);

        narSink.flush();
        narSender.close();
    }
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
