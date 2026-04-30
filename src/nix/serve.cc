#include "nix/cmd/command.hh"
#include "nix/util/file-system.hh"
#include "nix/util/serialise.hh"
#include "nix/util/signals.hh"
#include "nix/util/deleter.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/binary-cache-store.hh"
#include "nix/store/log-store.hh"
#include "nix/util/environment-variables.hh"

#include <future>
#include <regex>

#include <arpa/inet.h>
#include <microhttpd.h>

using namespace nix;

using Response = std::unique_ptr<MHD_Response, Deleter<MHD_destroy_response>>;

struct CmdServe : StoreCommand
{
    uint16_t port = 8080;
    std::string listenAddress = "127.0.0.1";
    std::optional<int> priority;
    std::optional<std::filesystem::path> portFile;

    CmdServe()
    {
        addFlag({
            .longName = "port",
            .shortName = 'p',
            .description = "Port to listen on (default: 8080). Use 0 to dynamically allocate a free port.",
            .labels = {"port"},
            .handler = {&port},
        });
        addFlag({
            .longName = "listen-address",
            .description = "IP address to listen on (default: `127.0.0.1`). "
                           "Use `0.0.0.0` or `::` to listen on all interfaces.",
            .labels = {"address"},
            .handler = {&listenAddress},
        });
        addFlag({
            .longName = "port-file",
            .description = "Write the bound port number to this file.",
            .labels = {"path"},
            .handler = {[this](std::string s) { portFile = s; }},
        });
        addFlag({
            .longName = "priority",
            .description = "Priority of this cache (overrides the store's default).",
            .labels = {"priority"},
            .handler = {[this](std::string s) { priority = std::stoi(s); }},
        });
    }

    std::string description() override
    {
        return "serve a Nix store as a HTTP binary cache";
    }

    Category category() override
    {
        return catSecondary;
    }

    std::string doc() override
    {
        return
#include "serve.md"
            ;
    }

    MHD_Result
    handleRequest(Store & store, MHD_Connection * connection, const std::string & url, std::string_view method)
    try {
        std::string clientAddr = "unknown";
        if (auto * info = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)) {
            char buf[INET6_ADDRSTRLEN];
            auto * addr = info->client_addr;
            const void * src = addr->sa_family == AF_INET6 ? (void *) &((sockaddr_in6 *) addr)->sin6_addr
                                                           : (void *) &((sockaddr_in *) addr)->sin_addr;
            if (inet_ntop(addr->sa_family, src, buf, sizeof(buf)))
                clientAddr = buf;
        }

        notice("request: client=%s, method=%s, url=%s", clientAddr, method, url);

        Response response;

        auto notFound = [&] {
            static constexpr std::string_view body = "404 not found\n";
            response.reset(MHD_create_response_from_buffer(body.size(), (void *) body.data(), MHD_RESPMEM_PERSISTENT));
            return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response.get());
        };

        static const std::regex narInfoUrlRegex{R"(^/([0-9a-z]+)\.narinfo$)"};
        static const std::regex narUrlRegex{R"(^/nar/([0-9a-z]+)-([0-9a-z]+)\.nar$)"};
        static const std::regex logUrlRegex{R"(^/log/([0-9a-z]+-[0-9a-zA-Z+\-._?=]+)$)"};

        if (method != MHD_HTTP_METHOD_GET && method != MHD_HTTP_METHOD_HEAD) {
            std::string_view body = "405 method not allowed\n";
            response.reset(MHD_create_response_from_buffer(body.size(), (void *) body.data(), MHD_RESPMEM_PERSISTENT));
            MHD_add_response_header(response.get(), "Allow", "GET, HEAD");
            return MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response.get());
        }

        if (url == "/nix-cache-info") {
            auto body = std::make_unique<std::string>(
                        "StoreDir: " + store.storeDir + "\n"
                        "WantMassQuery: " + (store.config.wantMassQuery ? "1" : "0") + "\n"
                        "Priority: " + std::to_string(priority.value_or(store.config.priority)) + "\n");
            response.reset(MHD_create_response_from_buffer(body->size(), body->data(), MHD_RESPMEM_MUST_COPY));
            MHD_add_response_header(response.get(), "Content-Type", "text/x-nix-cache-info");

        } else if (std::smatch m; std::regex_match(url, m, narInfoUrlRegex)) {
            auto hashPart = m[1].str();
            auto path = store.queryPathFromHashPart(hashPart);
            if (!path)
                return notFound();

            auto info = store.queryPathInfo(*path);
            NarInfo ni(*info);
            ni.compression = "none";
            // FIXME: would be nicer to use just the NAR hash, but we can't look up NARs by NAR hash.
            ni.url = "nar/" + std::string(info->path.hashPart()) + "-"
                     + info->narHash.to_string(HashFormat::Nix32, false) + ".nar";
            ni.fileSize = info->narSize;
            auto body = ni.to_string(store);
            response.reset(MHD_create_response_from_buffer(body.size(), body.data(), MHD_RESPMEM_MUST_COPY));
            MHD_add_response_header(response.get(), "Content-Type", "text/x-nix-narinfo");

        } else if (std::smatch m; std::regex_match(url, m, narUrlRegex)) {
            auto hashPart = m[1].str();
            auto expectedNarHash = m[2].str();
            auto path = store.queryPathFromHashPart(hashPart);
            if (!path)
                return notFound();

            auto info = store.queryPathInfo(*path);
            if (info->narHash.to_string(HashFormat::Nix32, false) != expectedNarHash)
                return notFound();

            struct State
            {
                std::unique_ptr<Source> source;
                std::optional<char> firstByte;
            };

            auto state = std::make_unique<State>();

            state->source = sinkToSource([&store, p = *path](Sink & sink) { store.narFromPath(p, sink); });

            // Read the first byte so we can return a 404 if the store path / NAR doesn't exist anymore.
            try {
                char firstByte;
                if (state->source->read(&firstByte, 1))
                    state->firstByte = firstByte;
            } catch (InvalidPath &) {
                return notFound();
            } catch (SubstituteGone &) {
                return notFound();
            } catch (EndOfFile &) {
            }

            // Stream the NAR.
            auto reader = [](void * cls, uint64_t pos, char * buf, size_t max) -> ssize_t {
                auto & state = *static_cast<State *>(cls);
                size_t read = 0;
                if (pos == 0 && state.firstByte) {
                    *buf++ = *state.firstByte;
                    pos++;
                    max--;
                    read++;
                    state.firstByte.reset();
                }

                static bool truncate = getEnv("_NIX_TEST_NIX_SERVE_TRUNCATE_NAR").value_or("") == "1";
                static std::atomic<uint64_t> truncatePoint{200000};
                if (truncate && pos > truncatePoint) {
                    truncatePoint += 200000;
                    return MHD_CONTENT_READER_END_WITH_ERROR;
                }

                try {
                    read += state.source->read(buf, max);
                    return read;
                } catch (EndOfFile &) {
                    return MHD_CONTENT_READER_END_OF_STREAM;
                } catch (...) {
                    return MHD_CONTENT_READER_END_WITH_ERROR;
                }
            };

            auto freeCb = [](void * cls) { delete static_cast<State *>(cls); };

            response.reset(MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 65536, reader, state.release(), freeCb));
            MHD_add_response_header(response.get(), "Content-Type", "application/x-nix-nar");

        } else if (std::smatch m; std::regex_match(url, m, logUrlRegex)) {
            auto * logStore = dynamic_cast<LogStore *>(&store);
            if (!logStore)
                return notFound();

            StorePath path{m[1].str()};

            auto log = logStore->getBuildLog(path);
            if (!log)
                return notFound();

            response.reset(MHD_create_response_from_buffer(log->size(), log->data(), MHD_RESPMEM_MUST_COPY));
            MHD_add_response_header(response.get(), "Content-Type", "text/plain; charset=utf-8");
        } else
            return notFound();

        return MHD_queue_response(connection, MHD_HTTP_OK, response.get());

    } catch (const Error & e) {
        auto body = fmt("500 Internal Server Error\n\nError: %s", e.message());
        Response response;
        response.reset(MHD_create_response_from_buffer(body.size(), body.data(), MHD_RESPMEM_MUST_COPY));
        MHD_add_response_header(response.get(), "Content-Type", "text/plain");
        return MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response.get());
    }

    void run(ref<Store> store) override
    {
        struct Ctx
        {
            Store & store;
            CmdServe & cmd;
        };

        Ctx ctx{*store, *this};

        auto handler = [](void * cls,
                          MHD_Connection * connection,
                          const char * url,
                          const char * method,
                          const char * version,
                          const char * upload_data,
                          size_t * upload_data_size,
                          void ** con_cls) -> MHD_Result {
            auto & ctx = *static_cast<Ctx *>(cls);
            auto & store = ctx.store;
            auto & cmd = ctx.cmd;
            return cmd.handleRequest(store, connection, std::string(url), method);
        };

        sockaddr_in addr4{};
        sockaddr_in6 addr6{};
        const sockaddr * sockAddr = nullptr;
        unsigned int flags = MHD_USE_INTERNAL_POLLING_THREAD;

        if (inet_pton(AF_INET, listenAddress.c_str(), &addr4.sin_addr) == 1) {
            addr4.sin_family = AF_INET;
            addr4.sin_port = htons(port);
            sockAddr = (const sockaddr *) &addr4;
        } else if (inet_pton(AF_INET6, listenAddress.c_str(), &addr6.sin6_addr) == 1) {
            addr6.sin6_family = AF_INET6;
            addr6.sin6_port = htons(port);
            sockAddr = (const sockaddr *) &addr6;
            flags |= MHD_USE_IPv6;
        } else
            throw Error("invalid listen address '%s'", listenAddress);

        auto * daemon = MHD_start_daemon(
            flags, port, nullptr, nullptr, handler, &ctx, MHD_OPTION_SOCK_ADDR, sockAddr, MHD_OPTION_END);

        if (!daemon)
            throw Error("failed to start HTTP daemon on %s:%d", listenAddress, port);

        Finally _stopDaemon{[&] {
            notice("Shutting down...");
            MHD_stop_daemon(daemon);
        }};

        auto * info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_BIND_PORT);
        uint16_t boundPort = info ? info->port : port;
        notice("Listening on http://%s:%d/", listenAddress, boundPort);

        if (portFile)
            writeFile(*portFile, std::to_string(boundPort) + "\n");

        /* Wait for Ctrl-C. */
        std::promise<void> interruptPromise;
        std::future<void> interruptFuture = interruptPromise.get_future();
        auto callback = createInterruptCallback([&]() { interruptPromise.set_value(); });
        interruptFuture.get();
    }
};

static auto rCmdServe = registerCommand<CmdServe>("serve");
