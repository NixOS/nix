#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


#define SOCKET_PATH "/tmp/nix-daemon"


int main(int argc, char * * argv)
{
    int res;
    
    int sock = socket(PF_UNIX, SOCK_STREAM, 0);
    assert(sock != -1);

    unlink(SOCKET_PATH);
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    
    res = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
    assert(res != -1);

    res = listen(sock, 5);
    if (res == -1)
        fprintf(stderr, "%s\n", strerror(errno));
    assert(res != -1);

    while (1) {

        struct sockaddr_un remoteAddr;
        socklen_t remoteAddrLen = sizeof(remoteAddr);
        
        int remote = accept(sock,
            (struct sockaddr *) &remoteAddr, &remoteAddrLen);
        if (remote == -1)
            fprintf(stderr, "%s\n", strerror(errno));
        assert(remote != -1);

        fprintf(stderr, "connection %d\n", remote);

        while (1) {
            int i;
            ssize_t len;

            len = recv(remote, &i, sizeof(i), 0);
            if (len < sizeof(i)) break;

            //            printf("%d\n", i);

            int j = i * 2;
            len = send(remote, &j, sizeof(j), 0);
            if (len == -1)
                fprintf(stderr, "%s\n", strerror(errno));
            assert(len == sizeof(j));
        }
        
        close(remote);
    }
    
    close(sock);

    return 0;
}
