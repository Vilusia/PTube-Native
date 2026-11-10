/* Adapted from KOMI Tube (MIT), copyright (c) 2026 komi-tube contributors.
 * PTube changes: see native/UPSTREAM.md. Licence: native/LICENSE-KOMI. */
/*
 * komi-app -- the native komi-tube client (stage 3).
 *
 * Screens: home -> search (firmware keyboard, then kana-to-kanji candidates)
 * -> result list (L/R pages) -> player, related videos from any list or the
 * player, and the Bookmarks (liked videos, kept only on this PSP).
 *
 * The confirm and back buttons follow the system setting (○ or × to
 * confirm), the same as the firmware keyboard; OK/BACK below mean those.
 *
 *   home      OK open        △ search       START quit
 *   list      OK play        △ like/unlike  □ related   L/R page
 *             BACK back      START search
 *   mylist    OK play        △ remove       □ related   BACK back
 *   player    OK pause       △ like/unlike  □ related   ←/→ 10 s  BACK back
 *   convert   OK search with the candidate   △ edit      BACK cancel
 *
 * ptube.cfg beside the program can run an unattended check instead:
 *   autotest=1, autotest_query=<UTF-8>, autotest_kana=<hiragana>,
 *   autotest_play_seconds=N
 * Lines are logged to ptube.log and screenshots saved as shot-*.bmp.
 */
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psputility.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tilefinch/fetch.h"
#include "tilefinch/psp_threads.h"

#include "kanji.h"
#include "discover.h"
#include "komi_runtime.h"
#include "../common/ptube_guard.h"
#include "../common/ptube_recovery.h"
#include "../common/ptube_power.h"
#include "mylist.h"
#include "osk.h"
#include "search.h"
#include "ui.h"

PSP_MODULE_INFO("PTubeNative", PSP_MODULE_USER, 0, 6);
PSP_MAIN_THREAD_PRIORITY(TILEFINCH_PSP_THREAD_PRIORITY_BROWSER);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(2560);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(2048);

#define W KOMI_SCREEN_WIDTH
#define H KOMI_SCREEN_HEIGHT
#define TOP_BAR 26
#define HINT_BAR 22
#define ROW_HEIGHT 44
#define VISIBLE_ROWS ((H - TOP_BAR - HINT_BAR) / ROW_HEIGHT)
#define LINE_ROW 30
#define VISIBLE_LINES ((H - TOP_BAR - HINT_BAR) / LINE_ROW)
#define OVERLAY_US (3u * 1000u * 1000u)
#define TOAST_US (2u * 1000u * 1000u)
#define OPEN_TIMEOUT_US (60u * 1000u * 1000u)
#define SEEK_STEP_US (10u * 1000u * 1000u)
#define LIST_PAGES 16
#define LIST_DEPTH 4
#define CONVERT_ROWS 20

typedef enum { SCREEN_HOME, SCREEN_LIST, SCREEN_MYLIST } Screen;

typedef struct {
    YtVideo videos[SEARCH_RESULTS];
    size_t count;
    char more[SEARCH_URL_SIZE];
} Page;

/* One result list: a search (with its pages) or a video's related list. */
typedef struct {
    bool related;
    bool discovery;
    char title[320];
    Page *pages[LIST_PAGES];
    size_t page_count;
    size_t page;
    int selection;
} ListView;

typedef struct {
    Screen screen;
    int home_selection;
    int mylist_selection;
    ListView lists[LIST_DEPTH];
    size_t list_depth;
    char query[256];
    char toast[128];
    uint64_t toast_until;
    uint32_t buttons;
    uint32_t pressed;
    uint64_t held_since;
    uint64_t last_repeat;
    uint64_t generation;
} App;

static App app;
static Mylist mylist;
static Budget ui_budget;
static bool load_cancelled;
static int last_discover_topic = -1;
static unsigned power_mode;
static const char *const power_names[] = {"Performance", "Balanced", "Battery saver"};

static void power_apply(bool playback, bool busy)
{
    static int applied;
    int cpu = ptube_power_cpu(power_mode, playback, busy);
    if (cpu == applied) return;
    int result = scePowerSetClockFrequency(cpu, cpu, cpu / 2);
    komi_result("power mode=%u playback=%d busy=%d cpu=%d result=0x%08x",
                power_mode, playback, busy, cpu, (unsigned)result);
    if (result >= 0) applied = cpu;
    else {
        power_mode = 0;
        if (scePowerSetClockFrequency(333, 333, 166) >= 0) applied = 333;
    }
}

static void power_load(void)
{
    char path[256];
    komi_sibling_path(path, sizeof path, "ptube-power.txt");
    FILE *file = fopen(path, "r");
    unsigned value;
    if (file != NULL) {
        if (fscanf(file, "%u", &value) == 1 && value < 3) power_mode = value;
        fclose(file);
    }
}

static void power_cycle(void)
{
    power_mode = (power_mode + 1) % 3;
    char path[256];
    komi_sibling_path(path, sizeof path, "ptube-power.txt");
    FILE *file = fopen(path, "w");
    if (file != NULL) {
        fprintf(file, "%u\n", power_mode);
        fclose(file);
    }
    power_apply(false, false);
}

static void draw_battery(uint16_t *vram)
{
    static uint64_t checked;
    static int percent = -1, plugged;
    uint64_t now = komi_now_us();
    if (checked == 0 || now - checked >= 5000000u) {
        percent = scePowerGetBatteryLifePercent();
        plugged = scePowerIsPowerOnline() > 0;
        checked = now;
    }
    char label[24];
    if (percent >= 0 && percent <= 100)
        snprintf(label, sizeof label, "%s%d%%", plugged ? "AC " : "", percent);
    else snprintf(label, sizeof label, "%s", plugged ? "AC" : "--%");
    ui_text(vram, W - 80, 5, W - 4, label, 16, UI_TEXT);
}

/* Confirm/back, from the system setting. */
static uint32_t BTN_OK = PSP_CTRL_CIRCLE;
static uint32_t BTN_BACK = PSP_CTRL_CROSS;
static const char *ICON_OK = UI_ICON_CIRCLE;
static const char *ICON_BACK = UI_ICON_CROSS;

static void read_button_setting(void)
{
    int accept = 0;
    int result = sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN,
                                             &accept);
    /* Setting 9 is the X/O accept choice (PSPSDK names it UNKNOWN):
       0 = ○ confirms, 1 = × confirms. */
    if (result >= 0 && accept == 1) {
        BTN_OK = PSP_CTRL_CROSS;
        BTN_BACK = PSP_CTRL_CIRCLE;
        ICON_OK = UI_ICON_CROSS;
        ICON_BACK = UI_ICON_CIRCLE;
    }
    komi_result("buttons confirm=%s (setting=%d result=0x%08x)",
                BTN_OK == PSP_CTRL_CROSS ? "cross" : "circle", accept,
                (unsigned) result);
}

/* --- input --- */

/* Buttons newly pressed this frame, with auto-repeat for the d-pad. */
static void read_input(void)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    uint32_t now = pad.Buttons;
    app.pressed = now & ~app.buttons;
    uint32_t repeatable = PSP_CTRL_UP | PSP_CTRL_DOWN;
    uint64_t t = komi_now_us();
    if ((now & repeatable) != 0
        && (now & repeatable) == (app.buttons & repeatable)) {
        if (t - app.held_since > 400000u && t - app.last_repeat > 90000u) {
            app.pressed |= now & repeatable;
            app.last_repeat = t;
        }
    } else {
        app.held_since = t;
    }
    app.buttons = now;
}

static bool pressed(uint32_t button)
{
    return (app.pressed & button) != 0;
}

static void toast(const char *text)
{
    snprintf(app.toast, sizeof app.toast, "%s", text);
    app.toast_until = komi_now_us() + TOAST_US;
    komi_result("toast \"%s\"", text);
}

/* --- screenshots (autotest): the back buffer as a 24-bit BMP --- */

static void screenshot(const uint16_t *vram, const char *name)
{
    char path[256];
    komi_sibling_path(path, sizeof path, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL) return;
    const unsigned row = W * 3u;
    const unsigned size = 54u + row * H;
    unsigned char header[54] = {'B', 'M'};
    const struct { unsigned at, value; } fields[] = {
        {2, size}, {10, 54}, {14, 40}, {18, W}, {22, H}, {34, row * H},
    };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        for (unsigned b = 0; b < 4; b++)
            header[fields[i].at + b] =
                (unsigned char) (fields[i].value >> (8u * b));
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, sizeof header, file);
    static unsigned char line[W * 3];
    for (int y = H - 1; y >= 0; y--) {
        const uint16_t *source = vram + (size_t) y * KOMI_VRAM_STRIDE;
        for (int x = 0; x < W; x++) {
            uint16_t p = source[x];
            line[x * 3 + 0] = (unsigned char) (((p >> 11) & 31u) << 3);
            line[x * 3 + 1] = (unsigned char) (((p >> 5) & 63u) << 2);
            line[x * 3 + 2] = (unsigned char) ((p & 31u) << 3);
        }
        fwrite(line, 1, sizeof line, file);
    }
    fclose(file);
    komi_result("screenshot %s", name);
}

/* --- drawing --- */

static void draw_bars(uint16_t *vram, const char *title, const char *hints)
{
    ui_fill(vram, 0, 0, W, TOP_BAR, UI_BAR);
    int x = ui_text(vram, 10, 5, 140, "PTube Native", 16, UI_ACCENT);
    ui_text(vram, x + 12, 5, W - 88, title, 16, UI_TEXT);
    draw_battery(vram);
    ui_fill(vram, 0, H - HINT_BAR, W, HINT_BAR, UI_BAR);
    ui_text(vram, 8, H - HINT_BAR + 3, W - 4, hints, 16, UI_TEXT_MUTED);
}

static void draw_toast(uint16_t *vram)
{
    if (app.toast[0] == '\0' || komi_now_us() > app.toast_until) return;
    int width = ui_text_width(app.toast, 16) + 24;
    if (width > W - 20) width = W - 20;
    int x = (W - width) / 2;
    int y = H - HINT_BAR - 36;
    ui_fill(vram, x, y, width, 28, UI_ROW_SELECTED);
    ui_text(vram, x + 12, y + 6, x + width - 12, app.toast, 16, UI_TEXT);
}

static void draw_video_rows(uint16_t *vram, const YtVideo *videos,
                            size_t count, int selection, bool mark_liked)
{
    int first = selection - VISIBLE_ROWS / 2;
    if (first > (int) count - VISIBLE_ROWS) first = (int) count - VISIBLE_ROWS;
    if (first < 0) first = 0;
    for (int row = 0; row < VISIBLE_ROWS && first + row < (int) count; row++) {
        const YtVideo *video = &videos[first + row];
        int y = TOP_BAR + row * ROW_HEIGHT;
        if (first + row == selection)
            ui_fill(vram, 0, y, W, ROW_HEIGHT, UI_ROW_SELECTED);
        int right = W - 10;
        if (video->duration[0] != '\0') {
            int width = ui_text_width(video->duration, 12);
            ui_text(vram, W - 10 - width, y + 6, W, video->duration, 12,
                    UI_TEXT_MUTED);
            right = W - 18 - width;
        }
        bool liked = mark_liked && mylist_find(&mylist, video->id) >= 0;
        int x = 10;
        if (liked) x = ui_text(vram, x, y + 4, right, "♥ ", 16, UI_LIKED);
        ui_text(vram, x, y + 4, right, video->title, 16, UI_TEXT);
        char meta[YT_CHANNEL_SIZE + YT_SHORT_SIZE + 8];
        snprintf(meta, sizeof meta, "%s%s%s", video->channel,
                 video->meta[0] != '\0' ? "  " : "", video->meta);
        /* The Japanese bitmaps are 16 px; smaller sizes smear them. */
        ui_text(vram, 10, y + 23, W - 10, meta, 16, UI_TEXT_MUTED);
    }
}

static void draw_center(uint16_t *vram, const char *line1, const char *line2)
{
    int width = ui_text_width(line1, 16);
    if (width > W - 20) width = W - 20;
    ui_text(vram, (W - width) / 2, H / 2 - 22, W - 10, line1, 16, UI_TEXT);
    if (line2 != NULL) {
        width = ui_text_width(line2, 16);
        if (width > W - 20) width = W - 20;
        ui_text(vram, (W - width) / 2, H / 2 + 4, W - 10, line2, 16,
                UI_TEXT_MUTED);
    }
}

static uint16_t *begin_frame(void)
{
    uint16_t *vram = psp_display_back_buffer(&komi.display);
    if (vram != NULL) ui_fill(vram, 0, 0, W, H, UI_BACKGROUND);
    return vram;
}

/* "<OK> Play   <△> Save ..." with the right confirm/back icons. */
static void hints(char *out, size_t size, const char *ok, const char *rest,
                  const char *back)
{
    snprintf(out, size, "%s %s  %s  %s %s", ICON_OK, ok, rest, ICON_BACK,
             back);
}

static ListView *top_list(void)
{
    return app.list_depth == 0 ? NULL : &app.lists[app.list_depth - 1];
}

static const Page *current_page(const ListView *list)
{
    return list == NULL || list->page_count == 0 ? NULL
        : list->pages[list->page];
}

static void draw_screen(uint16_t *vram)
{
    char hint[200];
    switch (app.screen) {
    case SCREEN_HOME: {
        snprintf(hint, sizeof hint, "%s Open  " UI_ICON_TRIANGLE
                 " Search  START Exit", ICON_OK);
        draw_bars(vram, "", hint);
        static const char *const entries[] = {"Discover videos", "Search YouTube", "Bookmarks", "Power"};
        for (int i = 0; i < 4; i++) {
            int y = TOP_BAR + 10 + i * 50;
            if (i == app.home_selection)
                ui_fill(vram, 40, y, W - 80, 44, UI_ROW_SELECTED);
            char label[64];
            if (i == 2)
                snprintf(label, sizeof label, "%s (%u videos)", entries[i],
                         (unsigned) mylist.count);
            else if (i == 3)
                snprintf(label, sizeof label, "Power: %s", power_names[power_mode]);
            else
                snprintf(label, sizeof label, "%s", entries[i]);
            ui_text(vram, 64, y + 13, W - 60, label, 16, UI_TEXT);
        }
        break;
    }
    case SCREEN_LIST: {
        const ListView *list = top_list();
        const Page *page = current_page(list);
        char title[400];
        if (list->discovery)
            snprintf(title, sizeof title, "Discover: %s", list->title);
        else if (list->related)
            snprintf(title, sizeof title, "Related: %s", list->title);
        else if (list->page > 0 || (page != NULL && page->more[0] != '\0'))
            snprintf(title, sizeof title, "\"%s\" - page %u", list->title,
                     (unsigned) list->page + 1);
        else
            snprintf(title, sizeof title, "\"%s\"", list->title);
        hints(hint, sizeof hint, "Play",
              UI_ICON_TRIANGLE " Save  " UI_ICON_SQUARE " Related"
              "  L/R Page", "Back");
        if (list->related)
            hints(hint, sizeof hint, "Play",
                  UI_ICON_TRIANGLE " Save  " UI_ICON_SQUARE " Related",
                  "Back");
        if (list->discovery)
            hints(hint, sizeof hint, "Play", "SELECT Shuffle  " UI_ICON_TRIANGLE " Save", "Back");
        draw_bars(vram, title, hint);
        if (page == NULL || page->count == 0)
            draw_center(vram, "No videos found", NULL);
        else
            draw_video_rows(vram, page->videos, page->count,
                            list->selection, true);
        break;
    }
    case SCREEN_MYLIST:
        hints(hint, sizeof hint, "Play",
              UI_ICON_TRIANGLE " Remove  " UI_ICON_SQUARE " Related", "Back");
        draw_bars(vram, "Bookmarks", hint);
        if (mylist.count == 0)
            draw_center(vram, "No saved videos",
                        "Press " UI_ICON_TRIANGLE " in a list or player to save");
        draw_video_rows(vram, mylist.items, mylist.count,
                        app.mylist_selection, false);
        break;
    }
    draw_toast(vram);
}

static void present_screen(void)
{
    uint16_t *vram = begin_frame();
    if (vram == NULL) return;
    draw_screen(vram);
    (void) psp_display_publish(&komi.display);
}

static void show_busy(const char *title, const char *line, uint64_t started,
                       bool cancellable)
{
    static const char *const dots[] = {".", "..", "..."};
    uint16_t *vram = begin_frame();
    if (vram == NULL) return;
    char hint[64];
    if (cancellable) snprintf(hint, sizeof hint, "%s Cancel  HOME Exit", ICON_BACK);
    else snprintf(hint, sizeof hint, "%s", "HOME Exit");
    draw_bars(vram, title, hint);
    char text[400];
    snprintf(text, sizeof text, "%s%s", line,
             dots[((komi_now_us() - started) / 300000u) % 3u]);
    draw_center(vram, text, NULL);
    (void) psp_display_publish(&komi.display);
}

/* --- like / unlike --- */

static void toggle_like(const YtVideo *video)
{
    if (mylist_find(&mylist, video->id) >= 0) {
        mylist_remove(&mylist, video->id);
        toast("Removed from bookmarks");
    } else if (mylist_add(&mylist, video)) {
        toast("Added to bookmarks");
    } else {
        toast("Could not save bookmarks");
    }
}

/* --- loading lists --- */

/* Load one page from url into page (blocking, with a busy screen). */
static bool load_page(const char *url, const char *busy_title,
                      const char *busy_line, Page *page)
{
    komi_result("load url=%.200s", url);
    power_apply(false, true);
    char error[256] = {0};
    uint64_t started = komi_now_us();
    ptube_operation_begin(45);
    SearchJob *job = search_begin_url(url, error, sizeof error);
    if (job == NULL) {
        ptube_operation_end();
        komi_result("load FAIL start error=\"%s\"", error);
        return false;
    }
    SearchStatus status = SEARCH_PENDING;
    komi_result("load job created; entering search pump");
    uint64_t next_report = started + 5000000u;
    app.buttons = ~0u;
    while (status == SEARCH_PENDING) {
        read_input();
        if (pressed(BTN_BACK) || komi_now_us() - started > 35000000u) {
            load_cancelled = true;
            search_cancel(job);
            status = SEARCH_FAILED;
            snprintf(error, sizeof error, "Search cancelled or timed out");
            break;
        }
        komi_progress("load");
        status = search_pump(job, page->videos, &page->count, page->more,
                             sizeof page->more, error, sizeof error);
        if (komi_now_us() >= next_report) {
            komi_result("load pending elapsed=%llums", (unsigned long long)((komi_now_us()-started)/1000u));
            next_report = komi_now_us() + 5000000u;
        }
        show_busy(busy_title, busy_line, started, true);
        /* The PSP transport's setup worker is deliberately lower priority.
           The owner must donate bounded CPU windows after input/rendering. */
        fetch_background_transport_cooperate(komi_now_us());
    }
    search_end(job);
    ptube_operation_end();
    komi_result("load %s elapsed=%llums results=%u more=%d error=\"%s\"",
                status == SEARCH_DONE ? "DONE" : "FAIL",
                (unsigned long long) ((komi_now_us() - started) / 1000u),
                (unsigned) page->count, page->more[0] != '\0', error);
    if (status != SEARCH_DONE) return false;
    for (size_t i = 0; i < page->count; i++)
        komi_result("result %u %s [%s] %s / %s", (unsigned) i + 1,
                    page->videos[i].id, page->videos[i].duration,
                    page->videos[i].title, page->videos[i].channel);
    return true;
}

/* YouTube now and then answers in a layout that yields nothing; the same
   request a moment later is normally fine, so try twice. */
static Page *load_page_retrying(const char *url, const char *busy_title,
                                const char *busy_line)
{
    Page *page = calloc(1, sizeof *page);
    if (page == NULL) return NULL;
    bool loaded = false;
    load_cancelled = false;
    for (int attempt = 1; attempt <= 2 && !loaded; attempt++) {
        loaded = load_page(url, busy_title, busy_line, page)
            && page->count > 0;
        if (!loaded) komi_result("load attempt %d found nothing", attempt);
        if (load_cancelled) break;
    }
    if (!loaded) {
        free(page);
        return NULL;
    }
    /* Show the first useful response immediately. L/R requests more pages;
       don't hide extra network requests behind the initial search screen. */
    return page;
}

static void free_list(ListView *list)
{
    for (size_t i = 0; i < list->page_count; i++) free(list->pages[i]);
    memset(list, 0, sizeof *list);
}

/* Push a new list (dropping the oldest when the stack is full). */
static bool push_list(bool related, const char *title, const char *url,
                      const char *busy_line)
{
    Page *page = load_page_retrying(url, related ? "Related videos" : "Searching",
                                    busy_line);
    if (page == NULL) {
        toast(related ? "Could not load related videos"
                      : "Search failed");
        return false;
    }
    if (app.list_depth == LIST_DEPTH) {
        free_list(&app.lists[0]);
        memmove(&app.lists[0], &app.lists[1],
                (LIST_DEPTH - 1) * sizeof app.lists[0]);
        memset(&app.lists[LIST_DEPTH - 1], 0, sizeof app.lists[0]);
        app.list_depth--;
    }
    ListView *list = &app.lists[app.list_depth++];
    memset(list, 0, sizeof *list);
    list->related = related;
    snprintf(list->title, sizeof list->title, "%s", title);
    list->pages[0] = page;
    list->page_count = 1;
    app.screen = SCREEN_LIST;
    return true;
}

static void pop_list(void)
{
    if (app.list_depth > 0) free_list(&app.lists[--app.list_depth]);
    app.screen = app.list_depth > 0 ? SCREEN_LIST : SCREEN_HOME;
}

static bool open_discover(void)
{
    uint64_t clock = komi_now_us();
    uint32_t seed = (uint32_t)clock ^ (uint32_t)(clock >> 32);
    unsigned chosen = discover_pick(seed, last_discover_topic);
    const char *topic = discover_topic(chosen);
    char url[1024];
    if (!yt_search_url(topic, url, sizeof url)) return false;
    if (!push_list(false, topic, url, "Finding videos to discover")) return false;
    ListView *list = &app.lists[app.list_depth-1];
    list->discovery = true;
    discover_shuffle(list->pages[0]->videos, list->pages[0]->count, seed);
    last_discover_topic = (int)chosen;
    toast("Random topic picks. SELECT for another mix.");
    return true;
}

static bool run_search(const char *query)
{
    snprintf(app.query, sizeof app.query, "%s", query);
    komi_result("search query=\"%s\"", query);
    char url[1024];
    if (!yt_search_url(query, url, sizeof url)) {
        toast("Search is empty or too long");
        return false;
    }
    char line[320];
    snprintf(line, sizeof line, "Searching for \"%s\"", query);
    return push_list(false, query, url, line);
}

static bool open_related(const YtVideo *video)
{
    char url[96];
    snprintf(url, sizeof url, "https://m.youtube.com/watch?v=%s", video->id);
    return push_list(true, video->title, url, "Loading related videos");
}

/* R: the next page (loading it the first time); L: the previous one. */
static void turn_page(ListView *list, int direction)
{
    if (direction < 0) {
        if (list->page == 0) return;
        list->page--;
        list->selection = 0;
        return;
    }
    if (list->page + 1 < list->page_count) {
        list->page++;
        list->selection = 0;
        return;
    }
    const Page *page = current_page(list);
    if (page == NULL || page->more[0] == '\0'
        || list->page_count == LIST_PAGES) {
        toast("No more pages");
        return;
    }
    char line[64];
    snprintf(line, sizeof line, "Loading page %u",
             (unsigned) list->page + 2);
    Page *next = load_page_retrying(page->more, "Searching", line);
    if (next == NULL) {
        toast("Could not load the next page");
        return;
    }
    list->pages[list->page_count++] = next;
    list->page++;
    list->selection = 0;
}

/* --- kana to kanji --- */

typedef struct {
    char rows[CONVERT_ROWS][KANJI_TEXT * 2];
    bool suggestion[CONVERT_ROWS];
    size_t count;
} Candidates;

static bool fetch_text(const char *url, char *out, size_t size,
                       size_t *length)
{
    FetchResult result = {0};
    bool ok = fetch_url(&komi.budget, url, 64u * 1024u, 10000, &result)
        && result.status_code == 200 && result.data != NULL;
    *length = 0;
    if (ok) {
        *length = result.length < size - 1 ? result.length : size - 1;
        memcpy(out, result.data, *length);
        out[*length] = '\0';
    }
    komi_result("fetch %s status=%ld length=%u url=%.120s",
                ok ? "OK" : "FAIL", result.status_code,
                (unsigned) result.length, url);
    fetch_result_free(&result);
    return ok;
}

static void add_candidate(Candidates *c, const char *text, bool suggestion)
{
    if (text[0] == '\0' || c->count == CONVERT_ROWS) return;
    for (size_t i = 0; i < c->count; i++)
        if (strcmp(c->rows[i], text) == 0) return;
    snprintf(c->rows[c->count], sizeof c->rows[0], "%s", text);
    c->suggestion[c->count] = suggestion;
    c->count++;
}

/* Whole-phrase conversions (first choice everywhere, then one segment at a
   time swapped for its alternatives), YouTube's suggestions for the best
   one, and finally the text as typed. */
static void build_candidates(const char *text, Candidates *c)
{
    memset(c, 0, sizeof *c);
    uint64_t started = komi_now_us();
    show_busy("Convert", "Converting Japanese input", started, false);
    static char body[16384];
    size_t length = 0;
    char encoded[768];
    char url[1024];
    KanjiConversion conversion;
    bool converted = false;
    if (kanji_url_encode(text, encoded, sizeof encoded)) {
        snprintf(url, sizeof url,
                 "https://www.google.com/transliterate?langpair=ja-Hira%%7Cja"
                 "&text=%s", encoded);
        converted = fetch_text(url, body, sizeof body, &length)
            && kanji_parse_transliteration(body, length, &conversion);
    }
    char best[KANJI_TEXT * 2] = {0};
    if (converted) {
        for (size_t s = 0; s < conversion.count; s++) {
            const KanjiSegment *segment = &conversion.segments[s];
            strncat(best, segment->count > 0 ? segment->candidates[0]
                                             : segment->reading,
                    sizeof best - strlen(best) - 1);
        }
        add_candidate(c, best, false);
        for (size_t s = 0; s < conversion.count; s++) {
            for (size_t k = 1; k < conversion.segments[s].count; k++) {
                char phrase[KANJI_TEXT * 2] = {0};
                for (size_t t = 0; t < conversion.count; t++) {
                    const KanjiSegment *segment = &conversion.segments[t];
                    const char *part = t == s ? segment->candidates[k]
                        : segment->count > 0 ? segment->candidates[0]
                        : segment->reading;
                    strncat(phrase, part, sizeof phrase - strlen(phrase) - 1);
                }
                if (c->count < CONVERT_ROWS / 2)
                    add_candidate(c, phrase, false);
            }
        }
    }
    const char *basis = best[0] != '\0' ? best : text;
    if (kanji_url_encode(basis, encoded, sizeof encoded)) {
        snprintf(url, sizeof url,
                 "https://suggestqueries.google.com/complete/search?"
                 "client=firefox&ds=yt&hl=ja&q=%s", encoded);
        static char suggestions[KANJI_SUGGESTIONS][KANJI_TEXT];
        size_t count = fetch_text(url, body, sizeof body, &length)
            ? kanji_parse_suggestions(body, length, suggestions,
                                      KANJI_SUGGESTIONS)
            : 0;
        for (size_t i = 0; i < count; i++)
            add_candidate(c, suggestions[i], true);
    }
    add_candidate(c, text, false);
    komi_result("convert text=\"%s\" converted=%d candidates=%u best=\"%s\"",
                text, converted, (unsigned) c->count, best);
}

static void draw_candidates(uint16_t *vram, const char *text,
                            const Candidates *c, int selection)
{
    char title[200];
    snprintf(title, sizeof title, "Convert: %s", text);
    char hint[200];
    hints(hint, sizeof hint, "Search", UI_ICON_TRIANGLE " Edit", "Cancel");
    draw_bars(vram, title, hint);
    int first = selection - VISIBLE_LINES / 2;
    if (first > (int) c->count - VISIBLE_LINES)
        first = (int) c->count - VISIBLE_LINES;
    if (first < 0) first = 0;
    for (int row = 0; row < VISIBLE_LINES && first + row < (int) c->count;
         row++) {
        int at = first + row;
        int y = TOP_BAR + row * LINE_ROW;
        if (at == selection) ui_fill(vram, 0, y, W, LINE_ROW, UI_ROW_SELECTED);
        int x = ui_text(vram, 16, y + 7, W - 90, c->rows[at], 16, UI_TEXT);
        (void) x;
        const char *tag = at == (int) c->count - 1 ? "Use as typed"
            : c->suggestion[at] ? "Suggestion" : "Convert";
        int width = ui_text_width(tag, 16);
        ui_text(vram, W - 12 - width, y + 7, W, tag, 16, UI_TEXT_MUTED);
    }
}

/* 1 = search with out, 2 = edit out in the keyboard, 0 = cancelled. */
static int choose_candidate(const char *text, char *out, size_t size,
                            const char *shot)
{
    static Candidates c;
    build_candidates(text, &c);
    int selection = 0;
    app.buttons = ~0u;
    for (;;) {
        komi_progress("convert");
        read_input();
        if (pressed(PSP_CTRL_UP) && selection > 0) selection--;
        if (pressed(PSP_CTRL_DOWN) && selection + 1 < (int) c.count)
            selection++;
        uint16_t *vram = begin_frame();
        if (vram != NULL) {
            draw_candidates(vram, text, &c, selection);
            if (shot != NULL) {
                screenshot(vram, shot);
                snprintf(out, size, "%s", c.rows[0]);
                (void) psp_display_publish(&komi.display);
                return 1;
            }
            (void) psp_display_publish(&komi.display);
        }
        if (pressed(BTN_BACK)) return 0;
        if (pressed(BTN_OK) || pressed(PSP_CTRL_TRIANGLE)) {
            snprintf(out, size, "%s", c.rows[selection]);
            return pressed(BTN_OK) ? 1 : 2;
        }
    }
}

static void ask_and_search(void)
{
    char text[256];
    snprintf(text, sizeof text, "%s", app.query);
    for (;;) {
        char typed[256];
        if (!osk_input("Search YouTube", text,
                       typed, sizeof typed))
            return;
        komi_result("osk text=\"%s\"", typed);
        if (typed[0] == '\0') return;
        if (!kanji_has_hiragana(typed)) {
            run_search(typed);
            return;
        }
        char chosen[256];
        int choice = choose_candidate(typed, chosen, sizeof chosen, NULL);
        if (choice == 0) return;
        if (choice == 1) {
            run_search(chosen);
            return;
        }
        snprintf(text, sizeof text, "%s", chosen);
    }
}

/* --- player --- */

typedef struct {
    const YtVideo *video;
    uint64_t overlay_until;
} PlayerView;

static void format_time(char *out, size_t size, uint64_t us)
{
    unsigned seconds = (unsigned) (us / 1000000u);
    if (seconds >= 3600u)
        snprintf(out, size, "%u:%02u:%02u", seconds / 3600u,
                 seconds / 60u % 60u, seconds % 60u);
    else
        snprintf(out, size, "%u:%02u", seconds / 60u, seconds % 60u);
}

static void player_overlay(uint16_t *vram, void *context)
{
    PlayerView *view = context;
    const PspUiMediaState *ui = &komi.media.ui;
    uint64_t now = komi_now_us();
    bool loading = !ui->playing && !ui->failed && !ui->ended
        && komi.media.frame.pixels == NULL;
    if (loading) draw_center(vram, "Loading...", view->video->title);
    if (now < view->overlay_until || loading || ui->failed
        || (!ui->playing && !ui->buffering)) {
        ui_shade(vram, 0, 0, W, TOP_BAR);
        bool liked = mylist_find(&mylist, view->video->id) >= 0;
        int x = 10;
        if (liked) x = ui_text(vram, x, 5, W - 10, "♥ ", 16, UI_LIKED);
        ui_text(vram, x, 5, W - 88, view->video->title, 16, UI_TEXT);
        draw_battery(vram);
        ui_shade(vram, 0, H - 32, W, 32);
        char position[32], duration[32], line[80];
        format_time(position, sizeof position, ui->current_time_us);
        format_time(duration, sizeof duration, ui->duration_us);
        snprintf(line, sizeof line, "%s %s/%s",
                 ui->playing ? UI_ICON_PLAY : UI_ICON_PAUSE, position,
                 duration);
        ui_text(vram, 6, H - 27, 170, line, 16, UI_TEXT);
        char hint[200];
        snprintf(hint, sizeof hint,
                 "%s Pause " UI_ICON_TRIANGLE " Save " UI_ICON_SQUARE
                 " Related %s Back", ICON_OK, ICON_BACK);
        ui_text(vram, 168, H - 27, W - 4, hint, 16, UI_TEXT_MUTED);
        if (ui->duration_us > 0) {
            int filled = (int) ((uint64_t) (W - 20) * ui->current_time_us
                                / ui->duration_us);
            ui_fill(vram, 10, H - 7, W - 20, 3, UI_TEXT_MUTED);
            ui_fill(vram, 10, H - 7, filled, 3, UI_ACCENT);
        }
    }
    if (ui->failed) draw_center(vram, "Could not play this video", ui->status);
    draw_toast(vram);
}

typedef struct {
    bool active;
    uint64_t play_us;
    bool like_after_start;
    const char *shot;
} AutoPlay;

typedef enum { PLAYER_BACK, PLAYER_RELATED } PlayerExit;

/* Play one video until BACK or □ (or, under autotest, for play_us). */
static PlayerExit play_video_attempt(const YtVideo *video, const AutoPlay *automatic,
                                    uint64_t resume_us, uint64_t *recovery_us)
{
    char url[96];
    snprintf(url, sizeof url, "https://www.youtube.com/watch?v=%s",
             video->id);
    komi_result("play id=%s title=\"%s\"", video->id, video->title);
    PlayerView view = {video, komi_now_us() + OVERLAY_US};
    KomiPlayback playback = {0};
    uint64_t started = komi_now_us();
    uint64_t playing_since = 0;
    bool shot_taken = false;
    bool auto_liked = false;
    bool resume_pending = resume_us != 0;
    uint64_t last_position = resume_us;
    uint64_t refresh_started = 0;
    *recovery_us = 0;
    PlayerExit exit = PLAYER_BACK;
    ptube_operation_begin(65);
    power_apply(true, true);
    bool accepted = psp_media_open_provider_route(&komi.media, url,
                                                  ++app.generation);
    const char *outcome = accepted ? "closed" : "refused";
    app.buttons = ~0u;
    while (accepted) {
        read_input();
        bool redraw = false;
        uint64_t now = komi_now_us();
        if (app.pressed != 0) {
            view.overlay_until = now + OVERLAY_US;
            redraw = true;
        }
        if (pressed(BTN_BACK)) break;
        if (pressed(PSP_CTRL_SQUARE)) {
            exit = PLAYER_RELATED;
            break;
        }
        if (pressed(BTN_OK)) {
            PspUiMediaIntent intent = {
                .action = PSP_UI_MEDIA_ACTION_PLAY_PAUSE};
            psp_media_execute_intent(&komi.media, intent);
        }
        if (pressed(PSP_CTRL_TRIANGLE)) toggle_like(video);
        if (pressed(PSP_CTRL_LEFT) || pressed(PSP_CTRL_RIGHT)) {
            uint64_t at = komi.media.ui.current_time_us;
            uint64_t target = pressed(PSP_CTRL_RIGHT)
                ? at + SEEK_STEP_US
                : (at > SEEK_STEP_US ? at - SEEK_STEP_US : 0);
            if (komi.media.ui.duration_us > 0
                && target >= komi.media.ui.duration_us)
                target = komi.media.ui.duration_us - 1000000u;
            (void) psp_media_request_seek(&komi.media, target, false);
        }
        const PspUiMediaState *ui = &komi.media.ui;
        power_apply(true, !ui->playing || ui->buffering);
        if (ui->playing && ui->current_time_us > 0)
            last_position = ui->current_time_us;
        /* Refresh destroys the old pipeline and resets its visible clock.
           Preserve our last position separately until the replacement opens. */
        bool refreshing = playing_since != 0 && !ui->playing
            && !ui->failed && !ui->ended && komi.media.frame.pixels == NULL;
        if (refreshing && refresh_started == 0) refresh_started = now;
        if (!refreshing) refresh_started = 0;
        if (ptube_refresh_stalled(now, refresh_started)) {
            outcome = "refresh-stalled";
            *recovery_us = last_position;
            break;
        }
        if (ui->playing && playing_since == 0) {
            ptube_operation_end();
            playing_since = now;
            komi_result("playing id=%s first-frame=%llums", video->id,
                        (unsigned long long) ((now - started) / 1000u));
        }
        /* Keep the overlay current while it is up (clock, toast). */
        if (now < view.overlay_until || now < app.toast_until
            || !ui->playing)
            redraw = redraw
                || (now / 250000u) != ((now - 16667u) / 250000u);
        komi_playback_frame(&playback, player_overlay, &view, redraw);
        if (resume_pending && ui->playing) {
            resume_pending = false;
            if (!psp_media_request_seek(&komi.media, resume_us, false)) {
                komi_result("recovery resume seek refused position=%llums",
                            (unsigned long long)(resume_us / 1000u));
                toast("Could not resume. Use Right to seek forward.");
            }
        }
        komi_progress(playing_since == 0 ? "opening" : "playing");
        if (ui->ended) { outcome = "ended"; break; }
        if (ui->failed) {
            outcome = "failed";
            *recovery_us = last_position;
            komi_result("play failed status=\"%s\"", ui->status);
            uint64_t until = komi_now_us() + 3000000u;
            while (komi_now_us() < until) {
                komi_playback_frame(&playback, player_overlay, &view, true);
                komi_progress("failed");
            }
            break;
        }
        if (playing_since == 0 && now - started > OPEN_TIMEOUT_US) {
            outcome = "timeout";
            break;
        }
        if (automatic != NULL && automatic->active && playing_since != 0) {
            if (automatic->like_after_start && !auto_liked
                && now - playing_since > 2000000u) {
                auto_liked = true;
                toggle_like(video);
                view.overlay_until = now + OVERLAY_US;
            }
            if (!shot_taken && now - playing_since > 3000000u) {
                /* Draw the overlay over the current picture, then keep it. */
                komi_playback_frame(&playback, player_overlay, &view, true);
                screenshot(psp_display_front_buffer(&komi.display),
                           automatic->shot);
                shot_taken = true;
            }
            if (now - playing_since > automatic->play_us) {
                outcome = "auto-stop";
                break;
            }
        }
    }
    komi_result("play end id=%s outcome=%s position=%llums heap-used=%u",
                video->id, outcome,
                (unsigned long long) (komi.media.ui.current_time_us / 1000u),
                komi_heap_used());
    komi_playback_close(&playback);
    app.buttons = ~0u; /* ignore whatever is still held */
    return exit;
}

static PlayerExit play_video(const YtVideo *video, const AutoPlay *automatic)
{
    uint64_t resume = 0;
    for (unsigned attempt = 0; ; ++attempt) {
        uint64_t recovery = 0;
        PlayerExit result = play_video_attempt(video, automatic, resume, &recovery);
        if (attempt > 0 && recovery > 0) {
            komi_result("recovery exhausted; returning to XMB");
            show_busy("Connection could not recover", "Returning to PSP menu. Reopen PTube to retry.",
                      komi_now_us(), false);
            sceKernelDelayThread(2500000);
            sceKernelExitGame();
            return PLAYER_BACK;
        }
        bool retry = ptube_recovery_allowed(attempt, recovery);
        FetchBackgroundTransportMetrics transport = {0};
        bool pending = fetch_background_transport_metrics(&transport)
            && transport.occupied_slots != 0;
        /* Back during a refresh must not leave the cancelled request behind
           for the next search or video (the original failure sequence). */
        if (!retry && !pending) return result;
        komi_result("recovery begin position=%llums retry=%d occupied=%u",
                    (unsigned long long)(recovery / 1000u), retry,
                    transport.occupied_slots);
        show_busy("Reconnecting", "Restoring video connection", komi_now_us(), false);
        /* All resolver/decoder owners were closed above. Never delete or
           reuse a transport whose worker still owns a request. */
        ptube_operation_begin(15);
        power_apply(false, true);
        fetch_background_transport_set_media_priority(false);
        bool stopped = fetch_background_transport_shutdown(3000);
        bool ready = stopped && fetch_background_transport_initialize(&komi.budget);
        ptube_operation_end();
        komi_result("recovery transport stopped=%d ready=%d", stopped, ready);
        if (!ready) {
            show_busy("Connection could not recover", "Returning to PSP menu. Reopen PTube to retry.",
                      komi_now_us(), false);
            sceKernelDelayThread(2500000);
            sceKernelExitGame();
            return PLAYER_BACK;
        }
        if (!retry) return result;
        resume = recovery;
    }
}

static void play_then_maybe_related(const YtVideo *video)
{
    YtVideo copy = *video;
    if (play_video(&copy, NULL) == PLAYER_RELATED) open_related(&copy);
}

/* --- config and autotest --- */

typedef struct {
    bool autotest;
    char query[128];
    char kana[128];
    unsigned play_seconds;
} Config;

static void load_config(Config *config)
{
    memset(config, 0, sizeof *config);
    config->play_seconds = 12;
    char path[256];
    komi_sibling_path(path, sizeof path, "ptube.cfg");
    FILE *file = fopen(path, "r");
    if (file == NULL) return;
    char line[256];
    while (fgets(line, sizeof line, file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        unsigned value = 0;
        if (sscanf(line, "autotest=%u", &value) == 1)
            config->autotest = value != 0;
        else if (strncmp(line, "autotest_query=", 15) == 0)
            snprintf(config->query, sizeof config->query, "%s", line + 15);
        else if (strncmp(line, "autotest_kana=", 14) == 0)
            snprintf(config->kana, sizeof config->kana, "%s", line + 14);
        else if (sscanf(line, "autotest_play_seconds=%u", &value) == 1
                 && value > 0)
            config->play_seconds = value;
    }
    fclose(file);
}

static void shot_screen(const char *name)
{
    uint16_t *vram = begin_frame();
    if (vram == NULL) return;
    draw_screen(vram);
    screenshot(vram, name);
    (void) psp_display_publish(&komi.display);
}

static bool check(bool condition, const char *what)
{
    komi_result("check %s %s", condition ? "PASS" : "FAIL", what);
    return condition;
}

static void autotest(const Config *config)
{
    komi_set_watchdog_seconds(120);
    unsigned failures = 0;
    app.screen = SCREEN_HOME;
    shot_screen("shot-01-home.bmp");

    /* Kana to kanji, then search with the first candidate. */
    const char *kana = config->kana[0] != '\0' ? config->kana : "ねこ";
    char chosen[256] = {0};
    choose_candidate(kana, chosen, sizeof chosen, "shot-02-convert.bmp");
    failures += !check(chosen[0] != '\0' && !kanji_has_hiragana(chosen),
                       "kana converted to kanji");
    const char *query = config->query[0] != '\0' ? config->query : chosen;
    failures += !check(run_search(query), "search returned results");
    shot_screen("shot-03-results.bmp");
    ListView *list = top_list();
    if (list == NULL) goto done;

    /* Pages: R loads page 2, L returns to page 1. */
    size_t first_count = list->pages[0]->count;
    turn_page(list, 1);
    failures += !check(list->page == 1 && list->pages[1]->count > 0,
                       "R loads the next page");
    shot_screen("shot-04-page2.bmp");
    turn_page(list, -1);
    failures += !check(list->page == 0
                       && list->pages[0]->count == first_count,
                       "L returns to page 1");

    /* Related videos of the first result, then back. */
    YtVideo first = list->pages[0]->videos[0];
    failures += !check(open_related(&first) && top_list()->related
                       && current_page(top_list())->count > 0,
                       "related videos load");
    shot_screen("shot-05-related.bmp");
    pop_list();
    failures += !check(app.screen == SCREEN_LIST && !top_list()->related,
                       "back returns to the results");

    /* Play, like during playback, and the mylist. */
    size_t before = mylist.count;
    AutoPlay play = {true, (uint64_t) config->play_seconds * 1000000u, true,
                     "shot-06-player.bmp"};
    play_video(&first, &play);
    failures += !check(mylist_find(&mylist, first.id) == 0,
                       "liked during playback is first in mylist");
    failures += !check(mylist.count == before + 1, "mylist grew by one");
    char path[256];
    snprintf(path, sizeof path, "%s", mylist.path);
    mylist_load(&mylist, path);
    failures += !check(mylist_find(&mylist, first.id) == 0,
                       "mylist file holds the like");
    app.screen = SCREEN_MYLIST;
    app.mylist_selection = 0;
    shot_screen("shot-07-mylist.bmp");
    failures += !check(mylist_remove(&mylist, first.id)
                       && mylist.count == before,
                       "remove from mylist");
done:
    komi_result("AUTOTEST %s failures=%u heap-used=%u",
                failures == 0 ? "PASS" : "FAIL", failures, komi_heap_used());
}

/* --- main --- */

static void boot_message(const char *line1, const char *line2)
{
    uint16_t *vram = begin_frame();
    if (vram == NULL) return;
    draw_bars(vram, "", "");
    draw_center(vram, line1, line2);
    (void) psp_display_publish(&komi.display);
}

static void list_input(void)
{
    ListView *list = top_list();
    Page *page = list->pages[list->page];
    if (pressed(PSP_CTRL_UP) && list->selection > 0) list->selection--;
    if (pressed(PSP_CTRL_DOWN) && list->selection + 1 < (int) page->count)
        list->selection++;
    if (pressed(PSP_CTRL_RTRIGGER) && !list->related) turn_page(list, 1);
    if (pressed(PSP_CTRL_LTRIGGER) && !list->related) turn_page(list, -1);
    if (pressed(BTN_BACK)) {
        pop_list();
        return;
    }
    if (pressed(PSP_CTRL_START)) {
        ask_and_search();
        return;
    }
    if (page->count == 0) return;
    const YtVideo *video = &page->videos[list->selection];
    if (pressed(PSP_CTRL_TRIANGLE)) toggle_like(video);
    if (pressed(PSP_CTRL_SQUARE)) {
        YtVideo copy = *video;
        open_related(&copy);
        return;
    }
    if (pressed(BTN_OK)) play_then_maybe_related(video);
}

int main(int argc, char **argv)
{
    komi_log_open(argc > 0 ? argv[0] : NULL, "ptube.log");
    komi_result("start app version=6 safety=search-timeout-home-exit argv0=%s", komi.argv0);
    power_load();
    Config config;
    load_config(&config);
    komi_platform_init(false);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    read_button_setting();
    ptube_set_back_button(BTN_BACK);

    budget_init(&ui_budget, 4u * 1024u * 1024u);
    char path[256];
    komi_sibling_path(path, sizeof path, "fonts/TilefinchSans-Regular.ttf");
    komi_result("font %s %s", path,
                ui_init(&ui_budget, path) ? "loaded" : "missing");
    komi_sibling_path(path, sizeof path, "ptube-bookmarks.txt");
    mylist_load(&mylist, path);
    komi_result("mylist %u videos (%s)", (unsigned) mylist.count, path);

    boot_message("Connecting to Wi-Fi...", NULL);
    if (!komi_services_init()) {
        boot_message("Could not connect to Wi-Fi",
                     "Check the WLAN switch and saved connection.");
        uint64_t until = komi_now_us() + 6000000u;
        while (komi_now_us() < until) {
            komi_progress("offline");
            sceDisplayWaitVblankStart();
        }
        komi_log_close("offline");
        sceKernelExitGame();
        return 0;
    }
    komi_result("ready heap-used=%u", komi_heap_used());

    if (config.autotest) {
        autotest(&config);
        komi_log_close("autotest");
        sceKernelExitGame();
        return 0;
    }

    /* Interactive: nothing is expected to happen without the user, so the
       watchdog only guards against a truly stuck frame. */
    komi_set_watchdog_seconds(90);
    app.buttons = ~0u;
    for (;;) {
        komi_progress("menu");
        power_apply(false, false);
        read_input();
        switch (app.screen) {
        case SCREEN_HOME:
            if (pressed(PSP_CTRL_START)) goto quit;
            if (pressed(PSP_CTRL_UP)) app.home_selection = (app.home_selection + 3) % 4;
            if (pressed(PSP_CTRL_DOWN)) app.home_selection = (app.home_selection + 1) % 4;
            if (pressed(PSP_CTRL_TRIANGLE)
                || (pressed(BTN_OK) && app.home_selection == 1)) {
                ask_and_search();
            } else if (pressed(BTN_OK) && app.home_selection == 0) {
                open_discover();
            } else if (pressed(BTN_OK) && app.home_selection == 3) {
                power_cycle();
            } else if (pressed(BTN_OK)) {
                app.screen = SCREEN_MYLIST;
                app.mylist_selection = 0;
            }
            break;
        case SCREEN_LIST:
            if (pressed(PSP_CTRL_SELECT) && top_list()->discovery) {
                /* Keep the current feed if the replacement fails. */
                size_t old_depth = app.list_depth;
                if (open_discover() && app.list_depth == old_depth + 1) {
                    free_list(&app.lists[old_depth-1]);
                    app.lists[old_depth-1] = app.lists[old_depth];
                    memset(&app.lists[old_depth], 0, sizeof app.lists[0]);
                    app.list_depth--;
                }
                app.buttons = ~0u;
            } else list_input();
            break;
        case SCREEN_MYLIST:
            if (pressed(PSP_CTRL_UP) && app.mylist_selection > 0)
                app.mylist_selection--;
            if (pressed(PSP_CTRL_DOWN)
                && app.mylist_selection + 1 < (int) mylist.count)
                app.mylist_selection++;
            if (pressed(BTN_BACK)) {
                app.screen = app.list_depth > 0 ? SCREEN_LIST : SCREEN_HOME;
                break;
            }
            if (mylist.count > 0) {
                YtVideo copy = mylist.items[app.mylist_selection];
                if (pressed(PSP_CTRL_TRIANGLE)) {
                    mylist_remove(&mylist, copy.id);
                    toast("Removed from bookmarks");
                    if (app.mylist_selection >= (int) mylist.count
                        && app.mylist_selection > 0)
                        app.mylist_selection--;
                } else if (pressed(PSP_CTRL_SQUARE)) {
                    open_related(&copy);
                } else if (pressed(BTN_OK)) {
                    play_then_maybe_related(&copy);
                }
            }
            break;
        }
        present_screen();
    }
quit:
    psp_media_shutdown(&komi.media);
    komi_result("quit");
    komi_log_close("quit");
    sceKernelExitGame();
    return 0;
}
