/* Code shared between libmain and nix-setuid-helper. */

extern char * * environ;


namespace nix {
    

void setuidCleanup()
{
    /* Don't trust the environment. */
    environ = 0;

    /* Make sure that file descriptors 0, 1, 2 are open. */
    for (int fd = 0; fd <= 2; ++fd) {
        struct stat st;
        if (fstat(fd, &st) == -1) abort();
    }
}

 
}
