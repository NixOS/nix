/*
 * Builder that asserts no signals have SIG_IGN disposition.
 *
 * Nix builders should start with all signal dispositions at SIG_DFL.
 * If any signal is SIG_IGN, builds become non-deterministic as the result
 * now additionally depends on how the daemon was started rather than just
 * on the derivation inputs.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    struct sigaction sa;
    int bad = 0;

    for (int sig = 1; sig < NSIG; sig++) {
        if (sig == SIGKILL || sig == SIGSTOP)
            continue;
        if (sigaction(sig, NULL, &sa) != 0)
            continue;
        if (sa.sa_handler == SIG_IGN) {
            fprintf(stderr, "FAIL: signal %d has disposition SIG_IGN\n", sig);
            bad = 1;
        }
    }

    if (!bad) {
        FILE * f = fopen(getenv("out"), "w");
        fputs("PASS\n", f);
        fclose(f);
    }

    return bad;
}
