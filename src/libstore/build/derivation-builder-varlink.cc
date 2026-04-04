#include "nix/store/build/derivation-builder.hh"
#include "nix/store/build/derivation-builder-varlink.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/path.hh"
#include "nix/util/serialise.hh"
#include "nix/util/error.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/file-descriptor.hh"

#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/un.h>

namespace nix {

using namespace derivation_builder_varlink;

/**
 * Receive a file descriptor from a Unix domain socket using SCM_RIGHTS.
 * This is used by Varlink to pass file descriptors alongside messages.
 */
static AutoCloseFD receiveFileDescriptor(int sockFd)
{
    // Buffer for the dummy byte that must be sent with the fd
    char buf[1];
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = sizeof(buf),
    };

    // Allocate space for control message
    union
    {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } controlMsg;

    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = controlMsg.buf,
        .msg_controllen = sizeof(controlMsg.buf),
        .msg_flags = 0,
    };

    ssize_t n = recvmsg(sockFd, &msg, 0);
    if (n < 0)
        throw SysError("receiving file descriptor");
    if (n == 0)
        throw Error("unexpected EOF while receiving file descriptor");

    // Extract the file descriptor from control message
    struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS)
        throw Error("no file descriptor received in control message");

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

    return AutoCloseFD(fd);
}

/**
 * Process Varlink protocol messages for the derivation builder interface.
 * This implements the org.nix.derivation-builder Varlink interface defined in
 * doc/manual/source/protocols/derivation-builder/derivation-builder.varlink
 */
void processVarlinkConnection(Store & store, FdSource & from, FdSink & to)
{
    using json = nlohmann::json;

    auto sendResponse = [&](const Response & response) {
        json j;
        nlohmann::adl_serializer<Response>::to_json(j, response);
        auto responseStr = j.dump();
        to << responseStr << "\n";
        to.flush();
    };

    while (true) {
        std::string line;
        try {
            line = readLine(from.fd);
        } catch (EndOfFile &) {
            break;
        }

        if (line.empty())
            continue;

        json requestJson;
        try {
            requestJson = json::parse(line);
        } catch (json::parse_error & e) {
            throw Error("Invalid JSON in Varlink request: %s", e.what());
        }

        // Parse the request using the typed Request structure
        Request request = nlohmann::adl_serializer<Request>::from_json(requestJson);

        // Handle the request based on its type
        std::visit(
            overloaded{
                [&](const Request::AddToStore & req) {
                    // Receive file descriptor from client via SCM_RIGHTS
                    // The client sends the file descriptor containing the NAR archive.
                    //
                    // FIXME no the file descriptor should be to the
                    // file/directory itself, and we use the yet-to-be-written
                    // source accessor to add to the store. No NAR format for
                    // this!
                    AutoCloseFD narFd = receiveFileDescriptor(from.fd);

                    // Read from the received file descriptor
                    FdSource narSource(narFd.get());
                    auto path = store.addToStoreFromDump(
                        narSource,
                        req.name,
                        FileSerialisationMethod::NixArchive,
                        req.method,
                        HashAlgorithm::SHA256,
                        {});

                    sendResponse(Response{Response::AddToStore{.path = path}});
                },
                [&](const Request::AddDerivation & req) {
                    // Write the derivation to the store
                    auto drvPath = store.writeDerivation(req.derivation);

                    sendResponse(Response{Response::AddDerivation{.path = drvPath}});
                },
                [&](const Request::SubmitOutput & req) {
                    // Register this as a build output
                    // Note: The actual output registration happens in registerOutputs()
                    // This method is primarily for the builder to signal completion of an output
                    // The store path is already tracked by the RestrictedStore
                    // Authorization is handled automatically by the RestrictedStore wrapper

                    sendResponse(Response{Response::SubmitOutput{}});
                }},
            request.raw);
    }
}

} // namespace nix

// JSON serialization implementations
namespace nlohmann {

using namespace nix;
using namespace nix::derivation_builder_varlink;
using json = nlohmann::json;

Request::AddToStore adl_serializer<Request::AddToStore>::from_json(const json & j)
{
    return Request::AddToStore{
        .name = j.at("name").get<std::string>(),
        .method = ContentAddressMethod::parse(j.at("method").get<std::string>()),
    };
}

void adl_serializer<Request::AddToStore>::to_json(json & j, const Request::AddToStore & req)
{
    j = json{
        {"name", req.name},
        {"method", req.method.render()},
    };
}

Response::AddToStore adl_serializer<Response::AddToStore>::from_json(const json & j)
{
    return Response::AddToStore{
        .path = adl_serializer<StorePath>::from_json(j.at("path")),
    };
}

void adl_serializer<Response::AddToStore>::to_json(json & j, const Response::AddToStore & resp)
{
    j = json{
        {"path", resp.path.to_string()},
    };
}

Request::AddDerivation adl_serializer<Request::AddDerivation>::from_json(const json & j)
{
    return Request::AddDerivation{
        .derivation = adl_serializer<Derivation>::from_json(j.at("derivation"), experimentalFeatureSettings),
    };
}

void adl_serializer<Request::AddDerivation>::to_json(json & j, const Request::AddDerivation & req)
{
    j = json{};
    adl_serializer<Derivation>::to_json(j["derivation"], req.derivation);
}

Response::AddDerivation adl_serializer<Response::AddDerivation>::from_json(const json & j)
{
    return Response::AddDerivation{
        .path = adl_serializer<StorePath>::from_json(j.at("path")),
    };
}

void adl_serializer<Response::AddDerivation>::to_json(json & j, const Response::AddDerivation & resp)
{
    j = json{
        {"path", resp.path.to_string()},
    };
}

Request::SubmitOutput adl_serializer<Request::SubmitOutput>::from_json(const json & j)
{
    return Request::SubmitOutput{
        .name = j.at("name").get<std::string>(),
        .path = adl_serializer<StorePath>::from_json(j.at("path")),
    };
}

void adl_serializer<Request::SubmitOutput>::to_json(json & j, const Request::SubmitOutput & req)
{
    j = json{
        {"name", req.name},
        {"path", req.path.to_string()},
    };
}

Response::SubmitOutput adl_serializer<Response::SubmitOutput>::from_json(const json & j)
{
    return Response::SubmitOutput{};
}

void adl_serializer<Response::SubmitOutput>::to_json(json & j, const Response::SubmitOutput & resp)
{
    j = json::object();
}

Request adl_serializer<Request>::from_json(const json & j)
{
    std::string method = j.at("method").get<std::string>();
    json params = j.value("parameters", json::object());

    if (method == "org.nix.derivation-builder.AddToStore") {
        return Request{adl_serializer<Request::AddToStore>::from_json(params)};
    } else if (method == "org.nix.derivation-builder.AddDerivation") {
        return Request{adl_serializer<Request::AddDerivation>::from_json(params)};
    } else if (method == "org.nix.derivation-builder.SubmitOutput") {
        return Request{adl_serializer<Request::SubmitOutput>::from_json(params)};
    } else {
        throw Error("Unknown Varlink method: %s", method);
    }
}

void adl_serializer<Request>::to_json(json & j, const Request & req)
{
    std::visit(
        overloaded{
            [&](const Request::AddToStore & r) {
                j["method"] = "org.nix.derivation-builder.AddToStore";
                adl_serializer<Request::AddToStore>::to_json(j["parameters"], r);
            },
            [&](const Request::AddDerivation & r) {
                j["method"] = "org.nix.derivation-builder.AddDerivation";
                adl_serializer<Request::AddDerivation>::to_json(j["parameters"], r);
            },
            [&](const Request::SubmitOutput & r) {
                j["method"] = "org.nix.derivation-builder.SubmitOutput";
                adl_serializer<Request::SubmitOutput>::to_json(j["parameters"], r);
            },
        },
        req.raw);
}

Response adl_serializer<Response>::from_json(const json & j)
{
    json params = j.value("parameters", json::object());

    // Response type is determined by which fields are present
    if (params.contains("path") && !params.contains("name")) {
        // Could be Response::AddToStore or Response::AddDerivation
        // We can't distinguish them from JSON alone, so we'll need context
        // For now, just return one type
        return Response{adl_serializer<Response::AddToStore>::from_json(params)};
    } else {
        return Response{Response::SubmitOutput{}};
    }
}

void adl_serializer<Response>::to_json(json & j, const Response & resp)
{
    j = json::object();
    std::visit(
        overloaded{
            [&](const Response::AddToStore & r) { adl_serializer<Response::AddToStore>::to_json(j["parameters"], r); },
            [&](const Response::AddDerivation & r) {
                adl_serializer<Response::AddDerivation>::to_json(j["parameters"], r);
            },
            [&](const Response::SubmitOutput & r) {
                adl_serializer<Response::SubmitOutput>::to_json(j["parameters"], r);
            },
        },
        resp.raw);
}

} // namespace nlohmann
