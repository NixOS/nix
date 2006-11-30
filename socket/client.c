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

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    
    res = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
    assert(res != -1);

    int i;
    for (i = 0; i < 100000; i++) {
        int len = send(sock, &i, sizeof(i), 0);
        assert(len == sizeof(i));

        int j;
        len = recv(sock, &j, sizeof(j), 0);
        if (len < sizeof(j)) break;
        assert(i * 2 == j);
    }

    close(sock);

    return 0;
}
