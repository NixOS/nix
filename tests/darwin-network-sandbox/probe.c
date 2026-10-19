#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

typedef union
{
    struct sockaddr generic;
    struct sockaddr_in ipv4;
    struct sockaddr_in6 ipv6;
    struct sockaddr_un unixSocket;
} SocketAddress;

enum AddressKind {
    LOOPBACK,
    WILDCARD,
    DOCUMENTATION,
};

static void fail(const char * format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static ssize_t checked(ssize_t result, const char * operation)
{
    if (result == -1)
        fail("%s: %s", operation, strerror(errno));

    return result;
}

static const char * requiredEnv(const char * name)
{
    const char * value = getenv(name);

    if (value == NULL)
        fail("%s is not set", name);

    return value;
}

static void checkLength(ssize_t result, size_t expected, const char * operation)
{
    checked(result, operation);
    if ((size_t) result != expected)
        fail("%s: expected %zu bytes, got %zd", operation, expected, result);
}

static const char * familyName(int family)
{
    return family == AF_INET ? "IPv4" : "IPv6";
}

static SocketAddress makeAddress(int family, enum AddressKind kind, uint16_t port)
{
    static const char * hosts[][2] = {
        [LOOPBACK] = {"127.0.0.1", "::1"},
        [WILDCARD] = {"0.0.0.0", "::"},
        // Avoid targeting real services.
        [DOCUMENTATION] = {"192.0.2.1", "2001:db8::1"},
    };
    const char * host = hosts[kind][family == AF_INET6];
    SocketAddress address = {0};
    void * destination;

    if (family == AF_INET) {
        address.ipv4.sin_len = sizeof(address.ipv4);
        address.ipv4.sin_family = AF_INET;
        address.ipv4.sin_port = htons(port);

        destination = &address.ipv4.sin_addr;
    } else {
        address.ipv6.sin6_len = sizeof(address.ipv6);
        address.ipv6.sin6_family = AF_INET6;
        address.ipv6.sin6_port = htons(port);

        destination = &address.ipv6.sin6_addr;
    }

    if (inet_pton(family, host, destination) != 1)
        fail("inet_pton: invalid numeric address %s", host);

    return address;
}

static int openSocket(int family, int type)
{
    return checked(socket(family, type, 0), "socket");
}

static void expectDenied(ssize_t result, const char * operation)
{
    int error = errno;

    if (result == -1 && error == EPERM)
        return;

    if (result == -1)
        fail("%s: expected sandbox denial (EPERM), got: %s", operation, strerror(error));

    fail("%s unexpectedly succeeded", operation);
}

static void checkOutboundDenied(const struct sockaddr * address)
{
    int fd = openSocket(address->sa_family, SOCK_STREAM);
    char byte = 'x';

    // A missing sandbox denial must fail immediately, not wait for a timeout.
    checked(fcntl(fd, F_SETFL, O_NONBLOCK), "fcntl O_NONBLOCK");
    expectDenied(connect(fd, address, address->sa_len), "TCP connect");
    close(fd);

    fd = openSocket(address->sa_family, SOCK_DGRAM);
    expectDenied(sendto(fd, &byte, sizeof(byte), 0, address, address->sa_len), "UDP sendto");
    close(fd);
}

static void checkLoopbackDenied(int family)
{
    SocketAddress address = makeAddress(family, LOOPBACK, 9);

    fprintf(stderr, "%s loopback outbound denied\n", familyName(family));
    checkOutboundDenied(&address.generic);
}

static void checkBindDenied(int family)
{
    SocketAddress address = makeAddress(family, LOOPBACK, 0);
    const int types[] = {SOCK_STREAM, SOCK_DGRAM};

    fprintf(stderr, "%s loopback bind denied\n", familyName(family));
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        int fd = openSocket(family, types[i]);

        expectDenied(bind(fd, &address.generic, address.generic.sa_len), i == 0 ? "TCP bind" : "UDP bind");
        close(fd);
    }
}

static int bindSocket(struct sockaddr * address, int type)
{
    int fd = openSocket(address->sa_family, type);
    socklen_t length = address->sa_len;

    checked(bind(fd, address, length), "bind");
    checked(getsockname(fd, address, &length), "getsockname");

    return fd;
}

static int listenAt(struct sockaddr * address)
{
    int server = bindSocket(address, SOCK_STREAM);

    checked(listen(server, 1), "listen");

    return server;
}

static int connectTo(const struct sockaddr * address)
{
    int client = openSocket(address->sa_family, SOCK_STREAM);

    checked(connect(client, address, address->sa_len), "connect");

    return client;
}

static void sendByte(int fd, char byte)
{
    checkLength(write(fd, &byte, sizeof(byte)), sizeof(byte), "write");
}

static void receiveByte(int fd, char expected)
{
    char received = 0;

    checkLength(read(fd, &received, sizeof(received)), sizeof(received), "read");
    if (received != expected)
        fail("read: expected byte %d, got %d", expected, received);
}

static void checkStream(struct sockaddr * address)
{
    int server = listenAt(address);
    int client = connectTo(address);
    int peer = checked(accept(server, NULL, NULL), "accept");

    sendByte(client, 'q');
    receiveByte(peer, 'q');
    sendByte(peer, 'r');
    receiveByte(client, 'r');

    close(peer);
    close(client);
    close(server);
}

static void checkTcpLoopback(int family)
{
    SocketAddress address = makeAddress(family, LOOPBACK, 0);

    fprintf(stderr, "%s TCP loopback\n", familyName(family));
    checkStream(&address.generic);
}

static void checkLocalhost(int family)
{
    struct addrinfo hints = {.ai_family = family, .ai_socktype = SOCK_STREAM};
    struct addrinfo * addresses;

    fprintf(stderr, "%s localhost resolution\n", familyName(family));
    int result = getaddrinfo("localhost", "0", &hints, &addresses);
    if (result != 0)
        fail("getaddrinfo: %s", gai_strerror(result));

    for (struct addrinfo * address = addresses; address != NULL; address = address->ai_next)
        checkStream(address->ai_addr);

    freeaddrinfo(addresses);
}

static void
transferDatagram(int sender, int receiver, const SocketAddress * destination, SocketAddress * source, char byte)
{
    socklen_t length = sizeof(*source);
    char received = 0;

    checkLength(
        sendto(sender, &byte, sizeof(byte), 0, &destination->generic, destination->generic.sa_len),
        sizeof(byte),
        "sendto");
    checkLength(
        recvfrom(receiver, &received, sizeof(received), 0, &source->generic, &length), sizeof(received), "recvfrom");
    if (received != byte)
        fail("recvfrom: expected byte %d, got %d", byte, received);
}

static void checkUdpLoopback(int family)
{
    SocketAddress address = makeAddress(family, LOOPBACK, 0);
    SocketAddress replyAddress;

    fprintf(stderr, "%s UDP loopback\n", familyName(family));
    int receiver = bindSocket(&address.generic, SOCK_DGRAM);
    int sender = openSocket(family, SOCK_DGRAM);

    transferDatagram(sender, receiver, &address, &replyAddress, 'q');
    transferDatagram(receiver, sender, &replyAddress, &address, 'r');

    close(sender);
    close(receiver);
}

static void checkWildcardBind(int family)
{
    SocketAddress address = makeAddress(family, WILDCARD, 0);

    fprintf(stderr, "%s wildcard TCP bind/listen\n", familyName(family));
    int server = listenAt(&address.generic);

    close(server);
}

static void checkRemoteDenied(int family)
{
    SocketAddress remote = makeAddress(family, DOCUMENTATION, 9);

    fprintf(stderr, "%s non-loopback denied\n", familyName(family));
    checkOutboundDenied(&remote.generic);
}

static SocketAddress unixAddress(const char * directory, const char * name)
{
    SocketAddress address = {0};
    struct sockaddr_un * socket = &address.unixSocket;

    socket->sun_len = sizeof(*socket);
    socket->sun_family = AF_UNIX;

    int length = snprintf(socket->sun_path, sizeof(socket->sun_path), "%s/%s", directory, name);
    if (length < 0 || (size_t) length >= sizeof(socket->sun_path))
        fail("Unix socket path is too long: %s/%s", directory, name);

    return address;
}

static void checkUnixSocket(const char * directory)
{
    SocketAddress address = unixAddress(directory, "socket");

    fprintf(stderr, "Unix socket at %s\n", address.unixSocket.sun_path);
    checkStream(&address.generic);

    checked(unlink(address.unixSocket.sun_path), "unlink socket");
}

static void writeFile(const char * path)
{
    const char contents[] = "ok\n";
    int fd = checked(open(path, O_WRONLY | O_CREAT | O_EXCL, 0666), "create file");

    checkLength(write(fd, contents, sizeof(contents) - 1), sizeof(contents) - 1, "write file");
    checked(close(fd), "close file");
}

static void checkUnixDenied(const char * directory)
{
    SocketAddress address = unixAddress(directory, "denied");
    struct stat st;
    int fd;

    fprintf(stderr, "Unix sockets outside the permitted directories: %s\n", directory);

    // File creation must work here, so a file permission failure cannot
    // masquerade as the expected network denial.
    writeFile(address.unixSocket.sun_path);
    checked(unlink(address.unixSocket.sun_path), "unlink file");

    fd = openSocket(AF_UNIX, SOCK_STREAM);
    expectDenied(bind(fd, &address.generic, address.generic.sa_len), "outside Unix bind");
    close(fd);

    address = unixAddress(directory, "socket");
    checked(stat(address.unixSocket.sun_path, &st), "stat outside socket");
    if (!S_ISSOCK(st.st_mode))
        fail("outside listener is not a socket");

    fd = openSocket(AF_UNIX, SOCK_STREAM);
    expectDenied(connect(fd, &address.generic, address.generic.sa_len), "outside Unix connect");
    close(fd);
}

static void serveUnixSocket(const char * directory)
{
    SocketAddress address = unixAddress(directory, "socket");
    int server = listenAt(&address.generic);

    puts("ready");
    fflush(stdout);

    for (;;) {
        int peer = checked(accept(server, NULL, NULL), "accept");

        receiveByte(peer, 'q');
        sendByte(peer, 'r');
        close(peer);
    }
}

static void checkUnixClient(const char * directory)
{
    SocketAddress address = unixAddress(directory, "socket");
    int client = connectTo(&address.generic);

    sendByte(client, 'q');
    receiveByte(client, 'r');
    close(client);
}

int main(int argc, char ** argv)
{
    signal(SIGPIPE, SIG_IGN);
    alarm(30);

    if (argc == 3 && strcmp(argv[1], "unix-listener") == 0) {
        alarm(300);
        serveUnixSocket(argv[2]);
        return EXIT_SUCCESS;
    }

    if (argc == 3 && strcmp(argv[1], "unix-client") == 0) {
        checkUnixClient(argv[2]);
        return EXIT_SUCCESS;
    }

    if (argc != 2 || (strcmp(argv[1], "default") != 0 && strcmp(argv[1], "local") != 0))
        fail("usage: %s default | local | unix-listener DIR | unix-client DIR", argv[0]);

    bool local = strcmp(argv[1], "local") == 0;
    const int families[] = {AF_INET, AF_INET6};

    for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
        int family = families[i];

        if (local) {
            checkTcpLoopback(family);
            checkUdpLoopback(family);
            checkLocalhost(family);
            checkWildcardBind(family);
            checkRemoteDenied(family);
        } else {
            checkBindDenied(family);
            checkLoopbackDenied(family);
        }
    }

    // Unix-socket permissions must hold with local IP networking both on and off.
    checkUnixSocket(requiredEnv("NIX_BUILD_TOP"));
    checkUnixSocket(requiredEnv("globalTmpDir"));
    checkUnixDenied(requiredEnv("outsideDir"));
    writeFile(requiredEnv("out"));
    return EXIT_SUCCESS;
}
