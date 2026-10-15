/* Host-side regression for LaunchServices' URL hand-off (LSHostOpenBus.c).
 *
 * Runs a private dbus-daemon with no service directories (nothing can be
 * activated, so no real portal or browser is reachable), plus a fake
 * org.freedesktop.portal.Desktop / org.freedesktop.systemd1 forked from this
 * process, and drives the same hand-off code LaunchServices uses, linked
 * natively against libsystemd. The real session bus is never used.
 *
 *   cc -std=gnu11 -O2 -Wall -Wextra \
 *     -I build/darling-src/src/frameworks/CoreServices/src/LaunchServices \
 *     tools/riot-host-open-url-check.c \
 *     build/darling-src/src/frameworks/CoreServices/src/LaunchServices/LSHostOpenBus.c \
 *     $(pkg-config --cflags --libs libsystemd) -o build/riot-host-open-url-check
 *   build/riot-host-open-url-check
 */
#include "LSHostOpenBus.h"
#include <systemd/sd-bus.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum { OWN_PORTAL = 1, OWN_SYSTEMD = 2, PORTAL_FAIL = 4, PORTAL_HANG = 8 };

static char tmpdir[64], records[128], bus_address[256];
static int failures, checks, bus_new_calls, fake_mode;
static FILE *record_file;
static pid_t daemon_pid, main_pid;

/* Stops the private bus if the test exits early. */
static void stop_daemon(void) {
    if (daemon_pid > 0 && getpid() == main_pid) {
        kill(daemon_pid, SIGTERM);
        waitpid(daemon_pid, NULL, 0);
        daemon_pid = 0;
    }
}

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); } } while (0)

/* ---- fake desktop services (child process) ---- */

static void record(const char *line) {
    fputs(line, record_file);
    fputc('\n', record_file);
    fflush(record_file);
}

static int portal_handler(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    (void) userdata; (void) error;
    if (!sd_bus_message_is_method_call(m, "org.freedesktop.portal.OpenURI", "OpenURI"))
        return 0;
    const char *parent, *uri;
    int options = 0;
    if (sd_bus_message_read(m, "ss", &parent, &uri) < 0 ||
        sd_bus_message_enter_container(m, 'a', "{sv}") < 0)
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad OpenURI arguments");
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        sd_bus_message_skip(m, "sv");
        sd_bus_message_exit_container(m);
        options++;
    }
    sd_bus_message_exit_container(m);
    char line[8192];
    snprintf(line, sizeof(line), "OpenURI\t%s\t%s\t%d", parent, uri, options);
    record(line);
    if (fake_mode & PORTAL_HANG) {
        sd_bus_message_ref(m);   /* never answered */
        return 1;
    }
    if (fake_mode & PORTAL_FAIL)
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_FAILED, "fake portal failure");
    return sd_bus_reply_method_return(m, "o", "/org/freedesktop/portal/desktop/request/1_1/t");
}

static void join(char *out, size_t size, char **list) {
    out[0] = '\0';
    for (size_t i = 0; list && list[i]; i++) {
        if (i) strncat(out, "\x1e", size - strlen(out) - 1);
        strncat(out, list[i], size - strlen(out) - 1);
    }
}

static int systemd_handler(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    (void) userdata; (void) error;
    if (!sd_bus_message_is_method_call(m, "org.freedesktop.systemd1.Manager", "StartTransientUnit"))
        return 0;
    const char *name, *mode, *key, *value;
    char kill_mode[64] = "", collect_mode[64] = "", path[512] = "", argv[8192] = "", env[2048] = "";
    int execs = 0, ignore;
    if (sd_bus_message_read(m, "ss", &name, &mode) < 0 ||
        sd_bus_message_enter_container(m, 'a', "(sv)") < 0)
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad unit arguments");
    while (sd_bus_message_enter_container(m, 'r', "sv") > 0) {
        sd_bus_message_read(m, "s", &key);
        if (!strcmp(key, "ExecStart")) {
            sd_bus_message_enter_container(m, 'v', "a(sasb)");
            sd_bus_message_enter_container(m, 'a', "(sasb)");
            while (sd_bus_message_enter_container(m, 'r', "sasb") > 0) {
                const char *p; char **list = NULL;
                sd_bus_message_read(m, "s", &p);
                sd_bus_message_read_strv(m, &list);
                sd_bus_message_read(m, "b", &ignore);
                sd_bus_message_exit_container(m);
                snprintf(path, sizeof(path), "%s", p);
                join(argv, sizeof(argv), list);
                for (size_t i = 0; list && list[i]; i++) free(list[i]);
                free(list);
                execs++;
            }
            sd_bus_message_exit_container(m);
            sd_bus_message_exit_container(m);
        } else if (!strcmp(key, "Environment")) {
            char **list = NULL;
            sd_bus_message_enter_container(m, 'v', "as");
            sd_bus_message_read_strv(m, &list);
            sd_bus_message_exit_container(m);
            join(env, sizeof(env), list);
            for (size_t i = 0; list && list[i]; i++) free(list[i]);
            free(list);
        } else if (!strcmp(key, "KillMode") || !strcmp(key, "CollectMode")) {
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &value);
            sd_bus_message_exit_container(m);
            snprintf(!strcmp(key, "KillMode") ? kill_mode : collect_mode, 64, "%s", value);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    sd_bus_message_skip(m, "a(sa(sv))");
    char line[16384];
    snprintf(line, sizeof(line), "StartTransientUnit\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s",
             name, mode, path, argv, execs, kill_mode, collect_mode, env);
    record(line);
    return sd_bus_reply_method_return(m, "o", "/org/freedesktop/systemd1/job/1");
}

static pid_t start_fake(int mode) {
    int ready[2];
    if (pipe(ready)) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid > 0) {
        char c = 0;
        close(ready[1]);
        if (read(ready[0], &c, 1) != 1 || c != 'R') { fprintf(stderr, "fake service failed\n"); exit(2); }
        close(ready[0]);
        return pid;
    }
    close(ready[0]);
    fake_mode = mode;
    record_file = fopen(records, "a");
    sd_bus *bus = NULL;
    if (!record_file || sd_bus_new(&bus) < 0 || sd_bus_set_address(bus, bus_address) < 0 ||
        sd_bus_set_bus_client(bus, 1) < 0 || sd_bus_start(bus) < 0)
        _exit(3);
    sd_bus_add_object(bus, NULL, "/org/freedesktop/portal/desktop", portal_handler, NULL);
    sd_bus_add_object(bus, NULL, "/org/freedesktop/systemd1", systemd_handler, NULL);
    if ((mode & OWN_PORTAL) && sd_bus_request_name(bus, "org.freedesktop.portal.Desktop", 0) < 0)
        _exit(4);
    if ((mode & OWN_SYSTEMD) && sd_bus_request_name(bus, "org.freedesktop.systemd1", 0) < 0)
        _exit(5);
    if (write(ready[1], "R", 1) != 1)
        _exit(6);
    for (;;) {
        int r = sd_bus_process(bus, NULL);
        if (r < 0) _exit(7);
        if (r == 0 && sd_bus_wait(bus, UINT64_MAX) < 0) _exit(8);
    }
}

static void stop_fake(pid_t pid) {
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---- client side ---- */

static int counting_bus_new(void **ret) {
    bus_new_calls++;
    return sd_bus_new((sd_bus **) ret);
}

static struct ls_host_api native_api(void) {
    struct ls_host_api api;
    memset(&api, 0, sizeof(api));
#define SET(f) api.f = (__typeof__(api.f)) f
    api.sd_bus_new = counting_bus_new;
    SET(sd_bus_set_address); SET(sd_bus_set_bus_client); SET(sd_bus_start);
    SET(sd_bus_flush_close_unref); SET(sd_bus_message_new_method_call); SET(sd_bus_message_append);
    SET(sd_bus_message_append_strv); SET(sd_bus_message_open_container);
    SET(sd_bus_message_close_container); SET(sd_bus_call); SET(sd_bus_message_unref);
    SET(sd_bus_error_free); SET(getenv); SET(access);
#undef SET
    api.getuid = (unsigned int (*)(void)) getuid;
    api.runtime_dir_root = tmpdir;   /* never /run/user in this test */
    return api;
}

static char *read_records(void) {
    static char buffer[65536];
    FILE *f = fopen(records, "r");
    size_t n = f ? fread(buffer, 1, sizeof(buffer) - 1, f) : 0;
    if (f) fclose(f);
    buffer[n] = '\0';
    return buffer;
}

static void clear_records(void) {
    FILE *f = fopen(records, "w");
    if (f) fclose(f);
}

/* Splits one record line into tab-separated fields (empty fields kept). */
static int fields(char *line, char **out, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        out[n++] = p;
        p = strchr(p, '\t');
        if (!p) break;
        *p++ = '\0';
    }
    char *nl = strchr(out[n - 1], '\n');
    if (nl) *nl = '\0';
    return n;
}

static int count_lines(const char *s) {
    int n = 0;
    for (; *s; s++) n += *s == '\n';
    return n;
}

static void check_portal(const char *url) {
    struct ls_host_api api = native_api();
    clear_records();
    int method = 0, r = _LSHostOpenURL(&api, url, &method);
    CHECK(r == 0 && method == LS_HOST_OPEN_VIA_PORTAL, "portal open of %s: r=%d method=%d", url, r, method);
    char *rec = read_records(), *f[8];
    CHECK(count_lines(rec) == 1, "portal: expected exactly one call, got:\n%s", rec);
    int n = fields(rec, f, 8);
    CHECK(n == 4 && !strcmp(f[0], "OpenURI") && !strcmp(f[1], "") && !strcmp(f[2], url) && !strcmp(f[3], "0"),
          "portal call for %s recorded as '%s' '%s' '%s'", url, f[0], n > 2 ? f[2] : "", n > 3 ? f[3] : "");
}

static void check_systemd(const char *url, const char *what) {
    struct ls_host_api api = native_api();
    clear_records();
    int method = 0, r = _LSHostOpenURL(&api, url, &method);
    CHECK(r == 0 && method == LS_HOST_OPEN_VIA_SYSTEMD, "%s: r=%d method=%d", what, r, method);
    char *rec = read_records();
    char *unit = strstr(rec, "StartTransientUnit\t"), *f[10];
    CHECK(unit != NULL, "%s: no StartTransientUnit call, records:\n%s", what, rec);
    if (!unit) return;
    int n = fields(unit, f, 10);
    char expected_argv[8192];
    snprintf(expected_argv, sizeof(expected_argv), "xdg-open\x1e%s", url);
    size_t plen = strlen(f[3]);
    CHECK(n == 9, "%s: malformed record", what);
    CHECK(!strncmp(f[1], "app-darling-openurl-", 20) && strstr(f[1], ".service"), "%s: unit name %s", what, f[1]);
    CHECK(!strcmp(f[2], "fail"), "%s: mode %s", what, f[2]);
    CHECK(!strcmp(f[3], "xdg-open") || (f[3][0] == '/' && plen > 9 && !strcmp(f[3] + plen - 9, "/xdg-open")),
          "%s: ExecStart path %s", what, f[3]);
    CHECK(!strcmp(f[4], expected_argv), "%s: argv is not [xdg-open, url]: %s", what, f[4]);
    CHECK(!strcmp(f[5], "1"), "%s: %s ExecStart entries", what, f[5]);
    CHECK(!strcmp(f[6], "process") && !strcmp(f[7], "inactive-or-failed"), "%s: KillMode=%s CollectMode=%s",
          what, f[6], f[7]);
    CHECK(n == 9 && !strcmp(f[8], "DISPLAY=:97\x1eWAYLAND_DISPLAY=wayland-test"), "%s: Environment=%s",
          what, n == 9 ? f[8] : "");
}

static void check_rejected(const char *url, const char *what) {
    struct ls_host_api api = native_api();
    clear_records();
    int before = bus_new_calls, r = _LSHostOpenURL(&api, url, NULL);
    CHECK(r == -EINVAL, "%s: expected -EINVAL, got %d", what, r);
    CHECK(bus_new_calls == before, "%s: a bus connection was opened", what);
    CHECK(read_records()[0] == '\0', "%s: reached the bus", what);
}

static pid_t start_daemon(void) {
    char config[128], conf[1024];
    snprintf(config, sizeof(config), "%s/bus.conf", tmpdir);
    snprintf(conf, sizeof(conf),
             "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
             " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
             "<busconfig><type>session</type><listen>unix:path=%s/%u/bus</listen>"
             "<auth>EXTERNAL</auth><policy context=\"default\"><allow send_destination=\"*\"/>"
             "<allow receive_sender=\"*\"/><allow own=\"*\"/></policy></busconfig>\n", tmpdir, getuid());
    FILE *f = fopen(config, "w");
    if (!f || fputs(conf, f) < 0 || fclose(f)) { perror(config); exit(2); }

    int out[2];
    if (pipe(out)) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid == 0) {
        char arg_config[160], arg_fd[32];
        close(out[0]);
        snprintf(arg_config, sizeof(arg_config), "--config-file=%s", config);
        snprintf(arg_fd, sizeof(arg_fd), "--print-address=%d", out[1]);
        execlp("dbus-daemon", "dbus-daemon", arg_config, "--nofork", "--nosyslog", arg_fd, (char *) NULL);
        _exit(127);
    }
    close(out[1]);
    ssize_t n = read(out[0], bus_address, sizeof(bus_address) - 1);
    close(out[0]);
    if (n <= 0) { fprintf(stderr, "dbus-daemon did not start\n"); exit(2); }
    bus_address[n] = '\0';
    bus_address[strcspn(bus_address, "\n")] = '\0';
    return pid;
}

int main(void) {
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/riot-host-open-url-XXXXXX");
    if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 2; }
    char userdir[96];
    snprintf(userdir, sizeof(userdir), "%s/%u", tmpdir, getuid());
    mkdir(userdir, 0700);
    snprintf(records, sizeof(records), "%s/records", tmpdir);
    clear_records();

    main_pid = getpid();
    atexit(stop_daemon);
    daemon_pid = start_daemon();
    setenv("DBUS_SESSION_BUS_ADDRESS", bus_address, 1);
    setenv("DISPLAY", ":97", 1);
    setenv("WAYLAND_DISPLAY", "wayland-test", 1);
    unsetenv("XDG_CURRENT_DESKTOP");

    /* Portal available: OpenURI gets the exact URL, systemd is not used. */
    pid_t fake = start_fake(OWN_PORTAL | OWN_SYSTEMD);
    check_portal("https://example.invalid/path?q=1&x=%20y#frag");
    check_portal("HTTP://Example.invalid/");
    check_portal("mailto:someone@example.invalid?subject=Hi%20there");

    /* Rejected URLs never reach the bus. */
    char long_url[LS_HOST_OPEN_MAX_URL_LENGTH + 64];
    memset(long_url, 'a', sizeof(long_url) - 1);
    memcpy(long_url, "https://", 8);
    long_url[sizeof(long_url) - 1] = '\0';
    check_rejected("file:///etc/passwd", "file scheme");
    check_rejected("javascript:alert(1)", "javascript scheme");
    check_rejected("x-darling-test://open", "custom scheme");
    check_rejected("httpx://example.invalid/", "scheme prefix");
    check_rejected("http//example.invalid/", "missing colon");
    check_rejected("", "empty URL");
    check_rejected("https://example.invalid/\nX-Injected: 1", "newline");
    check_rejected("https://example.invalid/\t", "tab");
    check_rejected("https://example.invalid/\x7f", "DEL");
    check_rejected(long_url, "overlong URL");
    {
        struct ls_host_api api = native_api();
        int before = bus_new_calls;
        CHECK(_LSHostOpenURL(&api, NULL, NULL) == -EINVAL && bus_new_calls == before, "NULL URL");
    }
    stop_fake(fake);

    /* Portal name absent: transient systemd unit running [xdg-open, url]. */
    fake = start_fake(OWN_SYSTEMD);
    check_systemd("https://example.invalid/fallback?a=1&b=2", "fallback, portal absent");
    check_systemd("mailto:someone@example.invalid", "fallback mailto");
    stop_fake(fake);

    /* Portal returns an error: fall back as well. */
    fake = start_fake(OWN_PORTAL | OWN_SYSTEMD | PORTAL_FAIL);
    check_systemd("https://example.invalid/portal-error", "fallback, portal error");
    stop_fake(fake);

    /* Portal hangs: report the timeout, do not open the URL a second time. */
    fake = start_fake(OWN_PORTAL | OWN_SYSTEMD | PORTAL_HANG);
    {
        struct ls_host_api api = native_api();
        api.timeout_usec = 300000;
        clear_records();
        int r = _LSHostOpenURL(&api, "https://example.invalid/slow", NULL);
        char *rec = read_records();
        CHECK(r == -ETIMEDOUT, "portal timeout: r=%d", r);
        CHECK(strstr(rec, "OpenURI\t") && !strstr(rec, "StartTransientUnit"), "portal timeout fell back:\n%s", rec);
    }
    stop_fake(fake);

    /* Neither service present: an error, no crash. */
    fake = start_fake(0);
    {
        struct ls_host_api api = native_api();
        int r = _LSHostOpenURL(&api, "https://example.invalid/nobody", NULL);
        CHECK(r < 0, "no services: r=%d", r);
    }
    stop_fake(fake);

    /* DBUS_SESSION_BUS_ADDRESS unset: <runtime root>/<uid>/bus is used. */
    fake = start_fake(OWN_PORTAL);
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    check_portal("https://example.invalid/runtime-dir");
    stop_fake(fake);

    /* Missing or unusable bus: an error, no crash. */
    {
        struct ls_host_api api = native_api();
        char missing[160];
        snprintf(missing, sizeof(missing), "unix:path=%s/missing/bus", tmpdir);
        setenv("DBUS_SESSION_BUS_ADDRESS", missing, 1);
        int r1 = _LSHostOpenURL(&api, "https://example.invalid/", NULL);
        setenv("DBUS_SESSION_BUS_ADDRESS", "not-an-address", 1);
        int r2 = _LSHostOpenURL(&api, "https://example.invalid/", NULL);
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
        api.runtime_dir_root = missing;
        int r3 = _LSHostOpenURL(&api, "https://example.invalid/", NULL);
        CHECK(r1 < 0 && r2 < 0 && r3 < 0, "missing bus: r=%d,%d,%d", r1, r2, r3);
    }

    /* libsystemd unavailable or incomplete: an error, no crash. */
    {
        struct ls_host_api api = native_api();
        CHECK(_LSHostOpenURL(NULL, "https://example.invalid/", NULL) == -ENOSYS, "NULL API");
        api.sd_bus_call = NULL;
        CHECK(_LSHostOpenURL(&api, "https://example.invalid/", NULL) == -ENOSYS, "incomplete API");
    }

    stop_daemon();
    unlink(records);
    char path[160];
    snprintf(path, sizeof(path), "%s/bus.conf", tmpdir); unlink(path);
    snprintf(path, sizeof(path), "%s/bus", userdir); unlink(path);
    rmdir(userdir);
    rmdir(tmpdir);

    printf("HOST_OPEN_URL %s checks=%d failures=%d\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
