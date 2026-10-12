/* Time getaddrinfo from inside the guest for hosts the clients resolve at startup. */
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		struct addrinfo hints, *result = NULL;
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		struct timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);
		int status = getaddrinfo(argv[i], "443", &hints, &result);
		clock_gettime(CLOCK_MONOTONIC, &end);
		int count = 0;
		for (struct addrinfo* a = result; a; a = a->ai_next) ++count;
		printf("GETADDRINFO host=%s status=%d (%s) addresses=%d seconds=%.2f\n", argv[i], status,
		       status ? gai_strerror(status) : "ok", count,
		       (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9);
		fflush(stdout);
		if (result) freeaddrinfo(result);
	}
	return 0;
}
