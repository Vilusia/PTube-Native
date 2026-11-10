#include "net.h"
#include "player.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

#define SLOTS 8
#define SAMPLES 2944
#define MAX_JPEG 524288
typedef struct {unsigned int size; unsigned char jpeg[MAX_JPEG]; short pcm[SAMPLES*2] __attribute__((aligned(64)));} Frame;
static Frame *frames;
static PtStream stream;
static SceUID empty, full;
static volatile int ended, broken;
static unsigned int le32(const unsigned char *p) {return (unsigned int)p[0] | ((unsigned int)p[1]<<8) | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);}
static int wait_slot(SceUID semaphore) {
    while (!pt_stop) {SceUInt timeout = 100000; if (sceKernelWaitSemaCB(semaphore, 1, &timeout) == 0) return 0;}
    return -1;
}
static int reader(SceSize n, void *p) {
    (void)n; (void)p;
    int index = 0;
    while (!pt_stop) {
        if (wait_slot(empty) < 0) break;
        unsigned char length[4];
        if (pt_read(&stream, length, 4) < 0) {broken = 1; break;}
        Frame *f = &frames[index]; f->size = le32(length);
        if (!f->size) {ended = 1; break;}
        if (f->size > MAX_JPEG || pt_read(&stream, f->jpeg, f->size) < 0 || pt_read(&stream, f->pcm, sizeof(f->pcm)) < 0) {broken = 1; break;}
        sceKernelSignalSema(full, 1); index = (index + 1) % SLOTS;
    }
    return 0;
}
typedef struct {struct jpeg_error_mgr base; jmp_buf jump;} JpegError;
static void jpeg_error(j_common_ptr c) {longjmp(((JpegError *)c->err)->jump, 1);}
static int draw_jpeg(const unsigned char *data, unsigned int length, unsigned int *screen) {
    struct jpeg_decompress_struct decoder;
    JpegError error;
    memset(&decoder, 0, sizeof(decoder));
    decoder.err = jpeg_std_error(&error.base); error.base.error_exit = jpeg_error;
    if (setjmp(error.jump)) {jpeg_destroy_decompress(&decoder); return -1;}
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, data, length);
    jpeg_read_header(&decoder, TRUE);
    if (decoder.image_width != 640 || decoder.image_height != 360) {jpeg_destroy_decompress(&decoder); return -1;}
    decoder.out_color_space = JCS_RGB;
    decoder.do_fancy_upsampling = FALSE;
    decoder.dct_method = JDCT_IFAST;
    jpeg_start_decompress(&decoder);
    unsigned char row[640*3];
    JSAMPROW rows[1] = {row};
    int target = 0;
    // Preserve 16:9: 640x360 becomes 480x270, with one black row above/below.
    while (decoder.output_scanline < decoder.output_height) {
        unsigned int source = decoder.output_scanline;
        jpeg_read_scanlines(&decoder, rows, 1);
        if (target < 270 && source == (unsigned int)(target*360/270)) {
            unsigned int *out = screen + (target + 1)*512;
            for (int x = 0; x < 480; x++) {unsigned char *rgb = row + (x*640/480)*3; out[x] = rgb[0] | (rgb[1]<<8) | (rgb[2]<<16) | 0xff000000;}
            target++;
        }
    }
    jpeg_finish_decompress(&decoder); jpeg_destroy_decompress(&decoder); return 0;
}
int pt_play(const char *path, char *message, unsigned int capacity) {
    int channel = -1, ok = -1, index = 0, back = 0, started = 0;
    SceUID thread = -1;
    unsigned char header[32];
    unsigned int *screens[2] = {(unsigned int *)0x44000000, (unsigned int *)(0x44000000 + 512*272*4)};
    frames = NULL; empty = full = -1;
    pt_stop = ended = broken = 0;
    snprintf(message, capacity, "Playback failed: invalid stream or decoder error.");
    if (pt_open(&stream, "GET", path) < 0) return -1;
    if (stream.status != 200 || pt_read(&stream, header, sizeof(header)) < 0 || memcmp(header, "PTV1", 4) || le32(header+4)!=640 || le32(header+8)!=360 || le32(header+12)!=44100 || le32(header+16)!=SAMPLES) goto cleanup;
    frames = memalign(64, sizeof(Frame)*SLOTS);
    if (!frames) {snprintf(message, capacity, "Not enough memory for playback buffers."); goto cleanup;}
    empty = sceKernelCreateSema("pt-empty", 0, SLOTS, SLOTS, NULL);
    full = sceKernelCreateSema("pt-full", 0, 0, SLOTS, NULL);
    if (empty < 0 || full < 0) goto cleanup;
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) goto cleanup;
    scePowerSetClockFrequency(333, 333, 166);
    thread = sceKernelCreateThread("pt-reader", reader, 0x18, 64*1024, PSP_THREAD_ATTR_USER, NULL);
    if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0) goto cleanup;
    started = 1;
    // Wait for initial buffering without blocking HOME handling.
    for (;;) {
        SceKernelSemaInfo info; memset(&info, 0, sizeof(info)); info.size = sizeof(info);
        sceKernelReferSemaStatus(full, &info);
        SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CIRCLE) {ok = 0; goto cleanup;}
        if (info.currentCount >= 4 || ended || broken) break;
        sceKernelDelayThreadCB(20000);
    }
    sceDisplaySetMode(0, 480, 272);
    memset(screens[0], 0, 512*272*4); memset(screens[1], 0, 512*272*4);
    unsigned int previous = 0;
    int paused = 0;
    while (!pt_stop) {
        SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~previous; previous = pad.Buttons;
        if (pressed & PSP_CTRL_CIRCLE) {ok = 0; break;}
        if (pressed & PSP_CTRL_START) paused = !paused;
        if (paused) {sceKernelDelayThreadCB(20000); continue;}
        SceUInt timeout = 100000;
        if (sceKernelWaitSemaCB(full, 1, &timeout) < 0) {
            if (broken) {snprintf(message, capacity, "Video stream interrupted. Please try again."); break;}
            if (ended) {ok = 0; break;}
            continue;
        }
        Frame *frame = &frames[index];
        if (draw_jpeg(frame->jpeg, frame->size, screens[back]) < 0) break;
        // Wait for the previous PCM block before recycling any audio buffers.
        if (sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, frame->pcm) < 0) break;
        sceDisplayWaitVblankStartCB();
        sceDisplaySetFrameBuf(screens[back], 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
        back ^= 1; sceKernelSignalSema(empty, 1); index = (index + 1) % SLOTS;
        scePowerTick(0);
    }
cleanup:
    pt_stop = 1;
    if (thread >= 0) {if (started) sceKernelWaitThreadEndCB(thread, NULL); sceKernelDeleteThread(thread);}
    pt_close(&stream);
    if (channel >= 0) {while (sceAudioGetChannelRestLen(channel) > 0) sceKernelDelayThreadCB(1000); sceAudioChRelease(channel);}
    if (empty >= 0) sceKernelDeleteSema(empty);
    if (full >= 0) sceKernelDeleteSema(full);
    free(frames);
    scePowerSetClockFrequency(222, 222, 111);
    pt_stop = 0;
    if (ok == 0) snprintf(message, capacity, "Playback finished. X to replay, O to edit search.");
    return ok;
}
