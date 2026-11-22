/*
 * FLOPPY FIGHTERS - PS3 front-end (PSL1GHT homebrew)
 *
 * !! STATUS: NOT COMPILED, NOT RUN ON HARDWARE !!
 * This file was written against the PSL1GHT API from memory in a sandbox that
 * could not reach the PS3 toolchain. The game itself (game.c) is tested on
 * desktop; this thin layer only does: set up a double-buffered 32-bit RSX
 * framebuffer, read the pad, call game_update()/game_render(), flip.
 * If the compiler complains, compare against the framebuffer / pad samples that
 * ship with PSL1GHT (samples/graphics, samples/input) - the fixes should be
 * one-liners (a header name or a function signature).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-lv2.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>

#include "game.h"

SYS_PROCESS_PARAM(1001, 0x100000)

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

static gcmContextData *context;
static videoResolution  vres;
static uint32_t *fbuf[2];
static uint32_t  foffset[2];
static int       fb_w, fb_h, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0; /* PS button -> Quit Game */
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

    /* prefer 720p (the game is authored for 1280x720); otherwise use whatever the TV is on */
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

/* Stick drift handling.
 *  1. For the first ~0.75 s the resting position of the left stick is measured; if it sat
 *     still (not being pushed) that reading becomes the new centre, so a worn stick that rests
 *     off-centre no longer walks the stickman by itself.
 *  2. A generous dead zone, and everything past it is rescaled to 0..1 so there is no jump
 *     when the stick leaves the dead zone.
 * The D-pad is not affected by any of this and always works as a clean left/right. */
#define STICK_DEADZONE 0.38f
#define CAL_FRAMES     45

static float stick_centre = 128.0f;
static int   cal_frames = 0, cal_min = 255, cal_max = 0;
static float cal_sum = 0;

static float stick_axis(int raw)
{
    if (cal_frames < CAL_FRAMES) {
        if (raw < cal_min) cal_min = raw;
        if (raw > cal_max) cal_max = raw;
        cal_sum += (float)raw;
        if (++cal_frames == CAL_FRAMES) {
            float avg = cal_sum / (float)CAL_FRAMES;
            /* accept only if the stick was held still and is plausibly resting */
            if (cal_max - cal_min <= 8 && avg > 128.0f - 70.0f && avg < 128.0f + 70.0f)
                stick_centre = avg;
        }
    }
    /* each side is scaled by its own travel, so full push still reaches 1.0 even if the
     * centre was shifted by calibration */
    float d = (float)raw - stick_centre;
    float a = d >= 0 ? d / (255.0f - stick_centre) : d / stick_centre;
    float m = a < 0 ? -a : a;
    if (m < STICK_DEADZONE) return 0;
    m = (m - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    if (m > 1.0f) m = 1.0f;
    return a < 0 ? -m : m;
}

static void read_pad(Input *in)
{
    padInfo info;
    padData data;
    in->move = 0; in->held = 0;
    ioPadGetInfo(&info);
    for (int i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        ioPadGetData(i, &data);

        float lx = stick_axis((int)data.ANA_L_H); /* left stick, 0..255 */
        float dx = 0;
        if (data.BTN_LEFT)  dx -= 1;
        if (data.BTN_RIGHT) dx += 1;
        in->move = dx != 0 ? dx : lx;

        if (data.BTN_SQUARE)   in->held |= BTN_PUNCH;
        if (data.BTN_TRIANGLE) in->held |= BTN_KICK;
        if (data.BTN_CROSS)    in->held |= BTN_JUMP;
        if (data.BTN_CIRCLE)   in->held |= BTN_FLOP;
        if (data.BTN_START)    in->held |= BTN_START;
        break; /* first connected pad only */
    }
}

int main(int argc, const char *argv[])
{
    (void)argc; (void)argv;
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    ioPadInit(7);
    if (init_video() != 0) return 1;

    /* render into ordinary RAM, then copy to the RSX buffer: writing pixel-by-pixel
     * straight into video memory from the PPU is much slower */
    uint32_t *back = (uint32_t *)memalign(128, (size_t)fb_w * fb_h * sizeof(uint32_t));
    if (!back) return 2;

    game_init(0xF10BB1u);
    Input in;
    int cur = 0;
    while (running) {
        sysUtilCheckCallback();
        read_pad(&in);
        game_update(&in, 1.0f / 60.0f); /* vsync-locked */
        game_render(back, fb_w, fb_h, fb_w);

        if (!first_flip) wait_flip(); /* previous flip finished -> fbuf[cur] is off-screen */
        memcpy(fbuf[cur], back, (size_t)fb_w * fb_h * sizeof(uint32_t));
        gcmSetFlip(context, cur);
        rsxFlushBuffer(context);
        gcmSetWaitFlip(context);
        first_flip = 0;
        cur ^= 1;
    }

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    gcmSetWaitFlip(context);
    ioPadEnd();
    return 0;
}
