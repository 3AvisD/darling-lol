#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

static const char *fixture;
static FILE *config_open(const char *path, const char *mode) {
    (void)path; (void)mode;
    FILE *file = tmpfile();
    if (file) { fputs(fixture, file); rewind(file); }
    return file;
}
#define fopen config_open
#include "../build/darling-src/src/external/configd/darling/src/dnsinfo/dnsinfo.c"
#undef fopen

int main(void) {
    fixture = "# test\r\n nameserver 192.0.2.1 # comment\r\n"
        "nameserver 2001:db8::1\nnameserver invalid\n"
        "search one.example two.example\ndomain example\n"
        "options timeout:5 attempts:2\n";
    for (int i = 0; i < 1000; ++i) {
        dns_config_t *config = dns_configuration_copy();
        if (!config || config->n_resolver != 1) return 1;
        dns_resolver_t *r = config->resolver[0];
        if (r->n_nameserver != 2 || r->n_search != 2 ||
            r->nameserver[0]->sa_family != AF_INET ||
            r->nameserver[0]->sa_len != sizeof(struct sockaddr_in) ||
            r->nameserver[1]->sa_family != AF_INET6 ||
            r->nameserver[1]->sa_len != sizeof(struct sockaddr_in6) ||
            r->flags != DNS_RESOLVER_FLAGS_REQUEST_ALL_RECORDS ||
            strcmp(r->search[1], "two.example") || strcmp(r->domain, "example") ||
            strcmp(r->options, "timeout:5 attempts:2")) return 2;
        dns_configuration_free(config);
    }
    fixture = "# no resolver\n \nnameserver invalid\nsearch example\n";
    if (dns_configuration_copy()) return 3;
    dns_configuration_free(NULL);
    puts("DNS_CONFIG_CHECK IPv4 IPv6 search options empty repeated-free pass=1");
    return 0;
}
