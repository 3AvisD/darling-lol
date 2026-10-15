/* Host-side regression for iokitd's logind sleep bridge (LogindSleep.c).
 *
 * Runs a private dbus-daemon as the "system bus" (DBUS_SYSTEM_BUS_ADDRESS)
 * and a fake org.freedesktop.login1 forked from this process. The fake hands
 * out a pipe for every Inhibit call and records when its write end is closed,
 * i.e. when the lock is released. The bridge is linked natively against
 * libsystemd. The real system bus and logind are never used.
 *
 *   cc -std=gnu11 -O2 -Wall -Wextra -pthread \
 *     -I build/darling-src/src/external/iokitd/src \
 *     tools/riot-logind-sleep-check.c \
 *     build/darling-src/src/external/iokitd/src/LogindSleep.c \
 *     $(pkg-config --cflags --libs libsystemd) -o build/riot-logind-sleep-check
 *   build/riot-logind-sleep-check
 *
 * With --fake-logind DIR it only runs the private bus and the fake logind for
 * a guest test: the bus address is written to DIR/address, commands are read
 * from the FIFO DIR/control ("S" PrepareForSleep(true), "W" PrepareForSleep(false),
 * "Q" quit) and events are appended to DIR/records.
 */
#define _GNU_SOURCE
#include "LogindSleep.h"
#include "HostPowerInhibit.h"
#include <systemd/sd-bus.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char tmpdir[256], records[300], bus_address[512];
static int failures, checks;
static pid_t daemon_pid, fake_pid, main_pid;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); } } while (0)

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;
}

static void sleep_ms(unsigned ms) {
    struct timespec ts = { ms / 1000, (long) (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

/* ---- fake logind (child process) ---- */

#define MAX_LOCKS 64
static FILE *record_file;
static int lock_fds[MAX_LOCKS], lock_ids[MAX_LOCKS], lock_count, next_lock_id = 1;
static uint64_t sleep_started_ms;
static int fake_sleeping;
static int next_screen_cookie = 1, screen_denied;
static int screen_cookies[MAX_LOCKS];

static void record(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void record(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(record_file, format, ap);
    va_end(ap);
    fputc('\n', record_file);
    fflush(record_file);
}

static int logind_handler(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    (void) userdata; (void) error;
    if (sd_bus_message_is_method_call(m, "org.freedesktop.login1.Manager", "Inhibit")) {
        const char *what, *who, *why, *mode;
        int p[2];
        if (sd_bus_message_read(m, "ssss", &what, &who, &why, &mode) < 0)
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad Inhibit arguments");
        if (lock_count == MAX_LOCKS || pipe2(p, O_CLOEXEC) < 0)
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_NO_MEMORY, "no more locks");
        int id = next_lock_id++;
        lock_fds[lock_count] = p[0];
        lock_ids[lock_count++] = id;
        record("Inhibit\t%d\t%s\t%s\t%s\t%s", id, what, who, why, mode);
        int r = sd_bus_reply_method_return(m, "h", p[1]);
        close(p[1]);   /* the reply carried its own copy */
        return r;
    }
    if (sd_bus_message_is_method_call(m, "org.freedesktop.ScreenSaver", "Inhibit")) {
        const char *app, *reason;
        if (screen_denied) {
            record("ScreenDenied");
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_ACCESS_DENIED, "inhibition denied");
        }
        if (sd_bus_message_read(m, "ss", &app, &reason) < 0 || next_screen_cookie >= MAX_LOCKS)
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad screen inhibit");
        int cookie = next_screen_cookie++;
        screen_cookies[cookie] = 1;
        record("ScreenInhibit\t%d\t%s\t%s", cookie, app, reason);
        return sd_bus_reply_method_return(m, "u", (uint32_t)cookie);
    }
    if (sd_bus_message_is_method_call(m, "org.freedesktop.ScreenSaver", "UnInhibit")) {
        uint32_t cookie;
        if (sd_bus_message_read(m, "u", &cookie) < 0 || cookie >= MAX_LOCKS || !screen_cookies[cookie])
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "unknown screen cookie");
        screen_cookies[cookie] = 0;
        record("ScreenRelease\t%u", cookie);
        return sd_bus_reply_method_return(m, "");
    }
    if (sd_bus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")) {
        const char *iface, *name;
        if (sd_bus_message_read(m, "ss", &iface, &name) < 0)
            return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad Get arguments");
        record("Get\t%s\t%s", iface, name);
        if (!strcmp(iface, "org.freedesktop.login1.Manager") && !strcmp(name, "InhibitDelayMaxUSec"))
            return sd_bus_reply_method_return(m, "v", "t", (uint64_t) 5000000);
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_UNKNOWN_PROPERTY, "unknown property");
    }
    return 0;
}

static void emit_prepare(sd_bus *bus, int start) {
    if (start) {
        sleep_started_ms = now_ms();
        fake_sleeping = 1;
    } else {
        fake_sleeping = 0;
    }
    record(start ? "Sleep" : "Wake");
    sd_bus_emit_signal(bus, "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                       "PrepareForSleep", "b", start);
    sd_bus_flush(bus);
}

/* Serves until "Q" arrives on control_fd or the control pipe closes. */
static void run_fake(int control_fd, int ready_fd) {
    sd_bus *bus = NULL;
    if (sd_bus_new(&bus) < 0 || sd_bus_set_address(bus, bus_address) < 0 ||
        sd_bus_set_bus_client(bus, 1) < 0 || sd_bus_start(bus) < 0)
        _exit(3);
    sd_bus_add_object(bus, NULL, "/org/freedesktop/login1", logind_handler, NULL);
    sd_bus_add_object(bus, NULL, "/org/freedesktop/ScreenSaver", logind_handler, NULL);
    if (sd_bus_request_name(bus, "org.freedesktop.ScreenSaver", 0) < 0)
        _exit(4);
    if (sd_bus_request_name(bus, "org.freedesktop.login1", 0) < 0)
        _exit(4);
    if (write(ready_fd, "R", 1) != 1)
        _exit(5);
    close(ready_fd);

    for (;;) {
        while (sd_bus_process(bus, NULL) > 0) {}
        struct pollfd fds[2 + MAX_LOCKS];
        int n = 0;
        fds[n].fd = sd_bus_get_fd(bus);
        fds[n++].events = (short) sd_bus_get_events(bus);
        fds[n].fd = control_fd;
        fds[n++].events = POLLIN;
        for (int i = 0; i < lock_count; i++) {
            fds[n].fd = lock_fds[i];
            fds[n++].events = POLLIN;
        }
        if (poll(fds, n, 1000) < 0 && errno != EINTR)
            _exit(6);
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            char c;
            ssize_t got = read(control_fd, &c, 1);
            if (got <= 0 || c == 'Q')
                break;
            if (c == 'S' || c == 'W')
                emit_prepare(bus, c == 'S');
            if (c == 'D' || c == 'A') { screen_denied = c == 'D'; record("ScreenDeny\t%d", screen_denied); }
        }
        for (int i = 0; i < lock_count; i++) {
            if (!(fds[2 + i].revents & (POLLHUP | POLLERR)))
                continue;
            long delay = fake_sleeping ? (long) (now_ms() - sleep_started_ms) : -1;
            record("Released\t%d\t%ld", lock_ids[i], delay);
            close(lock_fds[i]);
            lock_fds[i] = -1;
        }
        int kept = 0;
        for (int i = 0; i < lock_count; i++) {
            if (lock_fds[i] >= 0) {
                lock_fds[kept] = lock_fds[i];
                lock_ids[kept++] = lock_ids[i];
            }
        }
        lock_count = kept;
    }
    sd_bus_flush_close_unref(bus);
    _exit(0);
}

static pid_t start_fake(int control_fd) {
    int ready[2];
    if (pipe(ready)) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        close(ready[0]);
        record_file = fopen(records, "a");
        if (!record_file) _exit(2);
        run_fake(control_fd, ready[1]);
    }
    close(ready[1]);
    char c = 0;
    if (read(ready[0], &c, 1) != 1 || c != 'R') { fprintf(stderr, "fake logind failed\n"); exit(2); }
    close(ready[0]);
    return pid;
}

static pid_t start_daemon(void) {
    char config[300], conf[2048];
    snprintf(config, sizeof(config), "%s/bus.conf", tmpdir);
    snprintf(conf, sizeof(conf),
             "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
             " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
             "<busconfig><type>system</type><listen>unix:path=%s/system_bus_socket</listen>"
             "<auth>EXTERNAL</auth><policy context=\"default\"><allow send_destination=\"*\"/>"
             "<allow receive_sender=\"*\"/><allow own=\"*\"/></policy></busconfig>\n", tmpdir);
    FILE *f = fopen(config, "w");
    if (!f || fputs(conf, f) < 0 || fclose(f)) { perror(config); exit(2); }

    int out[2];
    if (pipe(out)) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid == 0) {
        char arg_config[320], arg_fd[32];
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

static void stop_children(void) {
    if (getpid() != main_pid)
        return;
    if (fake_pid > 0) { kill(fake_pid, SIGTERM); waitpid(fake_pid, NULL, 0); fake_pid = 0; }
    if (daemon_pid > 0) { kill(daemon_pid, SIGTERM); waitpid(daemon_pid, NULL, 0); daemon_pid = 0; }
}

/* ---- client side ---- */

static atomic_int will_sleep_calls, did_wake_calls;
static atomic_ullong last_sleep_cycle, last_wake_cycle;

static void on_will_sleep(void *context, uint64_t cycle) {
    (void) context;
    last_sleep_cycle = cycle;
    will_sleep_calls++;
}

static void on_did_wake(void *context, uint64_t cycle) {
    (void) context;
    last_wake_cycle = cycle;
    did_wake_calls++;
}

static const struct iokitd_logind_callbacks callbacks = { on_will_sleep, on_did_wake };

/* Force an already-subscribed sleep signal to dispatch inside the property
 * lookup, before startup has assigned the timeout returned by that lookup. */
static iokitd_bus_handler startup_handler;
static void *startup_context;
static int capture_sleep_match(void *bus, void **slot, const char *sender, const char *path,
                               const char *interface, const char *member,
                               iokitd_bus_handler handler, void *context) {
    if (!strcmp(member, "PrepareForSleep")) {
        startup_handler = handler;
        startup_context = context;
    }
    return sd_bus_match_signal(bus, (sd_bus_slot **)slot, sender, path, interface, member,
                               (sd_bus_message_handler_t)handler, context);
}
static int interleaved_property(void *bus, const char *destination, const char *path,
                               const char *interface, const char *member,
                               struct iokitd_bus_error *error, char type, void *result) {
    int r = sd_bus_get_property_trivial(bus, destination, path, interface, member,
                                       (sd_bus_error *)error, type, result);
    sd_bus_message *message = NULL;
    if (sd_bus_message_new_signal(bus, &message, "/org/freedesktop/login1",
                                 "org.freedesktop.login1.Manager", "PrepareForSleep") >= 0 &&
        sd_bus_message_append(message, "b", 1) >= 0 &&
        sd_bus_message_seal(message, 1, 0) >= 0 && sd_bus_message_rewind(message, 1) >= 0)
        startup_handler(message, startup_context, NULL);
    sd_bus_message_unref(message);
    return r;
}

static struct iokitd_logind_api native_api(uint64_t ack_timeout_usec) {
    struct iokitd_logind_api api;
    memset(&api, 0, sizeof(api));
#define SET(f) api.f = (__typeof__(api.f)) f
    SET(sd_bus_new); SET(sd_bus_set_address); SET(sd_bus_set_bus_client); SET(sd_bus_start); SET(sd_bus_set_method_call_timeout);
    SET(sd_bus_flush_close_unref); SET(sd_bus_match_signal); SET(sd_bus_call_method);
    SET(sd_bus_get_property_trivial); SET(sd_bus_message_read); SET(sd_bus_message_unref);
    SET(sd_bus_slot_unref); SET(sd_bus_error_free); SET(sd_bus_process); SET(sd_bus_wait);
    SET(fcntl); SET(close); SET(getenv);
#undef SET
    api.ack_timeout_usec = ack_timeout_usec;
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

static int count_prefix(const char *prefix) {
    int n = 0;
    size_t len = strlen(prefix);
    for (const char *line = read_records(); *line; ) {
        if (!strncmp(line, prefix, len)) n++;
        const char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    return n;
}

/* Delay recorded for the release of lock `id`, -2 if not released. */
static long released_delay(int id) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "Released\t%d\t", id);
    const char *p = strstr(read_records(), prefix);
    return p ? strtol(p + strlen(prefix), NULL, 10) : -2;
}

static int wait_until(int (*cond)(void *), void *arg, unsigned timeout_ms) {
    uint64_t end = now_ms() + timeout_ms;
    while (!cond(arg)) {
        if (now_ms() > end) return 0;
        sleep_ms(10);
    }
    return 1;
}

struct prefix_count { const char *prefix; int count; };
static int prefix_reached(void *arg) {
    struct prefix_count *pc = arg;
    return count_prefix(pc->prefix) >= pc->count;
}
static int not_holding(void *arg) { return !iokitd_logind_holds_inhibitor(arg); }
static int released(void *arg) { return released_delay(*(int *) arg) != -2; }
static int counter_reached(void *arg) {
    struct { atomic_int *counter; int value; } *c = arg;
    return atomic_load(c->counter) >= c->value;
}

static int wait_prefix(const char *prefix, int count, unsigned timeout_ms) {
    struct prefix_count pc = { prefix, count };
    return wait_until(prefix_reached, &pc, timeout_ms);
}

static int wait_counter(atomic_int *counter, int value, unsigned timeout_ms) {
    struct { atomic_int *counter; int value; } c = { counter, value };
    return wait_until(counter_reached, &c, timeout_ms);
}

static int wait_released(int id, unsigned timeout_ms) {
    return wait_until(released, &id, timeout_ms);
}

static void command(int control_fd, char c) {
    if (write(control_fd, &c, 1) != 1) { perror("control"); exit(2); }
}

static int serve(const char *dir) {
    char path[300];
    snprintf(tmpdir, sizeof(tmpdir), "%s", dir);
    snprintf(records, sizeof(records), "%s/records", dir);
    fclose(fopen(records, "w"));
    main_pid = getpid();
    atexit(stop_children);
    signal(SIGTERM, exit);
    signal(SIGINT, exit);
    daemon_pid = start_daemon();

    snprintf(path, sizeof(path), "%s/control", dir);
    unlink(path);
    if (mkfifo(path, 0600) < 0) { perror(path); return 2; }
    /* O_RDWR keeps the FIFO open between writers. */
    int control = open(path, O_RDWR);
    if (control < 0) { perror(path); return 2; }
    fake_pid = start_fake(control);

    snprintf(path, sizeof(path), "%s/address", dir);
    FILE *f = fopen(path, "w");
    if (!f || fprintf(f, "%s\n", bus_address) < 0 || fclose(f)) { perror(path); return 2; }
    printf("FAKE_LOGIND address=%s pid=%d daemon=%d\n", bus_address, (int) getpid(), (int) daemon_pid);
    fflush(stdout);

    int status = 0;
    waitpid(fake_pid, &status, 0);
    fake_pid = 0;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--fake-logind"))
        return serve(argv[2]);

    const char *temporary = getenv("TMPDIR");
    snprintf(tmpdir, sizeof(tmpdir), "%s/riot-logind-sleep-XXXXXX", temporary ? temporary : "/tmp");
    if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 2; }
    snprintf(records, sizeof(records), "%s/records", tmpdir);
    fclose(fopen(records, "w"));

    main_pid = getpid();
    atexit(stop_children);
    daemon_pid = start_daemon();
    struct iokitd_logind *l = NULL;
    int r;

    /* Incomplete host api: nothing is attempted. */
    {
        struct iokitd_logind_api api = native_api(0);
        api.sd_bus_wait = NULL;
        r = iokitd_logind_start(&api, &callbacks, NULL, &l);
        CHECK(r == -ENOSYS && l == NULL, "incomplete api: r=%d", r);
    }

    /* No system bus: an error, no crash. */
    {
        char missing[400];
        snprintf(missing, sizeof(missing), "unix:path=%s/no-such-socket", tmpdir);
        setenv("DBUS_SYSTEM_BUS_ADDRESS", missing, 1);
        struct iokitd_logind_api api = native_api(0);
        r = iokitd_logind_start(&api, &callbacks, NULL, &l);
        CHECK(r < 0 && l == NULL, "missing bus: r=%d", r);
    }

    setenv("DBUS_SYSTEM_BUS_ADDRESS", bus_address, 1);

    /* Bus without logind: starts, holds no lock, no events. */
    {
        struct iokitd_logind_api api = native_api(0);
        r = iokitd_logind_start(&api, &callbacks, NULL, &l);
        CHECK(r == 0 && l != NULL, "bus without logind: r=%d", r);
        CHECK(!iokitd_logind_holds_inhibitor(l), "bus without logind: holds a lock");
        CHECK(iokitd_logind_ack_timeout_usec(l) == 4000000,
              "default ack timeout without logind: %llu", (unsigned long long) iokitd_logind_ack_timeout_usec(l));
        iokitd_logind_stop(l);
        l = NULL;
    }

    /* The fake is started before any lock exists, so it inherits none. */
    int control[2];
    if (pipe(control)) { perror("pipe"); return 2; }
    fake_pid = start_fake(control[0]);
    close(control[0]);

    /* Start: one delay lock, timeout derived from InhibitDelayMaxUSec (5 s) minus 1 s. */
    struct iokitd_logind_api api = native_api(0);
    r = iokitd_logind_start(&api, &callbacks, NULL, &l);
    CHECK(r == 0 && l != NULL, "start: r=%d", r);
    CHECK(wait_prefix("Inhibit\t", 1, 2000), "start: no Inhibit call:\n%s", read_records());
    CHECK(strstr(read_records(), "Inhibit\t1\tsleep\tDarling\t") != NULL &&
          strstr(read_records(), "\tdelay\n") != NULL, "start: wrong Inhibit arguments:\n%s", read_records());
    CHECK(strstr(read_records(), "Get\torg.freedesktop.login1.Manager\tInhibitDelayMaxUSec") != NULL,
          "start: InhibitDelayMaxUSec not read");
    CHECK(iokitd_logind_ack_timeout_usec(l) == 4000000, "derived ack timeout %llu",
          (unsigned long long) iokitd_logind_ack_timeout_usec(l));
    CHECK(iokitd_logind_holds_inhibitor(l), "start: lock not held");
    CHECK(released_delay(1) == -2, "start: lock 1 released early");

    /* Sleep, then acknowledge: the lock is held until the acknowledgement. */
    command(control[1], 'S');
    CHECK(wait_counter(&will_sleep_calls, 1, 2000) && last_sleep_cycle == 1, "sleep 1: will_sleep %d cycle %llu",
          atomic_load(&will_sleep_calls), (unsigned long long) last_sleep_cycle);
    sleep_ms(500);
    CHECK(released_delay(1) == -2 && iokitd_logind_holds_inhibitor(l), "sleep 1: released before acknowledgement");
    iokitd_logind_sleep_ready(l, 1);
    CHECK(wait_released(1, 2000), "sleep 1: not released after acknowledgement");
    long delay = released_delay(1);
    CHECK(delay >= 450 && delay < 2000, "sleep 1: released %ld ms after PrepareForSleep(true)", delay);
    CHECK(!iokitd_logind_holds_inhibitor(l), "sleep 1: still holds lock");

    /* Resume: did_wake, a new lock; a stale acknowledgement does not release it. */
    command(control[1], 'W');
    CHECK(wait_counter(&did_wake_calls, 1, 2000) && last_wake_cycle == 1, "wake 1: did_wake %d",
          atomic_load(&did_wake_calls));
    CHECK(wait_prefix("Inhibit\t", 2, 2000), "wake 1: lock not re-taken:\n%s", read_records());
    CHECK(wait_until((int (*)(void *)) iokitd_logind_holds_inhibitor, l, 1000), "wake 1: lock not held");
    iokitd_logind_sleep_ready(l, 1);
    sleep_ms(300);
    CHECK(released_delay(2) == -2 && iokitd_logind_holds_inhibitor(l), "stale acknowledgement released lock 2");

    /* logind suspended without waiting (no acknowledgement before resume): keep the same lock. */
    command(control[1], 'S');
    CHECK(wait_counter(&will_sleep_calls, 2, 2000) && last_sleep_cycle == 2, "sleep 2: will_sleep");
    command(control[1], 'W');
    CHECK(wait_counter(&did_wake_calls, 2, 2000) && last_wake_cycle == 2, "wake 2: did_wake");
    sleep_ms(300);
    CHECK(count_prefix("Inhibit\t") == 2 && released_delay(2) == -2 && iokitd_logind_holds_inhibitor(l),
          "wake 2: lock replaced or released:\n%s", read_records());
    iokitd_logind_sleep_ready(l, 2);
    sleep_ms(200);
    CHECK(released_delay(2) == -2, "acknowledgement after resume released the lock");

    /* Stop releases the lock. */
    iokitd_logind_stop(l);
    l = NULL;
    CHECK(wait_released(2, 2000) && released_delay(2) == -1, "stop: lock 2 not released");

    /* Nobody acknowledges: the lock is released at the timeout, not before. */
    api = native_api(700000);
    r = iokitd_logind_start(&api, &callbacks, NULL, &l);
    CHECK(r == 0 && iokitd_logind_ack_timeout_usec(l) == 700000, "timeout bridge: r=%d", r);
    CHECK(wait_prefix("Inhibit\t", 3, 2000), "timeout bridge: no lock");
    int sleeps = atomic_load(&will_sleep_calls);
    command(control[1], 'S');
    CHECK(wait_counter(&will_sleep_calls, sleeps + 1, 2000) && last_sleep_cycle == 1, "timeout: will_sleep");
    sleep_ms(350);
    CHECK(released_delay(3) == -2, "timeout: released before the timeout");
    CHECK(wait_released(3, 3000), "timeout: never released");
    delay = released_delay(3);
    CHECK(delay >= 650 && delay < 1500, "timeout: released %ld ms after PrepareForSleep(true)", delay);
    iokitd_logind_sleep_ready(l, 1);   /* late acknowledgement is harmless */
    int wakes = atomic_load(&did_wake_calls);
    command(control[1], 'W');
    CHECK(wait_counter(&did_wake_calls, wakes + 1, 2000), "timeout: did_wake");
    CHECK(wait_prefix("Inhibit\t", 4, 2000), "timeout: lock not re-taken after resume");
    iokitd_logind_stop(l);
    l = NULL;
    CHECK(wait_released(4, 2000), "timeout: lock 4 not released at stop");

    /* Restart logind while a bridge is active: discard the old lock and re-take it. */
    api = native_api(700000);
    r = iokitd_logind_start(&api, &callbacks, NULL, &l);
    CHECK(r == 0 && wait_prefix("Inhibit\t", 5, 2000), "restart: initial lock");
    command(control[1], 'Q');
    close(control[1]);
    waitpid(fake_pid, NULL, 0);
    fake_pid = 0;
    CHECK(wait_until(not_holding, l, 2000), "restart: stale inhibitor still held");
    CHECK(pipe(control) == 0, "restart: control pipe");
    fake_pid = start_fake(control[0]);
    close(control[0]);
    CHECK(wait_prefix("Inhibit\t", 6, 2000), "restart: inhibitor not re-taken");
    CHECK(iokitd_logind_holds_inhibitor(l), "restart: no new inhibitor");
    sleeps = atomic_load(&will_sleep_calls);
    command(control[1], 'S');
    CHECK(wait_counter(&will_sleep_calls, sleeps + 1, 2000), "restart: sleep not received");
    iokitd_logind_sleep_ready(l, last_sleep_cycle);
    CHECK(wait_until(not_holding, l, 1000), "restart: ack did not release inhibitor");
    wakes = atomic_load(&did_wake_calls);
    command(control[1], 'W');
    CHECK(wait_counter(&did_wake_calls, wakes + 1, 2000), "restart: wake not received");
    CHECK(wait_prefix("Inhibit\t", 7, 2000), "restart: inhibitor missing after wake");
    iokitd_logind_stop(l);
    l = NULL;
    /* Startup signal during timeout discovery must not create an expired deadline. */
    api = native_api(0);
    api.sd_bus_match_signal = capture_sleep_match;
    api.sd_bus_get_property_trivial = interleaved_property;
    sleeps = atomic_load(&will_sleep_calls);
    r = iokitd_logind_start(&api, &callbacks, NULL, &l);
    CHECK(r == 0 && l != NULL, "startup interleave: start failed %d", r);
    CHECK(atomic_load(&will_sleep_calls) == sleeps + 1, "startup interleave: signal not dispatched");
    sleep_ms(300);
    CHECK(iokitd_logind_holds_inhibitor(l), "startup interleave: inhibitor released before ack");
    iokitd_logind_sleep_ready(l, last_sleep_cycle);
    CHECK(wait_until(not_holding, l, 1000), "startup interleave: ack did not release inhibitor");
    iokitd_logind_stop(l);
    l = NULL;

    /* A complete system-bus outage must reconnect, not disable monitoring forever. */
    char stable_address[400];
    snprintf(stable_address, sizeof(stable_address), "unix:path=%s/system_bus_socket", tmpdir);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", stable_address, 1);
    api = native_api(700000);
    int expected_locks = count_prefix("Inhibit\t") + 1;
    r = iokitd_logind_start(&api, &callbacks, NULL, &l);
    CHECK(r == 0 && wait_prefix("Inhibit\t", expected_locks, 2000), "bus restart: initial lock");
    command(control[1], 'Q');
    close(control[1]);
    waitpid(fake_pid, NULL, 0);
    fake_pid = 0;
    kill(daemon_pid, SIGTERM);
    waitpid(daemon_pid, NULL, 0);
    daemon_pid = 0;
    CHECK(wait_until(not_holding, l, 2000), "bus restart: stale inhibitor");
    sleep_ms(300);
    daemon_pid = start_daemon();
    CHECK(pipe(control) == 0, "bus restart: control pipe");
    fake_pid = start_fake(control[0]);
    close(control[0]);
    CHECK(wait_prefix("Inhibit\t", expected_locks + 1, 7000), "bus restart: no new inhibitor");
    CHECK(iokitd_logind_holds_inhibitor(l), "bus restart: inhibitor not held");
    sleeps = atomic_load(&will_sleep_calls);
    command(control[1], 'S');
    CHECK(wait_counter(&will_sleep_calls, sleeps + 1, 2000), "bus restart: sleep not delivered");
    iokitd_logind_sleep_ready(l, last_sleep_cycle);
    CHECK(wait_until(not_holding, l, 1000), "bus restart: acknowledgement failed");
    wakes = atomic_load(&did_wake_calls);
    command(control[1], 'W');
    CHECK(wait_counter(&did_wake_calls, wakes + 1, 2000), "bus restart: wake not delivered");
    CHECK(wait_prefix("Inhibit\t", expected_locks + 2, 2000), "bus restart: no lock after wake");
    iokitd_logind_stop(l);
    l = NULL;

    /* Blocking assertions use idle/block, independently from sleep/delay. */
    setenv("DBUS_SYSTEM_BUS_ADDRESS", bus_address, 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", bus_address, 1);
    api = native_api(0);
    struct iokitd_power_inhibit *inhibit = NULL;
    int lock_count_before = count_prefix("Inhibit\t");
    int release_count_before = count_prefix("Released\t");
    r = iokitd_power_inhibit_create(&api, 0, &inhibit);
    CHECK(r == 0 && inhibit, "idle inhibit: create %d", r);
    CHECK(wait_prefix("Inhibit\t", lock_count_before + 1, 1000), "idle inhibit: not called");
    CHECK(strstr(read_records(), "\tidle\tDarling\tmacOS application prevents automatic idle sleep\tblock") != NULL,
          "idle inhibit: wrong arguments");
    iokitd_power_inhibit_destroy(inhibit);
    inhibit = NULL;
    CHECK(wait_prefix("Released\t", release_count_before + 1, 1000), "idle inhibit: not released");

    lock_count_before = count_prefix("Inhibit\t");
    release_count_before = count_prefix("Released\t");
    r = iokitd_power_inhibit_create(&api, 1, &inhibit);
    CHECK(r == 0 && inhibit, "display inhibit: create %d", r);
    CHECK(wait_prefix("ScreenInhibit\t", 1, 1000), "display inhibit: screen not inhibited");
    CHECK(wait_prefix("Inhibit\t", lock_count_before + 1, 1000), "display inhibit: idle not inhibited");
    iokitd_power_inhibit_destroy(inhibit);
    inhibit = NULL;
    CHECK(wait_prefix("ScreenRelease\t", 1, 1000), "display inhibit: cookie not released");
    CHECK(wait_prefix("Released\t", release_count_before + 1, 1000), "display inhibit: idle not released");

    command(control[1], 'D');
    CHECK(wait_prefix("ScreenDeny\t1", 1, 1000), "display denial: fake not ready");
    release_count_before = count_prefix("Released\t");
    r = iokitd_power_inhibit_create(&api, 1, &inhibit);
    CHECK(r < 0 && !inhibit, "display denial: returned success");
    CHECK(wait_prefix("Released\t", release_count_before + 1, 1000), "display denial: leaked idle lock");
    command(control[1], 'A');

    char missing_bus[400];
    snprintf(missing_bus, sizeof(missing_bus), "unix:path=%s/missing", tmpdir);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", missing_bus, 1);
    r = iokitd_power_inhibit_create(&api, 0, &inhibit);
    CHECK(r < 0 && !inhibit, "missing system bus: returned success");
    setenv("DBUS_SYSTEM_BUS_ADDRESS", bus_address, 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", missing_bus, 1);
    release_count_before = count_prefix("Released\t");
    r = iokitd_power_inhibit_create(&api, 1, &inhibit);
    CHECK(r < 0 && !inhibit, "missing session bus: returned success");
    CHECK(wait_prefix("Released\t", release_count_before + 1, 1000), "missing session bus: leaked idle lock");
    api.close = NULL;
    r = iokitd_power_inhibit_create(&api, 0, &inhibit);
    CHECK(r == -ENOSYS && !inhibit, "incomplete inhibitor api: %d", r);
    iokitd_power_inhibit_destroy(NULL);

    command(control[1], 'Q');
    close(control[1]);
    waitpid(fake_pid, NULL, 0);
    fake_pid = 0;
    stop_children();

    char path[400];
    snprintf(path, sizeof(path), "%s/bus.conf", tmpdir); unlink(path);
    snprintf(path, sizeof(path), "%s/system_bus_socket", tmpdir); unlink(path);
    unlink(records);
    rmdir(tmpdir);

    printf("LOGIND_SLEEP %s checks=%d failures=%d\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
