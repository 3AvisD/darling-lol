#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>

/* Run once normally and once from an executable named LeagueClient.
 * A sustained empty poll must remain immediate for ordinary applications;
 * the client workaround must still reduce busy polling and preserve readiness. */
int main(int argc, char **argv)
{
    int client = argc > 1 && atoi(argv[1]);
    int fds[2];
    if (pipe(fds)) return 1;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    uint64_t start = mach_absolute_time();
    for (unsigned i = 0; i < 25000; ++i) {
        fd_set readable;
        FD_ZERO(&readable); FD_SET(fds[0], &readable);
        struct timeval timeout = {0, 0};
        if (select(fds[0] + 1, &readable, NULL, NULL, &timeout) != 0 ||
            FD_ISSET(fds[0], &readable)) return 2;
    }
    double seconds = (mach_absolute_time() - start) * (double)tb.numer / tb.denom / 1e9;
    if ((!client && seconds > 1) || (client && seconds < 1)) {
        printf("SELECT_BACKOFF client=%d empty_seconds=%.3f pass=0\n", client, seconds);
        return 3;
    }
    for (unsigned i = 0; i < 100; ++i) {
        char byte = 'x';
        if (write(fds[1], &byte, 1) != 1) return 4;
        fd_set readable;
        FD_ZERO(&readable); FD_SET(fds[0], &readable);
        struct timeval timeout = {0, 0};
        if (select(fds[0] + 1, &readable, NULL, NULL, &timeout) != 1 ||
            !FD_ISSET(fds[0], &readable) || read(fds[0], &byte, 1) != 1) return 5;
    }
    close(fds[0]); close(fds[1]);
    printf("SELECT_BACKOFF client=%d empty_seconds=%.3f readiness=pass pass=1\n", client, seconds);
    return 0;
}
