/*
 * Two-game launcher (PSL1GHT homebrew).
 * Shows a small menu and starts RAGDOLL.SELF or FLOPPY.SELF from the same USRDIR folder
 * with sysProcessExitSpawn2. Status: written against the real PSL1GHT headers and built by CI;
 * never run on a console by me.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-lv2.h>
#include <sys/process.h>
#include <lv2/process.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>

#include "menu.h"

SYS_PROCESS_PARAM(1001, 0x100000)

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)
#define DEFAULT_DIR "/dev_hdd0/game/RAGDOLL01/USRDIR"

static gcmContextData *context;
static videoResolution  vres;
static uint32_t *fbuf[2];
static uint32_t  foffset[2];
static int       fb_w, fb_h, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static int init_video(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    if (!host) return -1;
    rsxInit(&context, CB_SIZE, HOST_SIZE, host);

    videoState state;
    videoGetState(0, 0, &state);
    u32 resolution = state.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0) > 0)
        resolution = VIDEO_RESOLUTION_720;
    if (videoGetResolution(resolution, &vres) != 0) return -2;

    videoConfiguration vconfig;
    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolution = resolution;
    vconfig.format     = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch      = vres.width * sizeof(uint32_t);
    vconfig.aspect     = state.displayMode.aspect;
    if (videoConfigure(0, &vconfig, NULL, 0) != 0) return -3;
    videoGetState(0, 0, &state);

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    fb_w = vres.width; fb_h = vres.height;
    size_t bytes = (size_t)fb_w * fb_h * sizeof(uint32_t);
    for (int i = 0; i < 2; i++) {
        fbuf[i] = (uint32_t *)rsxMemalign(64, bytes);
        if (!fbuf[i]) return -4;
        if (rsxAddressToOffset(fbuf[i], &foffset[i]) != 0) return -5;
        if (gcmSetDisplayBuffer(i, foffset[i], fb_w * sizeof(uint32_t), fb_w, fb_h) != 0) return -6;
        memset(fbuf[i], 0, bytes);
    }
    gcmResetFlipStatus();
    return 0;
}

/* buttons currently held, as MENU_BTN_* bits */
static unsigned read_buttons(void)
{
    padInfo info;
    padData data;
    unsigned held = 0;
    ioPadGetInfo(&info);
    for (int i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        ioPadGetData(i, &data);
        if (data.BTN_UP)    held |= MENU_BTN_UP;
        if (data.BTN_DOWN)  held |= MENU_BTN_DOWN;
        if (data.BTN_CROSS) held |= MENU_BTN_OK;
        if (data.BTN_START) held |= MENU_BTN_OK;
    }
    return held;
}

static void flip_frame(const uint32_t *back, int *cur)
{
    if (!first_flip) wait_flip();
    memcpy(fbuf[*cur], back, (size_t)fb_w * fb_h * sizeof(uint32_t));
    gcmSetFlip(context, *cur);
    rsxFlushBuffer(context);
    gcmSetWaitFlip(context);
    first_flip = 0;
    *cur ^= 1;
}

int main(int argc, const char *argv[])
{
    /* the folder this program was started from; both games sit next to it */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", DEFAULT_DIR);
    if (argc > 0 && argv && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (slash && slash != argv[0] && (size_t)(slash - argv[0]) < sizeof(dir)) {
            memcpy(dir, argv[0], (size_t)(slash - argv[0]));
            dir[slash - argv[0]] = 0;
        }
    }

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    ioPadInit(7);
    if (init_video() != 0) return 1;

    uint32_t *back = (uint32_t *)memalign(128, (size_t)fb_w * fb_h * sizeof(uint32_t));
    if (!back) return 2;

    Menu menu;
    menu_init(&menu);
    unsigned prev = ~0u; /* ignore buttons already held when we start (e.g. Cross from the XMB) */
    int cur = 0, launch_frames = 0;

    while (running) {
        sysUtilCheckCallback();
        unsigned held = read_buttons();
        unsigned pressed = held & ~prev;
        prev = held;

        menu_update(&menu, pressed, 1.0f / 60.0f);
        menu_render(&menu, back, fb_w, fb_h, fb_w);
        flip_frame(back, &cur);

        if (menu.launch >= 0 && ++launch_frames >= 3) { /* let "LOADING..." reach the screen first */
            char path[300];
            snprintf(path, sizeof(path), "%s/%s", dir, menu_target_file(menu.launch));
            FILE *f = fopen(path, "rb");
            if (!f) {
                menu_fail(&menu);
            } else {
                fclose(f);
                const char *args[2] = { path, NULL };
                sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
                ioPadEnd();
                sysProcessExitSpawn2(path, args, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);
                /* only reached if the spawn failed */
                sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
                ioPadInit(7);
                menu_fail(&menu);
            }
            launch_frames = 0;
            prev = ~0u;
        }
    }

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    gcmSetWaitFlip(context);
    ioPadEnd();
    return 0;
}
