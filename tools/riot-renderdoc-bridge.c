/* Native ELF capture control; loaded only by explicit runtime diagnostics. */
#include <renderdoc_app.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
static RENDERDOC_API_1_6_0 *capture_api;
static void *capture_device;
static int active;
/* A request arms the capture; it then starts at the first submit after a presenting one, so a capture holds one whole
 * frame (first submit after a present .. the next present) instead of the tail of a frame. */
static int armed, frame_boundary;
static double next_poll;
static const char *request_path = "@PROJECT_ROOT@/build/renderdoc/capture-request";
__attribute__((constructor)) static void init_capture(void) {
 void *lib = dlopen("@PROJECT_ROOT@/build/renderdoc/usr/lib/librenderdoc.so", RTLD_NOW | RTLD_LOCAL);
 pRENDERDOC_GetAPI get_api = lib ? (pRENDERDOC_GetAPI)dlsym(lib, "RENDERDOC_GetAPI") : NULL;
 if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0, (void **)&capture_api)) return;
 capture_api->MaskOverlayBits(0, 0);
 capture_api->SetCaptureFilePathTemplate("@PROJECT_ROOT@/build/renderdoc/captures/game");
}
static void start_armed_capture(void *instance);
void riot_capture_begin(void *instance) {
 if (!capture_api || active) return;
 if (armed) { start_armed_capture(instance); return; }
 struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
 double seconds = now.tv_sec + now.tv_nsec / 1e9;
 if (seconds < next_poll && !getenv("RCLOL_CAPTURE_ONE_SUBMIT")) return;
 next_poll = seconds + 0.25;
 FILE *request = fopen(request_path, "r"); if (!request) return;
 int pid = -1; int parsed = fscanf(request, "%d", &pid); fclose(request);
 if (parsed != 1 || (pid != 0 && pid != getpid())) return;
 unlink(request_path);
 armed = 1;
 if (getenv("RCLOL_CAPTURE_ONE_SUBMIT")) frame_boundary = 1;
 return;
}
static void start_armed_capture(void *instance) {
 if (!armed || !frame_boundary) return;
 armed = 0;
 capture_device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance);
 capture_api->StartFrameCapture(capture_device, NULL);
 active = capture_api->IsFrameCapturing();
}
void riot_capture_end(int presented) {
 if (!capture_api) return;
 if (!active) { frame_boundary = presented || getenv("RCLOL_CAPTURE_ONE_SUBMIT") != NULL; return; }
 if (!presented && !getenv("RCLOL_CAPTURE_ONE_SUBMIT")) return;
 unsigned result = capture_api->EndFrameCapture(capture_device, NULL);
 FILE *log = fopen("@PROJECT_ROOT@/build/renderdoc/capture-status.log", "a");
 if (log) { fprintf(log, "pid=%d capture_result=%u\n", getpid(), result); fclose(log); }
 active = 0;
}
