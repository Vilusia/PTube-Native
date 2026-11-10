#include "net.h"
#include "player.h"
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psputility.h>
#include <pspsdk.h>
#include <pspnet_apctl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PSP_MODULE_INFO("PTube", 0, 0, 2);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(-2048);
#define print pspDebugScreenPrintf
typedef struct {char id[12], title[121], channel[121];} Video;
static Video videos[8];
static int count, profile = 1;
static char query[81] = "psp", message[256] = "Search YouTube or use the server's demo mode.";
static const char letters[] = " abcdefghijklmnopqrstuvwxyz0123456789-";
static int exit_callback(int a, int b, void *p) {(void)a; (void)b; (void)p; sceKernelExitGame(); return 0;}
static int callbacks(SceSize n, void *p) {
    (void)n; (void)p;
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_callback, NULL));
    sceKernelSleepThreadCB(); return 0;
}
static void halt(const char *text) {print("%s\nPress HOME to exit.\n", text); for (;;) sceKernelSleepThreadCB();}
static int connect_wifi(void) {
    int state = 0;
    if (sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON) < 0 || sceUtilityLoadNetModule(PSP_NET_MODULE_INET) < 0 || pspSdkInetInit() < 0) return -1;
    if (sceNetApctlConnect(profile) < 0) return -1;
    for (int i = 0; i < 300; i++) {
        if (sceNetApctlGetState(&state) < 0) return -1;
        if (state == 4) return 0;
        sceKernelDelayThreadCB(100000);
    }
    return -1;
}
static int search(void) {
    char encoded[241], path[300], response[8192];
    unsigned int at = 0;
    for (const unsigned char *p = (unsigned char *)query; *p; p++) at += snprintf(encoded+at, sizeof(encoded)-at, "%%%02X", *p);
    encoded[at] = 0;
    snprintf(path, sizeof(path), "/v1/search?q=%s", encoded);
    count = 0;
    print("Searching...\n");
    if (pt_text("GET", path, response, sizeof(response)) < 0) {snprintf(message, sizeof(message), "%.250s", response); return -1;}
    if (strncmp(response, "OK\n", 3)) {snprintf(message, sizeof(message), "Invalid search response."); return -1;}
    char *line = response + 3;
    while (*line && count < 8) {
        char *end = strchr(line, '\n'); if (!end) break; *end = 0;
        char *title = strchr(line, '\t'); if (!title) break; *title++ = 0;
        char *channel = strchr(title, '\t'); if (!channel) break; *channel++ = 0;
        if (strlen(line) != 11 || strspn(line, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != 11) break;
        snprintf(videos[count].id, sizeof(videos[count].id), "%s", line);
        snprintf(videos[count].title, sizeof(videos[count].title), "%s", title);
        snprintf(videos[count].channel, sizeof(videos[count].channel), "%s", channel);
        count++; line = end + 1;
    }
    snprintf(message, sizeof(message), count ? "Up/Down: select | X: watch | O: edit search" : "No results. O to edit your search.");
    return count ? 0 : -1;
}
static void watch(int selected) {
    char path[128], response[512];
    snprintf(path, sizeof(path), "/v1/prepare?id=%s", videos[selected].id);
    pspDebugScreenClear(); print("Preparing %.60s\n\nFirst playback may take several minutes.\nO: cancel | HOME: exit\n", videos[selected].title);
    if (pt_text("POST", path, response, sizeof(response)) < 0) {snprintf(message, sizeof(message), "%.250s", response); return;}
    for (int poll = 0; poll < 600; poll++) {
        if (!strncmp(response, "READY\t", 6)) {
            char *end = strchr(response + 6, '\n'); if (end) *end = 0;
            char expected[128]; snprintf(expected, sizeof(expected), "/v1/media/%s", videos[selected].id);
            if (strcmp(response + 6, expected)) {snprintf(message, sizeof(message), "Invalid playback path."); return;}
            print("Buffering... During playback: START pause, O stop.\n");
            pt_play(expected, message, sizeof(message));
            pspDebugScreenInit(); return;
        }
        if (strncmp(response, "WAIT\t", 5)) {snprintf(message, sizeof(message), "%.250s", response); return;}
        for (int tick = 0; tick < 20; tick++) {
            SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
            if (pad.Buttons & PSP_CTRL_CIRCLE) {snprintf(message, sizeof(message), "Preparation continues on server. X to check again."); return;}
            sceKernelDelayThreadCB(100000);
        }
        snprintf(path, sizeof(path), "/v1/status?id=%s", videos[selected].id);
        if (pt_text("GET", path, response, sizeof(response)) < 0) {snprintf(message, sizeof(message), "%.250s", response); return;}
    }
    snprintf(message, sizeof(message), "Preparation timed out. Try another video.");
}
int main(int argc, char **argv) {
    if (argc && argv[0]) {char path[512]; snprintf(path, sizeof(path), "%s", argv[0]); char *slash = strrchr(path, '/'); if (slash) {*slash = 0; chdir(path);}}
    SceUID thread = sceKernelCreateThread("callbacks", callbacks, 0x11, 4096, 0, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
    pspDebugScreenInit(); sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    FILE *f = fopen("server.txt", "r");
    int valid = f && fscanf(f, "%127s %d %d", pt_host, &pt_port, &profile) == 3 && pt_port > 0 && pt_port <= 65535 && profile > 0 && strspn(pt_host, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == strlen(pt_host);
    if (f) fclose(f);
    if (!valid) halt("Configure server.txt beside EBOOT.PBP:\nSERVER_HOST PORT WIFI_PROFILE");
    print("PTube alpha\nConnecting to saved Wi-Fi profile %d...\n", profile);
    if (connect_wifi() < 0) halt("Wi-Fi failed. Check WLAN switch and saved Wi-Fi profile.");
    unsigned int previous = 0;
    int character = 1, selected = 0, editing = 1, redraw = 1;
    for (;;) {
        SceCtrlData pad; sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~previous; previous = pad.Buttons;
        if (editing) {
            if (pressed & PSP_CTRL_LEFT) {character = (character + sizeof(letters) - 2) % (sizeof(letters) - 1); redraw = 1;}
            if (pressed & PSP_CTRL_RIGHT) {character = (character + 1) % (sizeof(letters) - 1); redraw = 1;}
            if ((pressed & PSP_CTRL_CROSS) && strlen(query) < 80) {size_t len = strlen(query); query[len] = letters[character]; query[len+1] = 0; redraw = 1;}
            if ((pressed & PSP_CTRL_CIRCLE) && query[0]) {query[strlen(query)-1] = 0; redraw = 1;}
            if (pressed & PSP_CTRL_TRIANGLE) {query[0] = 0; redraw = 1;}
            if ((pressed & PSP_CTRL_START) && query[0]) {if (search() == 0) {editing = 0; selected = 0;} redraw = 1;}
        } else {
            if ((pressed & PSP_CTRL_UP) && selected > 0) {selected--; redraw = 1;}
            if ((pressed & PSP_CTRL_DOWN) && selected + 1 < count) {selected++; redraw = 1;}
            if (pressed & PSP_CTRL_CIRCLE) {editing = 1; redraw = 1;}
            if ((pressed & PSP_CTRL_CROSS) && count) {watch(selected); redraw = 1; sceCtrlPeekBufferPositive(&pad, 1); previous = pad.Buttons;}
        }
        if (redraw) {
            pspDebugScreenClear();
            pspDebugScreenSetTextColor(0xff66ccff); print("PTube alpha | 360p test transport | ~15fps\n");
            pspDebugScreenSetTextColor(0xffffffff); print("Query: %.64s\n\n", query);
            if (editing) {
                print("Left/Right: choose [%c]  X: add  O: delete\nTriangle: clear  START: search  HOME: exit\n\n", letters[character]);
            } else {
                for (int i = 0; i < count; i++) {pspDebugScreenSetTextColor(i == selected ? 0xff66ccff : 0xffffffff); print("%c %.63s\n  %.63s\n", i == selected ? '>' : ' ', videos[i].title, videos[i].channel);}
            }
            pspDebugScreenSetTextColor(0xffffffff); print("\n%.250s\n", message); redraw = 0;
        }
        sceDisplayWaitVblankStartCB();
    }
}
