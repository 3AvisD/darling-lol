/* Guest probe for system sleep/wake notifications (iokitd IOPMrootDomain).
 *
 * Registers the way Chromium's power monitor does (IORegisterForSystemPower,
 * then the notification port's run loop source), acknowledges
 * kIOMessageSystemWillSleep with IOAllowPowerChange after ACK_DELAY_MS and
 * waits until kIOMessageSystemHasPoweredOn or TIMEOUT_S. Sleep and wake are
 * triggered from the host (a fake logind, see tools/riot-logind-sleep-check.c).
 *
 *   riot-power-notify-check [ACK_DELAY_MS [TIMEOUT_S]]
 * ACK_DELAY_MS: >=0 acknowledges after that delay; -1 never acknowledges;
 * -2 deregisters on sleep; -3 runs the integration suite's three cycles
 * (delayed acknowledgement, timeout, deregistration).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static io_connect_t root_port;
static io_object_t notifier;
static int deregistered, suite, completed_cycles;
static int ack_delay_ms, failures, step;
static int got_will_sleep, got_will_power_on, got_has_powered_on;
static IOReturn ack_result = -1;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

static double now_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static const char *message_name(natural_t type) {
    switch (type) {
    case kIOMessageCanSystemSleep: return "CanSystemSleep";
    case kIOMessageSystemWillSleep: return "SystemWillSleep";
    case kIOMessageSystemWillNotSleep: return "SystemWillNotSleep";
    case kIOMessageSystemWillPowerOn: return "SystemWillPowerOn";
    case kIOMessageSystemHasPoweredOn: return "SystemHasPoweredOn";
    default: return "other";
    }
}

static void callback(void *refcon, io_service_t service, natural_t type, void *argument) {
    CHECK(refcon == &root_port, "refcon %p", refcon);
    printf("MESSAGE %.3f %s (0x%x) argument=0x%lx service=%s\n", now_s(), message_name(type), type,
           (unsigned long) (uintptr_t) argument, service ? "set" : "null");
    fflush(stdout);
    switch (type) {
    case kIOMessageCanSystemSleep:
        IOAllowPowerChange(root_port, (intptr_t) argument);
        break;
    case kIOMessageSystemWillSleep:
        CHECK(step == 0, "WillSleep out of order (step %d)", step);
        step = 1;
        got_will_sleep++;
        if (ack_delay_ms == -2 || (suite && completed_cycles == 2)) {
            CHECK(IODeregisterForSystemPower(&notifier) == kIOReturnSuccess, "deregister during sleep");
            deregistered = 1;
            CFRunLoopStop(CFRunLoopGetCurrent());
            break;
        }
        if ((ack_delay_ms < 0 && !suite) || (suite && completed_cycles == 1)) break;
        int delay = suite ? 1200 : ack_delay_ms;
        if (delay)
            usleep(delay * 1000);
        ack_result = IOAllowPowerChange(root_port, (intptr_t) argument);
        printf("ACK %.3f IOAllowPowerChange=0x%x\n", now_s(), ack_result);
        fflush(stdout);
        break;
    case kIOMessageSystemWillPowerOn:
        CHECK(argument == NULL, "WillPowerOn argument must be NULL");
        CHECK(step == 1, "WillPowerOn out of order (step %d)", step);
        step = 2;
        got_will_power_on++;
        break;
    case kIOMessageSystemHasPoweredOn:
        CHECK(argument == NULL, "HasPoweredOn argument must be NULL");
        CHECK(step == 2, "HasPoweredOn out of order (step %d)", step);
        completed_cycles++;
        step = suite ? 0 : 3;
        got_has_powered_on++;
        CFRunLoopStop(CFRunLoopGetCurrent());
        break;
    }
}

int main(int argc, char **argv) {
    IONotificationPortRef port = NULL;
    io_object_t dead_notifier = IO_OBJECT_NULL;
    double timeout = 60;

    if (argc > 1) ack_delay_ms = atoi(argv[1]);
    if (argc > 2) timeout = atof(argv[2]);
    suite = ack_delay_ms == -3;

    /* The registry entries IORegisterForSystemPower relies on. */
    io_registry_entry_t entry = IORegistryEntryFromPath(kIOMasterPortDefault,
                                                        kIOPowerPlane ":/IOPowerConnection/IOPMrootDomain");
    io_service_t matched = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPMrootDomain"));
    io_name_t class_name = "";
    if (entry) IOObjectGetClass(entry, class_name);
    CHECK(entry != IO_OBJECT_NULL && !strcmp(class_name, "IOPMrootDomain"), "IOPower path: entry=%u class=%s",
          entry, class_name);
    CHECK(matched != IO_OBJECT_NULL, "IOServiceMatching(IOPMrootDomain) found nothing");
    if (entry) {
        // Failed MIG requests must release the wake send right exactly once.
        IONotificationPortRef rejected = IONotificationPortCreate(kIOMasterPortDefault);
        io_object_t unused = IO_OBJECT_NULL;
        kern_return_t error = IOServiceAddInterestNotification(rejected, entry, "UnsupportedInterest",
                                                               callback, &root_port, &unused);
        CHECK(error != KERN_SUCCESS && unused == IO_OBJECT_NULL, "unsupported interest succeeded");
        mach_port_status_t status;
        mach_msg_type_number_t count = MACH_PORT_RECEIVE_STATUS_COUNT;
        CHECK(mach_port_get_attributes(mach_task_self(), IONotificationPortGetMachPort(rejected),
              MACH_PORT_RECEIVE_STATUS, (mach_port_info_t)&status, &count) == KERN_SUCCESS &&
              !status.mps_srights, "failed registration leaked a wake send right");
        IONotificationPortDestroy(rejected);
        if (suite) {
            // A retained notifier with a destroyed destination must not become
            // an outstanding sleep acknowledgement.
            IONotificationPortRef dead = IONotificationPortCreate(kIOMasterPortDefault);
            CHECK(IOServiceAddInterestNotification(dead, entry, kIOAppPowerStateInterest,
                  callback, &root_port, &dead_notifier) == KERN_SUCCESS, "dead-port fixture registration");
            IONotificationPortDestroy(dead);
        }
    }
    if (entry) IOObjectRelease(entry);
    if (matched) IOObjectRelease(matched);

    root_port = IORegisterForSystemPower(&root_port, &port, callback, &notifier);
    printf("REGISTER root_port=%u port=%s notifier=%u\n", root_port, port ? "set" : "null", notifier);
    CHECK(root_port != IO_OBJECT_NULL && port != NULL && notifier != IO_OBJECT_NULL, "IORegisterForSystemPower failed");
    if (root_port == IO_OBJECT_NULL || port == NULL) {
        /* Repeat the steps of IORegisterForSystemPower to show which one fails. */
        io_service_t domain = IORegistryEntryFromPath(kIOMasterPortDefault,
                                                      kIOPowerPlane ":/IOPowerConnection/IOPMrootDomain");
        io_connect_t connect = IO_OBJECT_NULL;
        IONotificationPortRef diag_port = IONotificationPortCreate(kIOMasterPortDefault);
        kern_return_t open_kr = IOServiceOpen(domain, mach_task_self(), 0, &connect);
        kern_return_t interest_kr = IOServiceAddInterestNotification(diag_port, domain, kIOAppPowerStateInterest,
                                                                     callback, &root_port, &notifier);
        printf("DIAG domain=%u IOServiceOpen=0x%x connect=%u IOServiceAddInterestNotification=0x%x\n",
               domain, open_kr, connect, interest_kr);
        printf("POWER_NOTIFY FAIL failures=%d\n", failures);
        return 1;
    }

    /* Selectors other than allow/cancel are refused cleanly. */
    uint64_t scalar = 0;
    kern_return_t kr = IOConnectCallScalarMethod(root_port, 99, &scalar, 1, NULL, NULL);
    CHECK(kr != KERN_SUCCESS, "unknown root domain selector succeeded");
    /* A stale acknowledgement is accepted and ignored. */
    CHECK(IOAllowPowerChange(root_port, 0x12345) == kIOReturnSuccess, "stale IOAllowPowerChange failed");

    CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopCommonModes);
    if (suite) {
        // Fill the notification queue, then keep it full through the host's first
        // PrepareForSleep. The power event must be retried without losing order.
        int filled = 0;
        for (int i = 0; i < 64; i++) {
            mach_msg_header_t filler = {0};
            filler.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
            filler.msgh_remote_port = IONotificationPortGetMachPort(port);
            filler.msgh_size = sizeof(filler);
            filler.msgh_id = 42;
            kern_return_t sent = mach_msg(&filler, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
                                         sizeof(filler), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
            if (sent == MACH_SEND_TIMED_OUT) break;
            CHECK(sent == KERN_SUCCESS, "cannot fill notification queue: 0x%x", sent);
            if (sent != KERN_SUCCESS) break;
            filled++;
        }
        CHECK(filled > 0, "notification queue not filled");
        printf("CONGESTED queued=%d\n", filled);
    }
    printf("READY %.3f ack_delay_ms=%d\n", now_s(), ack_delay_ms);
    fflush(stdout);

    if (suite) usleep(500000);
    double end = now_s() + timeout;
    while (step < 3 && !deregistered && now_s() < end)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);

    CHECK(got_will_sleep == (suite ? 3 : 1), "WillSleep received %d times", got_will_sleep);
    if (ack_delay_ms >= 0 || suite)
        CHECK(ack_result == kIOReturnSuccess, "IOAllowPowerChange returned 0x%x", ack_result);
    if (!deregistered || suite) {
        CHECK(got_will_power_on == (suite ? 2 : 1), "WillPowerOn received %d times", got_will_power_on);
        CHECK(got_has_powered_on == (suite ? 2 : 1), "HasPoweredOn received %d times", got_has_powered_on);
    }

    CHECK(IODeregisterForSystemPower(&notifier) == kIOReturnSuccess && notifier == IO_OBJECT_NULL,
          "IODeregisterForSystemPower");
    CHECK(IOServiceClose(root_port) == KERN_SUCCESS, "IOServiceClose");
    IONotificationPortDestroy(port);
    if (dead_notifier) IOObjectRelease(dead_notifier);

    printf("POWER_NOTIFY %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
