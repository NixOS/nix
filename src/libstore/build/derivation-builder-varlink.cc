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
#include "nix/util/unix-domain-socket.hh"

#include <deque>
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/un.h>

namespace nix {

using namespace derivation_builder_varlink;

/**
 * Process Varlink protocol messages for the derivation builder interface.
 * This implements the org.nix.derivation-builder Varlink interface defined in
 * doc/manual/source/protocols/derivation-builder/derivation-builder.varlink
 */
void processVarlinkConnection(
    Store & store, const StorePath & drvPath, ref<Sync<OutputPathMap>> _submittedOutputs, FdSource & from, FdSink & to)
{
    using json = nlohmann::json;

    auto sendData = [&](const json & data) {
        auto responseStr = data.dump();
        responseStr += '\0';
        writeFull(to.fd, responseStr);
    };

    auto sendResponse = [&](const Response & response) {
        json j;
        nlohmann::adl_serializer<Response>::to_json(j, response);
        sendData(j);
    };

    auto sendError = [&](const std::string & errorName) {
        sendData({
            {"error", errorName},
            {"parameters", json::object()},
        });
    };

    std::deque<std::byte> buffer;
    std::deque<AutoCloseFD> fds;

    while (true) {
        while (std::find(buffer.cbegin(), buffer.cend(), (std::byte) 0) == buffer.cend()) {
            std::array<std::byte, 1024> messageBuffer;
            unix::ReceivedMessage response;
            try {
                response = unix::receiveMessageWithFds(from.fd, messageBuffer);
            } catch (EndOfFile &) {
                return;
            }
            std::span<std::byte> receivedData(messageBuffer.data(), response.bytesReceived);

            buffer.insert(buffer.end(), receivedData.begin(), receivedData.end());

            for (auto & item : response.fds) {
                fds.insert(fds.end(), std::move(item));
            }
        }

        std::vector<std::byte> line;
        while (true) {
            auto ch = buffer.front();
            buffer.pop_front();
            if (ch == (std::byte) 0)
                break;
            line.push_back(ch);
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
                    if (fds.size() < 1) {
                        warn(
                            "Derivation '%s' didn't send file descriptor when adding to store",
                            store.printStorePath(drvPath));
                        sendError("org.nix.derivation-builder.NoFileDescriptor");
                        return;
                    }
                    AutoCloseFD narFd = std::move(fds.front());
                    fds.pop_front();

                    try {
                        // Read from the received file descriptor
                        FdSource narSource(narFd.get());
                        // TODO: lock paths
                        auto path = store.addToStoreFromDump(
                            narSource,
                            req.name,
                            FileSerialisationMethod::NixArchive,
                            req.method,
                            HashAlgorithm::SHA256,
                            {});
                        sendResponse(Response{Response::AddToStore{.path = path}});
                    } catch (SerialisationError & e) {
                        warn("Derivation '%s' sent an invalid NAR: %s", store.printStorePath(drvPath), e.info().msg);
                        sendError("org.nix.derivation-builder.InvalidNar");
                    }
                },
                [&](const Request::AddDerivation & req) {
                    // Write the derivation to the store
                    // TODO: lock paths
                    auto path = store.writeDerivation(req.derivation);

                    sendResponse(Response{Response::AddDerivation{.path = path}});
                },
                [&](const Request::SubmitOutput & req) {
                    // Register this as a build output
                    // Note: The actual output registration happens in registerOutputs()
                    // This method is primarily for the builder to signal completion of an output
                    // The store path is already tracked by the RestrictedStore
                    // Authorization is handled automatically by the RestrictedStore wrapper

                    try {
                        ValidPathInfo pathInfo(*store.queryPathInfo(req.path));

                        if (!pathInfo.isContentAddressed(store)) {
                            warn(
                                "Derivation '%s' tried to submit non-CA path '%s' for output '%s', skipping",
                                store.printStorePath(drvPath),
                                store.printStorePath(req.path),
                                req.name);
                            sendError("org.nix.derivation-builder.InvalidPath");
                            return;
                        }
                    } catch (const InvalidPath & ex) {
                        warn(
                            "Derivation '%s' tried to submit invalid path '%s' for output '%s', skipping",
                            store.printStorePath(drvPath),
                            store.printStorePath(req.path),
                            req.name);
                        sendError("org.nix.derivation-builder.InvalidPath");
                        return;
                    }

                    {
                        auto submittedOutputs(_submittedOutputs->lock());
                        if (submittedOutputs->contains(req.name)) {
                            warn(
                                "Derivation '%s' submitted duplicate output '%s', ignoring",
                                store.printStorePath(drvPath),
                                req.name);
                            sendError("org.nix.derivation-builder.DuplicateOutput");
                            return;
                        }

                        submittedOutputs->insert_or_assign(req.name, req.path);
                    }

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
