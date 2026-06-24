/*
 * Mesen2 OSD core - ImGui-based on-screen display overlay.
 *
 * Two rendering layers called from the same ImGui frame:
 *   1. osd_hud_draw()     — always-on HUD (FPS, messages, status icons)
 *   2. osd_core_build_ui() — toggleable OSD menu (File/Game tabs)
 */
#include "osd_core.hpp"
#include "Core/Shared/SettingTypes.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <string>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */
enum {
    OSD_FONT_SIZE     = 13,
    OSD_LOG_LINES     = 64,
    OSD_LOG_LINE_LEN  = 256
};

static constexpr float OSD_MAX_SCALE        = 6.0f;
static constexpr float OSD_REF_WIDTH        = 768.0f;
static constexpr float OSD_REF_HEIGHT       = 576.0f;

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */
enum OsdView {
    VIEW_MENU,
    VIEW_LOG
};

enum OsdTab {
    TAB_FILE,
    TAB_GAME,
    TAB_COUNT
};

static OsdView   current_view   = VIEW_MENU;
static OsdTab    current_tab    = TAB_FILE;
static int       menu_sel       = -1;

static char      osd_title[512] = "";
static osd_host_t osd_host       = { nullptr, nullptr, nullptr };
static osd_emu_state_t osd_emu  = {};
static float      osd_layout_scale = 1.0f;
static float      osd_font_raster_scale = 1.0f;
static ImGuiStyle osd_style_base;

/* ------------------------------------------------------------------ */
/*  Label localization (key → translated string)                       */
/* ------------------------------------------------------------------ */
static std::unordered_map<std::string, std::string> osd_labels;

void osd_core_set_label(const char *key, const char *value)
{
    if (key && value) osd_labels[key] = value;
}

/* Look up a translated label by key.  Returns the key itself when no
   translation has been registered (acts as the English fallback). */
static const char *osd_label(const char *key)
{
    if (!key) return "";
    auto it = osd_labels.find(key);
    if (it != osd_labels.end()) return it->second.c_str();
    return key;
}
static bool       osd_style_ready = false;

/* ------------------------------------------------------------------ */
/*  Log ring buffer (for message notifications)                        */
/* ------------------------------------------------------------------ */
struct HudMessage {
    char    text[OSD_LOG_LINE_LEN];
    float   time_remaining;  // seconds until fade-out
};

static HudMessage  hud_messages[8];
static int         hud_msg_count = 0;
static std::mutex  log_mutex;

static char        log_ring[OSD_LOG_LINES][OSD_LOG_LINE_LEN];
static int         log_ring_head  = 0;
static int         log_ring_count = 0;

static void show_main_menu(void);

/* ------------------------------------------------------------------ */
/*  Layout helpers                                                     */
/* ------------------------------------------------------------------ */
static float clamp_layout_scale(float scale)
{
    if (scale < 0.25f) return 0.25f;
    if (scale > OSD_MAX_SCALE) return OSD_MAX_SCALE;
    return std::round(scale * 4.0f) / 4.0f;
}

float osd_core_layout_scale_for_output(int output_w, int output_h)
{
    if (output_w <= 0 || output_h <= 0) return 1.0f;
    const float x_scale = (float)output_w / OSD_REF_WIDTH;
    const float y_scale = (float)output_h / OSD_REF_HEIGHT;
    return clamp_layout_scale(std::min(x_scale, y_scale));
}

float osd_core_scaled(float value)
{
    return (value > 0.0f) ? (value * osd_layout_scale) : value;
}

static void apply_layout_scale(void)
{
    if (!osd_style_ready || ImGui::GetCurrentContext() == nullptr) return;
    ImGuiStyle &s = ImGui::GetStyle();
    s = osd_style_base;
    s.ScaleAllSizes(osd_layout_scale);
    s.FontScaleMain = osd_layout_scale / std::max(1.0f, osd_font_raster_scale);
}

void osd_core_set_layout_scale(float scale)
{
    osd_layout_scale = clamp_layout_scale(scale);
    apply_layout_scale();
}

int osd_core_default_font_size(void) { return OSD_FONT_SIZE; }

void osd_core_rebuild_default_font(int pixel_size)
{
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig cfg;
    if (pixel_size < OSD_FONT_SIZE) pixel_size = OSD_FONT_SIZE;
    io.Fonts->Clear();
    cfg.PixelSnapH  = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.SizePixels  = (float)pixel_size;
    io.Fonts->AddFontDefault(&cfg);
    osd_font_raster_scale = (float)pixel_size / (float)OSD_FONT_SIZE;
    apply_layout_scale();
}

/* ------------------------------------------------------------------ */
/*  Emulator state                                                     */
/* ------------------------------------------------------------------ */
void osd_core_set_emu_state(const osd_emu_state_t *state)
{
    if (state) osd_emu = *state;
}

/* ------------------------------------------------------------------ */
/*  Log / message push                                                 */
/* ------------------------------------------------------------------ */
void osd_core_log_push(const char *line)
{
    if (!line) return;
    std::lock_guard<std::mutex> lock(log_mutex);

    // Add to HUD message display (visible for 3 seconds)
    if (hud_msg_count < 8) {
        strncpy(hud_messages[hud_msg_count].text, line, OSD_LOG_LINE_LEN - 1);
        hud_messages[hud_msg_count].text[OSD_LOG_LINE_LEN - 1] = '\0';
        hud_messages[hud_msg_count].time_remaining = 3.0f;
        hud_msg_count++;
    }

    // Also keep in ring buffer for log viewer
    strncpy(log_ring[log_ring_head], line, OSD_LOG_LINE_LEN - 1);
    log_ring[log_ring_head][OSD_LOG_LINE_LEN - 1] = '\0';
    int len = (int)strlen(log_ring[log_ring_head]);
    while (len > 0 && (log_ring[log_ring_head][len-1] == '\n'
                    || log_ring[log_ring_head][len-1] == '\r'))
        log_ring[log_ring_head][--len] = '\0';
    log_ring_head = (log_ring_head + 1) % OSD_LOG_LINES;
    if (log_ring_count < OSD_LOG_LINES) log_ring_count++;
}

void osd_core_show_message(const char *title, const char *message)
{
    if (!title && !message) return;
    char buf[OSD_LOG_LINE_LEN];
    if (title && title[0] && message && message[0]) {
        snprintf(buf, sizeof(buf), "[%s] %s", title, message);
    } else if (title && title[0]) {
        snprintf(buf, sizeof(buf), "%s", title);
    } else if (message && message[0]) {
        snprintf(buf, sizeof(buf), "%s", message);
    } else {
        return;
    }
    osd_core_log_push(buf);
}

/* ------------------------------------------------------------------ */
/*  OSD theme: retro / CRT-inspired                                   */
/* ------------------------------------------------------------------ */
void osd_core_setup_style(void)
{
    osd_style_base      = ImGuiStyle();
    ImGuiStyle &s       = osd_style_base;
    s.WindowRounding    = 2.0f;
    s.FrameRounding     = 1.0f;
    s.ScrollbarRounding = 1.0f;
    s.GrabRounding      = 1.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.ItemSpacing       = ImVec2(6, 4);
    s.WindowPadding     = ImVec2(8, 8);
    s.FramePadding      = ImVec2(4, 2);
    s.ScrollbarSize     = 10.0f;

    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.04f, 0.05f, 0.10f, 0.92f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.04f, 0.05f, 0.10f, 0.92f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.60f, 0.20f, 0.60f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.15f, 0.06f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.30f, 0.10f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.10f, 0.30f, 0.10f, 0.80f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.15f, 0.50f, 0.15f, 0.80f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.20f, 0.60f, 0.20f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.10f, 0.25f, 0.10f, 0.80f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.15f, 0.45f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.20f, 0.60f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.08f, 0.12f, 0.08f, 0.80f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.12f, 0.20f, 0.12f, 0.80f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.15f, 0.30f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.06f, 0.04f, 0.80f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.15f, 0.40f, 0.15f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.20f, 0.55f, 0.20f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.25f, 0.65f, 0.25f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.40f, 1.00f, 0.40f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.25f, 0.50f, 0.25f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.15f, 0.40f, 0.15f, 0.60f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.05f, 0.08f, 0.05f, 0.95f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.30f, 0.80f, 0.30f, 1.00f);
    c[ImGuiCol_Tab]                  = ImVec4(0.06f, 0.15f, 0.06f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.15f, 0.45f, 0.15f, 1.00f);
    c[ImGuiCol_TabActive]            = ImVec4(0.10f, 0.30f, 0.10f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.04f, 0.08f, 0.04f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.06f, 0.15f, 0.06f, 1.00f);

    osd_style_ready = true;
    apply_layout_scale();
}

/* ------------------------------------------------------------------ */
/*  Menu items definition                                              */
/* ------------------------------------------------------------------ */
enum OsdAction {
    ACT_NONE,
    ACT_EXIT,
    ACT_CLOSE_OSD,
    ACT_SHORTCUT
};

struct MenuItem {
    const char *label;
    OsdAction   action;
    int         shortcut;
};

/* ---- File tab ---- */
static const MenuItem file_items[] = {
    { "Open File...",              ACT_SHORTCUT,  (int)EmulatorShortcut::OpenFile          },
    { nullptr,                     ACT_NONE,      0                    },
    { "Save State",                ACT_SHORTCUT,  (int)EmulatorShortcut::SaveState         },
    { "Load State",                ACT_SHORTCUT,  (int)EmulatorShortcut::LoadState         },
    { "Load Last Session",         ACT_SHORTCUT,  (int)EmulatorShortcut::LoadLastSession   },
    { nullptr,                     ACT_NONE,      0                    },
    { "Save State  Slot 1",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot1    },
    { "Save State  Slot 2",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot2    },
    { "Save State  Slot 3",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot3    },
    { "Save State  Slot 4",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot4    },
    { "Save State  Slot 5",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot5    },
    { "Save State  Slot 6",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot6    },
    { "Save State  Slot 7",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot7    },
    { "Save State  Slot 8",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot8    },
    { "Save State  Slot 9",       ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot9    },
    { "Save State  Slot 10",      ACT_SHORTCUT,  (int)EmulatorShortcut::SaveStateSlot10   },
    { nullptr,                     ACT_NONE,      0                    },
    { "Load State  Slot 1",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot1    },
    { "Load State  Slot 2",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot2    },
    { "Load State  Slot 3",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot3    },
    { "Load State  Slot 4",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot4    },
    { "Load State  Slot 5",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot5    },
    { "Load State  Slot 6",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot6    },
    { "Load State  Slot 7",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot7    },
    { "Load State  Slot 8",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot8    },
    { "Load State  Slot 9",       ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot9    },
    { "Load State  Slot 10",      ACT_SHORTCUT,  (int)EmulatorShortcut::LoadStateSlot10   },
    { nullptr,                     ACT_NONE,      0                    },
    { "Exit",                      ACT_SHORTCUT,  (int)EmulatorShortcut::Exit              },
};
static constexpr int FILE_COUNT = (int)(sizeof(file_items) / sizeof(file_items[0]));

/* ---- Game tab ---- */
static const MenuItem game_items[] = {
    { "Pause",                     ACT_SHORTCUT,  (int)EmulatorShortcut::Pause             },
    { "Reset",                     ACT_SHORTCUT,  (int)EmulatorShortcut::Reset             },
    { "Power Cycle",               ACT_SHORTCUT,  (int)EmulatorShortcut::PowerCycle        },
    { "Reload ROM",                ACT_SHORTCUT,  (int)EmulatorShortcut::ReloadRom          },
    { "Power Off",                 ACT_SHORTCUT,  (int)EmulatorShortcut::PowerOff           },
};
static constexpr int GAME_COUNT = (int)(sizeof(game_items) / sizeof(game_items[0]));

/* ---- Per-tab data ---- */
struct TabDef {
    const char    *name;   // key for osd_label()
    const MenuItem *items;
    int            count;
};

static const TabDef tabs[TAB_COUNT] = {
    { "File", file_items, FILE_COUNT },
    { "Game", game_items, GAME_COUNT },
};

/* ------------------------------------------------------------------ */
/*  Menu helpers                                                       */
/* ------------------------------------------------------------------ */
static int item_find_selectable(const MenuItem *items, int count, int start, int step)
{
    for (int i = start; i >= 0 && i < count; i += step)
        if (items[i].label) return i;
    return -1;
}

static int item_first(const MenuItem *items, int count)
{
    return item_find_selectable(items, count, 0, 1);
}

static int item_last(const MenuItem *items, int count)
{
    return item_find_selectable(items, count, count - 1, -1);
}

static void menu_normalize_selection(void)
{
    const TabDef &tab = tabs[current_tab];
    if (menu_sel < 0 || menu_sel >= tab.count || !tab.items[menu_sel].label)
        menu_sel = item_first(tab.items, tab.count);
}

static void show_main_menu(void)
{
    current_view = VIEW_MENU;
    menu_sel = -1;
    menu_normalize_selection();
}

static void activate_menu_item(const MenuItem &mi, bool *close_osd)
{
    switch (mi.action) {
        case ACT_EXIT:
            if (osd_host.request_exit) osd_host.request_exit();
            *close_osd = true;
            return;
        case ACT_CLOSE_OSD:
            *close_osd = true;
            return;
        case ACT_SHORTCUT:
            if (osd_host.execute_shortcut) osd_host.execute_shortcut(mi.shortcut);
            *close_osd = true;
            return;
        case ACT_NONE: break;
    }
}

/* ------------------------------------------------------------------ */
/*  Draw item list                                                     */
/* ------------------------------------------------------------------ */
static void draw_item_list(const MenuItem *items, int count, bool *close_osd)
{
    for (int i = 0; i < count; i++) {
        const MenuItem &mi = items[i];
        if (!mi.label) {
            ImGui::Separator();
            continue;
        }
        const bool selected = (i == menu_sel);
        if (ImGui::Selectable(osd_label(mi.label), selected)) {
            menu_sel = i;
            activate_menu_item(mi, close_osd);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Text with black outline (matches SystemHud::DrawString style)      */
/* ------------------------------------------------------------------ */
static void DrawTextOutlined(const char *text, float x, float y, ImU32 fg = IM_COL32(255,255,255,255))
{
    // Draw black shadow in 8 directions, then colored text on top.
    // Matches the 3x3 outline from SystemHud::DrawString.
    // Uses GetForegroundDrawList() to avoid window clipping issues.
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const ImU32 shadow = IM_COL32(0, 0, 0, 255);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) dl->AddText(ImVec2(x + dx, y + dy), shadow, text);
        }
    }
    dl->AddText(ImVec2(x, y), fg, text);
}

/* ------------------------------------------------------------------ */
/*  Debug statistics state                                             */
/* ------------------------------------------------------------------ */
static osd_debug_stats_t osd_debug = {};

void osd_core_set_debug_stats(const osd_debug_stats_t *stats)
{
    if (stats) osd_debug = *stats;
}

/* ------------------------------------------------------------------ */
/*  Controller display state                                           */
/* ------------------------------------------------------------------ */
static osd_controller_t osd_controllers[OSD_MAX_CONTROLLERS];
static int              osd_controller_count = 0;
static osd_input_prefs_t osd_input_prefs = {};

void osd_core_set_controllers(const osd_controller_t *controllers, int count)
{
    if (count > OSD_MAX_CONTROLLERS) count = OSD_MAX_CONTROLLERS;
    for (int i = 0; i < count; i++)
        osd_controllers[i] = controllers[i];
    osd_controller_count = count;
}

void osd_core_set_input_prefs(const osd_input_prefs_t *prefs)
{
    if (prefs) osd_input_prefs = *prefs;
}

/* ------------------------------------------------------------------ */
/*  Audio player display state                                         */
/* ------------------------------------------------------------------ */
static osd_audio_player_t osd_audio_player = {};
static bool osd_audio_player_active = false;

void osd_core_set_audio_player(const osd_audio_player_t *player)
{
    if (player) {
        osd_audio_player = *player;
        osd_audio_player_active = true;
    } else {
        osd_audio_player_active = false;
    }
}

/* ------------------------------------------------------------------ */
/*  Controller display – data-driven layout                           */
/* ------------------------------------------------------------------ */

/* Element types that can appear in a controller layout. */
enum OsdElemType : uint8_t {
    OSD_ELEM_RECT,       /* filled rectangle (dpad arm, capsule button) */
    OSD_ELEM_CIRCLE,     /* filled circle (action button) */
    OSD_ELEM_LCD,        /* grey semi-transparent LCD decoration area   */
    OSD_ELEM_LABEL,      /* player number text (button_idx unused)      */
};

/* One visual element in a controller layout.  All coordinates are in
   the controller's local coordinate space (0,0 = top-left of the
   controller body), measured in design pixels that get multiplied by
   `scale` at draw time.  Negative button_idx means "always draw as
   off-colour" (e.g. dpad centre). */
struct OsdCtrlElem {
    OsdElemType type;
    int8_t      button_idx;  /* -1 = always off (decorative) */
    float       x, y;        /* centre (circle) or top-left (rect) */
    float       w, h;        /* width/height (rect) or radius/radius (circle) */
};

/* Layout descriptor: body size + array of elements. */
struct OsdCtrlLayout {
    float         body_w, body_h;
    float         label_x, label_y;   /* player-number text position */
    const OsdCtrlElem *elems;
    int           elem_count;
};

/* ---- NES layout ---- */
static const OsdCtrlElem nes_elems[] = {
    /* D-pad arms  (center=14,14  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  11,  6,  6, 6 },   /* Up    */
    { OSD_ELEM_RECT, 1,  11, 16,  6, 6 },   /* Down  */
    { OSD_ELEM_RECT, 2,   6, 11,  6, 6 },   /* Left  */
    { OSD_ELEM_RECT, 3,  16, 11,  6, 6 },   /* Right */
    { OSD_ELEM_RECT,-1,  11, 11,  6, 6 },   /* centre (decorative) */
    /* Select / Start capsules */
    { OSD_ELEM_RECT, 4,  28, 12,  8, 4 },
    { OSD_ELEM_RECT, 5,  40, 12,  8, 4 },
    /* B / A circles */
    { OSD_ELEM_CIRCLE, 6, 52, 14,  5, 0 },
    { OSD_ELEM_CIRCLE, 7, 64, 14,  5, 0 },
};

static const OsdCtrlLayout nes_layout = {
    70.0f, 28.0f,  30.0f, 2.0f,
    nes_elems, (int)(sizeof(nes_elems) / sizeof(nes_elems[0]))
};

/* ---- SNES layout ---- */
static const OsdCtrlElem snes_elems[] = {
    /* L / R shoulder bars */
    { OSD_ELEM_RECT, 10,  4,  1, 12, 4 },
    { OSD_ELEM_RECT, 11, 64,  1, 12, 4 },
    /* D-pad arms  (center=16,18  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  13, 10,  6, 6 },   /* Up    */
    { OSD_ELEM_RECT, 1,  13, 20,  6, 6 },   /* Down  */
    { OSD_ELEM_RECT, 2,   8, 15,  6, 6 },   /* Left  */
    { OSD_ELEM_RECT, 3,  18, 15,  6, 6 },   /* Right */
    { OSD_ELEM_RECT,-1,  13, 15,  6, 6 },   /* centre (decorative) */
    /* Select / Start capsules */
    { OSD_ELEM_RECT, 4,  32, 16,  8, 4 },
    { OSD_ELEM_RECT, 5,  44, 16,  8, 4 },
    /* Diamond: X top, Y left, A right, B bottom  (center=67,18  r=8) */
    { OSD_ELEM_CIRCLE, 9, 67, 10,  4, 0 },   /* X (top)    */
    { OSD_ELEM_CIRCLE, 8, 59, 18,  4, 0 },   /* Y (left)   */
    { OSD_ELEM_CIRCLE, 7, 75, 18,  4, 0 },   /* A (right)  */
    { OSD_ELEM_CIRCLE, 6, 67, 26,  4, 0 },   /* B (bottom) */
};

static const OsdCtrlLayout snes_layout = {
    80.0f, 32.0f,  34.0f, 4.0f,
    snes_elems, (int)(sizeof(snes_elems) / sizeof(snes_elems[0]))
};

/* ---- GBA layout ----  D-pad + Select/Start + B/A + L/R
   buttons: 0=Up 1=Down 2=Left 3=Right 4=Select 5=Start 6=B 7=A 10=L 11=R */
static const OsdCtrlElem gba_elems[] = {
    /* L / R shoulder bars */
    { OSD_ELEM_RECT, 10,  4,  1, 12, 4 },
    { OSD_ELEM_RECT, 11, 64,  1, 12, 4 },
    /* D-pad arms  (center=16,18  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  13, 10,  6, 6 },
    { OSD_ELEM_RECT, 1,  13, 20,  6, 6 },
    { OSD_ELEM_RECT, 2,   8, 15,  6, 6 },
    { OSD_ELEM_RECT, 3,  18, 15,  6, 6 },
    { OSD_ELEM_RECT,-1,  13, 15,  6, 6 },
    /* Select / Start capsules */
    { OSD_ELEM_RECT, 4,  28, 16,  8, 4 },
    { OSD_ELEM_RECT, 5,  40, 16,  8, 4 },
    /* B / A circles */
    { OSD_ELEM_CIRCLE, 6, 56, 18,  5, 0 },
    { OSD_ELEM_CIRCLE, 7, 68, 18,  5, 0 },
};

static const OsdCtrlLayout gba_layout = {
    80.0f, 32.0f,  34.0f, 4.0f,
    gba_elems, (int)(sizeof(gba_elems) / sizeof(gba_elems[0]))
};

/* ---- PCE layout ----  D-pad + Select/Run + I/II
   buttons: 0=Up 1=Down 2=Left 3=Right 4=Select 5=Run 6=I 7=II */
static const OsdCtrlElem pce_elems[] = {
    /* D-pad arms  (center=14,14  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  11,  6,  6, 6 },
    { OSD_ELEM_RECT, 1,  11, 16,  6, 6 },
    { OSD_ELEM_RECT, 2,   6, 11,  6, 6 },
    { OSD_ELEM_RECT, 3,  16, 11,  6, 6 },
    { OSD_ELEM_RECT,-1,  11, 11,  6, 6 },
    /* Select / Run capsules */
    { OSD_ELEM_RECT, 4,  26, 12,  8, 4 },
    { OSD_ELEM_RECT, 5,  38, 12,  8, 4 },
    /* I / II circles */
    { OSD_ELEM_CIRCLE, 7, 54, 14,  5, 0 },
    { OSD_ELEM_CIRCLE, 6, 66, 14,  5, 0 },
};

static const OsdCtrlLayout pce_layout = {
    76.0f, 28.0f,  30.0f, 2.0f,
    pce_elems, (int)(sizeof(pce_elems) / sizeof(pce_elems[0]))
};

/* ---- SMS layout ----  D-pad + Pause + B/A
   buttons: 0=Up 1=Down 2=Left 3=Right 5=Pause 6=B 7=A */
static const OsdCtrlElem sms_elems[] = {
    /* D-pad arms  (center=14,14  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  11,  6,  6, 6 },
    { OSD_ELEM_RECT, 1,  11, 16,  6, 6 },
    { OSD_ELEM_RECT, 2,   6, 11,  6, 6 },
    { OSD_ELEM_RECT, 3,  16, 11,  6, 6 },
    { OSD_ELEM_RECT,-1,  11, 11,  6, 6 },
    /* Pause capsule */
    { OSD_ELEM_RECT, 5,  28, 12,  8, 4 },
    /* B / A circles */
    { OSD_ELEM_CIRCLE, 6, 46, 14,  5, 0 },
    { OSD_ELEM_CIRCLE, 7, 58, 14,  5, 0 },
};

static const OsdCtrlLayout sms_layout = {
    66.0f, 28.0f,  26.0f, 2.0f,
    sms_elems, (int)(sizeof(sms_elems) / sizeof(sms_elems[0]))
};

/* ---- WS layout ----  2×D-pad + Sound/Start + B/A
   buttons: 0=Up 1=Down 2=Left 3=Right 4=Up2 5=Down2 6=Left2 7=Right2
             8=Sound 9=Start 10=B 11=A */
static const OsdCtrlElem ws_elems[] = {
    /* Left D-pad  (center=14,18  bs=6  gap=2) */
    { OSD_ELEM_RECT, 0,  11, 10,  6, 6 },
    { OSD_ELEM_RECT, 1,  11, 20,  6, 6 },
    { OSD_ELEM_RECT, 2,   6, 15,  6, 6 },
    { OSD_ELEM_RECT, 3,  18, 15,  6, 6 },
    { OSD_ELEM_RECT,-1,  11, 15,  6, 6 },
    /* Right D-pad  (center=56,18  bs=6  gap=2) */
    { OSD_ELEM_RECT, 4,  53, 10,  6, 6 },
    { OSD_ELEM_RECT, 5,  53, 20,  6, 6 },
    { OSD_ELEM_RECT, 6,  48, 15,  6, 6 },
    { OSD_ELEM_RECT, 7,  60, 15,  6, 6 },
    { OSD_ELEM_RECT,-1,  53, 15,  6, 6 },
    /* Sound / Start capsules */
    { OSD_ELEM_RECT, 8,  26, 16,  8, 4 },
    { OSD_ELEM_RECT, 9,  38, 16,  8, 4 },
    /* B / A circles */
    { OSD_ELEM_CIRCLE, 10, 26, 26,  4, 0 },
    { OSD_ELEM_CIRCLE, 11, 38, 26,  4, 0 },
};

static const OsdCtrlLayout ws_layout = {
    70.0f, 32.0f,  30.0f, 2.0f,
    ws_elems, (int)(sizeof(ws_elems) / sizeof(ws_elems[0]))
};

/* ---- NDS layout ----  D-pad + Select/Start + diamond + L/R + LCD
   buttons: 0=Up 1=Down 2=Left 3=Right 4=Select 5=Start
             6=B 7=A 8=Y 9=X 10=L 11=R */
static const OsdCtrlElem nds_elems[] = {
    /* L / R shoulder bars */
    { OSD_ELEM_RECT, 10,  6,  1, 14, 4 },
    { OSD_ELEM_RECT, 11, 140, 1, 14, 4 },
    /* D-pad arms  (center=30,28  bs=10  gap=3) */
    { OSD_ELEM_RECT, 0,  25, 14, 10, 10 },
    { OSD_ELEM_RECT, 1,  25, 30, 10, 10 },
    { OSD_ELEM_RECT, 2,  17, 22, 10, 10 },
    { OSD_ELEM_RECT, 3,  33, 22, 10, 10 },
    { OSD_ELEM_RECT,-1,  25, 22, 10, 10 },
    /* LCD decoration area  (top=8, bottom=64-8-48=8) */
    { OSD_ELEM_LCD, -1,  50, 8, 60, 48 },
    /* Diamond: X top, Y left, A right, B bottom  (center=130,26  r=12) */
    { OSD_ELEM_CIRCLE, 9, 130, 14,  6, 0 },
    { OSD_ELEM_CIRCLE, 8, 118, 26,  6, 0 },
    { OSD_ELEM_CIRCLE, 7, 142, 26,  6, 0 },
    { OSD_ELEM_CIRCLE, 6, 130, 38,  6, 0 },
    /* Select / Start capsules (below diamond) */
    { OSD_ELEM_RECT, 4, 118, 50, 10, 5 },
    { OSD_ELEM_RECT, 5, 134, 50, 10, 5 },
};

static const OsdCtrlLayout nds_layout = {
    160.0f, 64.0f,  68.0f, 6.0f,
    nds_elems, (int)(sizeof(nds_elems) / sizeof(nds_elems[0]))
};

/* ---- 3DS layout ----  D-pad + Select/Start + diamond + L/R/ZL/ZR + LCD
   buttons: 0=Up 1=Down 2=Left 3=Right 4=Select 5=Start
             6=B 7=A 8=Y 9=X 10=L 11=R 12=ZL 13=ZR */
static const OsdCtrlElem threeds_elems[] = {
    /* ZL / ZR shoulder bars (outer) */
    { OSD_ELEM_RECT, 12,  4,  1, 14, 4 },
    { OSD_ELEM_RECT, 13, 142, 1, 14, 4 },
    /* L / R shoulder bars (inner) */
    { OSD_ELEM_RECT, 10,  6,  6, 14, 4 },
    { OSD_ELEM_RECT, 11, 140, 6, 14, 4 },
    /* D-pad arms  (center=30,34  bs=10  gap=3) */
    { OSD_ELEM_RECT, 0,  25, 20, 10, 10 },
    { OSD_ELEM_RECT, 1,  25, 36, 10, 10 },
    { OSD_ELEM_RECT, 2,  17, 28, 10, 10 },
    { OSD_ELEM_RECT, 3,  33, 28, 10, 10 },
    { OSD_ELEM_RECT,-1,  25, 28, 10, 10 },
    /* LCD decoration area  (top=10, bottom=72-10-52=10) */
    { OSD_ELEM_LCD, -1,  50, 10, 60, 52 },
    /* Diamond: X top, Y left, A right, B bottom  (center=130,32  r=12) */
    { OSD_ELEM_CIRCLE, 9, 130, 20,  6, 0 },
    { OSD_ELEM_CIRCLE, 8, 118, 32,  6, 0 },
    { OSD_ELEM_CIRCLE, 7, 142, 32,  6, 0 },
    { OSD_ELEM_CIRCLE, 6, 130, 44,  6, 0 },
    /* Select / Start capsules (below diamond) */
    { OSD_ELEM_RECT, 4, 118, 56, 10, 5 },
    { OSD_ELEM_RECT, 5, 134, 56, 10, 5 },
};

static const OsdCtrlLayout threeds_layout = {
    160.0f, 72.0f,  68.0f, 8.0f,
    threeds_elems, (int)(sizeof(threeds_elems) / sizeof(threeds_elems[0]))
};

/* Look up a layout by enum.  Returns nullptr for unknown types. */
static const OsdCtrlLayout *get_ctrl_layout(OsdControllerLayout layout)
{
    switch (layout) {
        case OSD_LAYOUT_NES:   return &nes_layout;
        case OSD_LAYOUT_SNES:  return &snes_layout;
        case OSD_LAYOUT_GBA:   return &gba_layout;
        case OSD_LAYOUT_PCE:   return &pce_layout;
        case OSD_LAYOUT_SMS:   return &sms_layout;
        case OSD_LAYOUT_WS:    return &ws_layout;
        case OSD_LAYOUT_NDS:   return &nds_layout;
        case OSD_LAYOUT_3DS:   return &threeds_layout;
        default:               return nullptr;
    }
}

/* Draw a controller using a data-driven layout (ImGui path). */
static void draw_controller(ImDrawList *dl, float ox, float oy, float scale,
                             const osd_controller_t *ctrl)
{
    const OsdCtrlLayout *L = get_ctrl_layout(ctrl->layout);
    if (!L) return;

    const float s = scale;
    const ImU32 bg  = IM_COL32(0, 0, 0, 160);
    const ImU32 off = IM_COL32(60, 60, 60, 255);
    const ImU32 on  = IM_COL32(255, 255, 255, 255);

    /* Body */
    dl->AddRectFilled(ImVec2(ox, oy),
                      ImVec2(ox + L->body_w * s, oy + L->body_h * s),
                      bg, 4.0f * s);
    dl->AddRect(ImVec2(ox, oy),
                ImVec2(ox + L->body_w * s, oy + L->body_h * s),
                IM_COL32(100,100,100,255), 4.0f * s);

    /* Elements */
    for (int i = 0; i < L->elem_count; i++) {
        const OsdCtrlElem &e = L->elems[i];

        if (e.type == OSD_ELEM_LCD) {
            /* Grey semi-transparent LCD decoration area */
            float ex = ox + e.x * s, ey = oy + e.y * s;
            float ew = e.w * s, eh = e.h * s;
            dl->AddRectFilled(ImVec2(ex, ey), ImVec2(ex + ew, ey + eh),
                              IM_COL32(80, 80, 80, 100), 2.0f * s);
            dl->AddRect(ImVec2(ex, ey), ImVec2(ex + ew, ey + eh),
                        IM_COL32(120, 120, 120, 140), 2.0f * s);
            continue;
        }

        bool pressed = (e.button_idx >= 0) ? ctrl->buttons[e.button_idx] : false;
        ImU32 col = pressed ? on : off;

        if (e.type == OSD_ELEM_RECT) {
            float ex = ox + e.x * s, ey = oy + e.y * s;
            float ew = e.w * s, eh = e.h * s;
            dl->AddRectFilled(ImVec2(ex, ey), ImVec2(ex + ew, ey + eh), col, 1.0f * s);
        } else if (e.type == OSD_ELEM_CIRCLE) {
            dl->AddCircleFilled(ImVec2(ox + e.x * s, oy + e.y * s),
                                e.w * s, col, 12);
        }
    }

    /* Player label */
    char num[4];
    snprintf(num, sizeof(num), "P%d", ctrl->port + 1);
    DrawTextOutlined(num, ox + L->label_x * s, oy + L->label_y * s,
                     IM_COL32(180,180,180,255));
}

/* ------------------------------------------------------------------ */
/*  HUD: Always-on layer (FPS, messages, status icons)                 */
/*  Called every frame, regardless of OSD menu visibility.             */
/* ------------------------------------------------------------------ */
void osd_hud_draw(void)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 work_pos  = vp->WorkPos;
    const ImVec2 work_size = vp->WorkSize;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    float lineH = ImGui::GetTextLineHeight();
    char buf[64];

    /* ---- Top-right counters ---- */
    if (osd_emu.is_running) {
        float y = work_pos.y + 10.0f;
        float x_right = work_pos.x + work_size.x - 10.0f;

        if (osd_emu.show_fps) {
            snprintf(buf, sizeof(buf), "FPS: %d", osd_emu.fps);
            ImVec2 size = ImGui::CalcTextSize(buf);
            DrawTextOutlined(buf, x_right - size.x, y);
            y += lineH;
        }
        if (osd_emu.show_game_timer) {
            uint32_t seconds = (uint32_t)(osd_emu.frame_count / osd_emu.fps_rate) % 60;
            uint32_t minutes = (uint32_t)(osd_emu.frame_count / osd_emu.fps_rate / 60) % 60;
            uint32_t hours   = (uint32_t)(osd_emu.frame_count / osd_emu.fps_rate / 3600);
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hours, minutes, seconds);
            ImVec2 size = ImGui::CalcTextSize(buf);
            DrawTextOutlined(buf, x_right - size.x, y);
            y += lineH;
        }
        if (osd_emu.show_frame_counter) {
            snprintf(buf, sizeof(buf), "Frame: %u", osd_emu.frame_count);
            ImVec2 size = ImGui::CalcTextSize(buf);
            DrawTextOutlined(buf, x_right - size.x, y);
            y += lineH;
        }
        if (osd_emu.show_lag_counter) {
            snprintf(buf, sizeof(buf), "Lag: %u", osd_emu.lag_count);
            ImVec2 size = ImGui::CalcTextSize(buf);
            DrawTextOutlined(buf, x_right - size.x, y);
            y += lineH;
        }
    }

    /* ---- Top-left status icons ---- */
    if (osd_emu.is_running) {
        float x = work_pos.x + 10.0f;
        float y = work_pos.y + 10.0f;

        if (osd_emu.is_paused) {
            ImU32 color = IM_COL32(255, 255, 255, 255);
            dl->AddRectFilled(ImVec2(x, y),      ImVec2(x + 5, y + 12), color);
            dl->AddRectFilled(ImVec2(x + 7, y),  ImVec2(x + 12, y + 12), color);
        } else if (osd_emu.show_movie_icons && osd_emu.is_movie_playing) {
            ImU32 color = IM_COL32(255, 255, 255, 255);
            dl->AddTriangleFilled(
                ImVec2(x + 2, y),      ImVec2(x + 2, y + 12),
                ImVec2(x + 12, y + 6), color);
        } else if (osd_emu.show_movie_icons && osd_emu.is_movie_recording) {
            ImU32 color = IM_COL32(255, 0, 0, 255);
            dl->AddCircleFilled(ImVec2(x + 6, y + 6), 6.0f, color);
        }

        if (!osd_emu.is_paused && osd_emu.show_turbo_rewind_icons) {
            float ix = x + 14.0f;
            if (osd_emu.is_rewind) {
                ImU32 color = IM_COL32(255, 200, 128, 255);
                dl->AddTriangleFilled(
                    ImVec2(ix + 6, y),      ImVec2(ix, y + 6),
                    ImVec2(ix + 6, y + 12), color);
                dl->AddTriangleFilled(
                    ImVec2(ix + 12, y),     ImVec2(ix + 6, y + 6),
                    ImVec2(ix + 12, y + 12), color);
            } else if (osd_emu.is_turbo) {
                ImU32 color = IM_COL32(128, 255, 128, 255);
                dl->AddTriangleFilled(
                    ImVec2(ix, y),          ImVec2(ix + 6, y + 6),
                    ImVec2(ix, y + 12),     color);
                dl->AddTriangleFilled(
                    ImVec2(ix + 6, y),      ImVec2(ix + 12, y + 6),
                    ImVec2(ix + 6, y + 12), color);
            }
        }
    }

    /* ---- Debug statistics panel ---- */
    if (osd_emu.is_running && osd_emu.show_debug_info) {
        ImDrawList *dl2 = ImGui::GetForegroundDrawList();
        float s = osd_layout_scale;
        float ox = work_pos.x + 10.0f;
        float oy = work_pos.y + 30.0f;
        float lineH = ImGui::GetTextLineHeight();
        float pad = 4.0f * s;

        /* Layout constants
           Audio panel: 130*s wide, 6 text lines (title + Latency + Underruns + Buffer + Rate + pad)
           Video panel: 130*s wide, text + frame-time graph
           Misc panel:  full width below both panels */
        float aw = 130.0f * s;
        float vw = 130.0f * s;
        float gap = 8.0f * s;
        float ah = 68.0f * s;    /* audio: 6 lines + padding */
        float vh = 90.0f * s;    /* video: text + graph */

        /* Audio Stats panel */
        dl2->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + aw, oy + ah),
                           IM_COL32(0, 0, 0, 160), 4.0f * s);
        dl2->AddRect(ImVec2(ox, oy), ImVec2(ox + aw, oy + ah),
                     IM_COL32(100, 100, 100, 255), 4.0f * s);

        float tx = ox + pad;
        float ty = oy + pad;
        char buf[128];

        DrawTextOutlined("Audio Stats", tx, ty, IM_COL32(255, 220, 100, 255));
        ty += lineH;
        snprintf(buf, sizeof(buf), "Latency: %.2f ms", osd_debug.audio_latency);
        ImU32 latColor = (osd_debug.audio_latency > 0 && std::abs(osd_debug.audio_latency - osd_debug.audio_target_latency) > 3) ?
            IM_COL32(255, 80, 80, 255) : IM_COL32(255, 255, 255, 255);
        DrawTextOutlined(buf, tx, ty, latColor);
        ty += lineH;
        snprintf(buf, sizeof(buf), "Underruns: %u", osd_debug.audio_underruns);
        DrawTextOutlined(buf, tx, ty);
        ty += lineH;
        snprintf(buf, sizeof(buf), "Buffer: %u kb", osd_debug.audio_buffer_size / 1024);
        DrawTextOutlined(buf, tx, ty);
        ty += lineH;
        snprintf(buf, sizeof(buf), "Rate: %u Hz", osd_debug.audio_sample_rate);
        DrawTextOutlined(buf, tx, ty);

        /* Video Stats panel (to the right of Audio panel) */
        float vx0 = ox + aw + gap;
        dl2->AddRectFilled(ImVec2(vx0, oy), ImVec2(vx0 + vw, oy + vh),
                           IM_COL32(0, 0, 0, 160), 4.0f * s);
        dl2->AddRect(ImVec2(vx0, oy), ImVec2(vx0 + vw, oy + vh),
                     IM_COL32(100, 100, 100, 255), 4.0f * s);

        float vx = vx0 + pad;
        float vy = oy + pad;
        DrawTextOutlined("Video Stats", vx, vy, IM_COL32(255, 220, 100, 255));
        vy += lineH;
        snprintf(buf, sizeof(buf), "FPS: %.1f", osd_debug.video_fps);
        DrawTextOutlined(buf, vx, vy);
        vy += lineH;
        snprintf(buf, sizeof(buf), "Last: %.2f ms", osd_debug.video_last_frame_ms);
        DrawTextOutlined(buf, vx, vy);
        vy += lineH;
        double minMs = (osd_debug.video_min_frame_ms < 9999) ? osd_debug.video_min_frame_ms : 0.0;
        snprintf(buf, sizeof(buf), "Min: %.2f  Max: %.2f ms", minMs, osd_debug.video_max_frame_ms);
        DrawTextOutlined(buf, vx, vy);

        /* Frame time graph (inside Video panel) */
        float gx = vx0 + pad;
        float gy = vy + lineH + 2.0f * s;
        float gw = vw - 2.0f * pad;
        float gh = 30.0f * s;
        dl2->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), IM_COL32(0, 0, 0, 200));
        dl2->AddRect(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), IM_COL32(80, 80, 80, 255));

        double expectedMs = 1000.0 / std::max(1.0, osd_emu.fps_rate);
        for (int i = 0; i < 59; i++) {
            double d = osd_debug.frame_durations[i];
            double nd = osd_debug.frame_durations[i + 1];
            d = std::min(25.0, std::max(10.0, d));
            nd = std::min(25.0, std::max(10.0, nd));
            ImU32 lc = IM_COL32(0, 255, 0, 255);
            if (std::abs(d - expectedMs) > 2) lc = IM_COL32(255, 0, 0, 255);
            else if (std::abs(d - expectedMs) > 1) lc = IM_COL32(255, 165, 0, 255);
            float x1 = gx + (float)i * gw / 59.0f;
            float x2 = gx + (float)(i + 1) * gw / 59.0f;
            float y1 = gy + gh - (float)((d - 10.0) * gh / 15.0);
            float y2 = gy + gh - (float)((nd - 10.0) * gh / 15.0);
            dl2->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lc);
        }

        /* Misc. Stats panel — below both Audio and Video panels, full width */
        float fullW = aw + gap + vw;
        float my = oy + std::max(ah, vh) + 4.0f * s;
        float mh = 42.0f * s;    /* title + 2 data lines + padding */
        dl2->AddRectFilled(ImVec2(ox, my), ImVec2(ox + fullW, my + mh),
                           IM_COL32(0, 0, 0, 160), 4.0f * s);
        dl2->AddRect(ImVec2(ox, my), ImVec2(ox + fullW, my + mh),
                     IM_COL32(100, 100, 100, 255), 4.0f * s);
        float mx = ox + pad;
        float mty = my + pad;
        DrawTextOutlined("Misc. Stats", mx, mty, IM_COL32(255, 220, 100, 255));
        mty += lineH;
        snprintf(buf, sizeof(buf), "Rewind mem.: %.2f MB", osd_debug.rewind_memory_mb);
        DrawTextOutlined(buf, mx, mty);
        mty += lineH;
        snprintf(buf, sizeof(buf), "Rewind rate: %.2f MB/min", osd_debug.rewind_per_minute_mb);
        DrawTextOutlined(buf, mx, mty);
    }

    /* ---- Controller display (InputHud) ---- */
    if (osd_emu.is_running && osd_controller_count > 0) {
        float ctrl_x, ctrl_y;
        float pad = 10.0f;
        float gap = 4.0f * osd_layout_scale;

        /* Determine max controller dimensions across visible controllers */
        float ctrl_w = 80.0f * osd_layout_scale;
        float ctrl_h = 32.0f * osd_layout_scale;
        for (int i = 0; i < osd_controller_count; i++) {
            if (i >= OSD_MAX_CONTROLLERS || osd_controllers[i].port >= 8 || !osd_input_prefs.display_port[osd_controllers[i].port])
                continue;
            const OsdCtrlLayout *L = get_ctrl_layout(osd_controllers[i].layout);
            if (L) {
                float w = L->body_w * osd_layout_scale;
                float h = L->body_h * osd_layout_scale;
                if (w > ctrl_w) ctrl_w = w;
                if (h > ctrl_h) ctrl_h = h;
            }
        }

        int visible = 0;
        for (int i = 0; i < osd_controller_count; i++) {
            if (i < OSD_MAX_CONTROLLERS && osd_controllers[i].port < 8 && osd_input_prefs.display_port[osd_controllers[i].port])
                visible++;
        }
        if (visible > 0) {
            int pos = osd_input_prefs.display_position;
            if (pos == 0 || pos == 1) {
                ctrl_y = work_pos.y + 30.0f;
                if (pos == 0) ctrl_x = work_pos.x + pad;
                else ctrl_x = work_pos.x + work_size.x - pad - ctrl_w;
            } else {
                float total_h = visible * ctrl_h + (visible - 1) * gap;
                if (osd_input_prefs.display_horizontally) total_h = ctrl_h;
                ctrl_y = work_pos.y + work_size.y - pad - total_h;
                if (pos == 2) ctrl_x = work_pos.x + pad;
                else ctrl_x = work_pos.x + work_size.x - pad - ctrl_w;
            }

            for (int i = 0; i < osd_controller_count; i++) {
                if (i >= OSD_MAX_CONTROLLERS || osd_controllers[i].port >= 8 || !osd_input_prefs.display_port[osd_controllers[i].port])
                    continue;

                const osd_controller_t *c = &osd_controllers[i];
                draw_controller(dl, ctrl_x, ctrl_y, osd_layout_scale, c);

                if (osd_input_prefs.display_horizontally) {
                    ctrl_x += ctrl_w + gap;
                } else {
                    ctrl_y += ctrl_h + gap;
                }
            }
        }
    }

    /* ---- Audio player display ---- */
    if (osd_audio_player_active) {
        const osd_audio_player_t &ap = osd_audio_player;
        float s = osd_layout_scale;

        /* Full-screen overlay at 256x240 logical size, scaled to output */
        float logW = 256.0f * s;
        float logH = 240.0f * s;
        float ox = work_pos.x + (work_size.x - logW) / 2.0f;
        float oy = work_pos.y + (work_size.y - logH) / 2.0f;

        /* Background */
        dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + logW, oy + logH), IM_COL32(0, 0, 0, 255));

        float fontSize = ImGui::GetFontSize();
        float y = oy + 12.0f * s;

        /* Helper to draw a label + value */
        auto drawLabel = [&](const char *label, const char *value) {
            if (value[0] != '\0') {
                ImU32 labelCol = IM_COL32(187, 187, 187, 255);
                ImU32 valCol   = IM_COL32(255, 255, 255, 255);
                dl->AddText(ImVec2(ox + 10.0f * s, y), labelCol, label);
                dl->AddText(ImVec2(ox + 57.0f * s, y), valCol, value);
                y += 10.0f * s;
            }
        };

        drawLabel("Game:", ap.game_title);
        drawLabel("Artist:", ap.artist);
        drawLabel("Comment:", ap.comment);

        /* Track info */
        char trackStr[64];
        snprintf(trackStr, sizeof(trackStr), "Track: %d / %d", ap.track_number, ap.track_count);
        ImVec2 trackSize = ImGui::CalcTextSize(trackStr);
        float trackPosX = ox + logW - trackSize.x - 14.0f * s;
        dl->AddText(ImVec2(trackPosX, oy + 218.0f * s), IM_COL32(255, 255, 255, 255), trackStr);

        /* Track name */
        const char *trackName = ap.song_title[0] != '\0' ? ap.song_title : ap.rom_filename;
        float trackNameEndX = trackPosX - 20.0f * s;
        dl->AddText(ImVec2(ox + 15.0f * s, oy + 208.0f * s), IM_COL32(255, 255, 255, 255), trackName);

        /* Position / progress */
        auto fmtSec = [](double sec) -> string {
            uint32_t s = (uint32_t)sec;
            string seconds = std::to_string(s % 60);
            if (seconds.size() == 1) seconds = "0" + seconds;
            return std::to_string(s / 60) + ":" + seconds;
        };

        if (ap.length <= 0) {
            char posStr[64];
            snprintf(posStr, sizeof(posStr), " %s   ", fmtSec(ap.position).c_str());
            dl->AddText(ImVec2(ox + 215.0f * s, oy + 208.0f * s), IM_COL32(255, 255, 255, 255), posStr);
        } else {
            char posStr[64];
            snprintf(posStr, sizeof(posStr), " %s / %s   ", fmtSec(ap.position).c_str(), fmtSec(ap.length).c_str());
            dl->AddText(ImVec2(ox + 177.0f * s, oy + 208.0f * s), IM_COL32(255, 255, 255, 255), posStr);

            /* Progress bar */
            constexpr int barWidth = 222;
            float bx = ox + 15.0f * s;
            float by = oy + 199.0f * s;
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + (barWidth + 4) * s, by + 6.0f * s), IM_COL32(187, 187, 187, 255));
            double ratio = std::min(1.0, ap.position / ap.length);
            dl->AddRect(ImVec2(bx + 2.0f * s, by + 2.0f * s), ImVec2(bx + (2.0f + ratio * barWidth) * s, by + 4.0f * s), IM_COL32(119, 187, 255, 255));
        }

        /* Spectrum analyzer */
        static constexpr double ranges[8][3] = {
            { 20, 150, 0.5 }, { 150, 400, 0.5 }, { 400, 700, 0.75 }, { 700, 1000, 0.75 },
            { 1000, 2000, 1 }, { 2000, 4000, 1 }, { 4000, 6000, 1.25 }, { 6000, 20000, 1.25 }
        };
        static constexpr int maxVal = 140;
        int top = 191 - maxVal;
        int bottom = 190;
        ImU32 fgColor  = IM_COL32(85, 85, 85, 255);
        ImU32 fgColor2 = IM_COL32(102, 102, 102, 255);
        ImU32 bgColor  = IM_COL32(34, 34, 34, 255);

        float specTop = oy + top * s;
        float specBot = oy + bottom * s;
        dl->AddLine(ImVec2(ox, specTop - s), ImVec2(ox + 255.0f * s, specTop - s), fgColor);
        dl->AddLine(ImVec2(ox, specBot + s), ImVec2(ox + 255.0f * s, specBot + s), fgColor);
        dl->AddRectFilled(ImVec2(ox, specTop), ImVec2(ox + 256.0f * s, specBot + 140.0f * s), bgColor);
        dl->AddLine(ImVec2(ox + 192.0f * s, specTop), ImVec2(ox + 192.0f * s, specBot), fgColor);
        dl->AddLine(ImVec2(ox + 128.0f * s, specTop), ImVec2(ox + 128.0f * s, specBot), fgColor);
        dl->AddLine(ImVec2(ox + 64.0f * s, specTop), ImVec2(ox + 64.0f * s, specBot), fgColor);
        dl->AddLine(ImVec2(ox + 224.0f * s, specTop), ImVec2(ox + 224.0f * s, specBot), fgColor2);
        dl->AddLine(ImVec2(ox + 160.0f * s, specTop), ImVec2(ox + 160.0f * s, specBot), fgColor2);
        dl->AddLine(ImVec2(ox + 96.0f * s, specTop), ImVec2(ox + 96.0f * s, specBot), fgColor2);
        dl->AddLine(ImVec2(ox + 32.0f * s, specTop), ImVec2(ox + 32.0f * s, specBot), fgColor2);

        /* Frequency labels */
        ImU32 lblCol = fgColor;
        ImU32 lblBg  = bgColor;
        auto drawFreqLabel = [&](float fx, float fy, const char *txt) {
            dl->AddText(ImVec2(ox + fx * s, oy + fy * s), lblCol, txt);
        };
        drawFreqLabel(3, top + 10, "20Hz");
        drawFreqLabel(19, top + 30, "150Hz");
        drawFreqLabel(51, top + 10, "400Hz");
        drawFreqLabel(83, top + 30, "700Hz");
        drawFreqLabel(117, top + 10, "1kHz");
        drawFreqLabel(150, top + 30, "2kHz");
        drawFreqLabel(182, top + 10, "4kHz");
        drawFreqLabel(214, top + 30, "6kHz");
        drawFreqLabel(228, top + 10, "20kHz");

        /* Draw spectrum bars */
        if (ap.amplitudes && ap.amplitudes_count > 0) {
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 32; j++) {
                    double freqRange = ranges[i][1] - ranges[i][0];
                    double startFreq = ranges[i][0] + freqRange * j / 32;
                    double endFreq   = ranges[i][0] + freqRange * (j + 1) / 32;

                    int startIndex = (int)(startFreq / ((double)ap.sample_rate / (double)ap.amplitudes_count * 2));
                    int endIndex   = (int)(endFreq / ((double)ap.sample_rate / (double)ap.amplitudes_count * 2));

                    double avgAmp = 0;
                    for (int ai = startIndex; ai <= endIndex && ai < ap.amplitudes_count; ai++) {
                        avgAmp += ap.amplitudes[ai];
                    }
                    avgAmp /= (endIndex - startIndex + 1);
                    avgAmp *= ranges[i][2];
                    avgAmp = std::min<double>(maxVal, avgAmp);

                    int red   = std::min(255, (int)(256 * (avgAmp / maxVal) * 2));
                    int green = std::max(0, std::min(255, (int)(256 * ((maxVal - avgAmp) / maxVal) * 2)));
                    ImU32 barCol = IM_COL32(red, green, 0, 255);

                    float bx2 = ox + (i * 32 + j) * s;
                    float by2 = oy + 190.0f * s;
                    float bh  = (float)avgAmp * s;
                    if (bh >= 1.0f)
                        dl->AddRectFilled(ImVec2(bx2, by2), ImVec2(bx2 + s, by2 - bh), barCol);
                }
            }
        }
    }

    /* ---- Bottom messages (fade out) ---- */
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        // Tick down message timers.
        float dt = ImGui::GetIO().DeltaTime;
        for (int i = hud_msg_count - 1; i >= 0; i--) {
            hud_messages[i].time_remaining -= dt;
            if (hud_messages[i].time_remaining <= 0.0f) {
                for (int j = i; j < hud_msg_count - 1; j++)
                    hud_messages[j] = hud_messages[j + 1];
                hud_msg_count--;
            }
        }
        if (hud_msg_count > 0) {
            float y = work_pos.y + work_size.y - 10.0f;
            for (int i = 0; i < hud_msg_count; i++) {
                float alpha = std::min(hud_messages[i].time_remaining, 1.0f);
                ImU8 a8 = (ImU8)(alpha * 255);
                ImVec2 size = ImGui::CalcTextSize(hud_messages[i].text);
                float mx = work_pos.x + 10.0f;
                float my = y - size.y;
                ImU32 shadow = IM_COL32(0, 0, 0, a8);
                ImU32 fg     = IM_COL32(255, 255, 255, a8);
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx || dy) dl->AddText(ImVec2(mx + dx, my + dy), shadow, hud_messages[i].text);
                    }
                }
                dl->AddText(ImVec2(mx, my), fg, hud_messages[i].text);
                y = my - 2.0f;
            }
        }
    }
}

/*  Draw: Main menu (tabbed with ImGui native TabBar)                  */
/* ------------------------------------------------------------------ */
static bool draw_menu(void)
{
    const bool up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,     true);
    const bool down  = ImGui::IsKeyPressed(ImGuiKey_DownArrow,   true);
    const bool home  = ImGui::IsKeyPressed(ImGuiKey_Home,        true);
    const bool end   = ImGui::IsKeyPressed(ImGuiKey_End,         true);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter,       false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    menu_normalize_selection();

    const TabDef &tab = tabs[current_tab];

    bool close_osd = false;
    if (up) {
        const int prev = item_find_selectable(tab.items, tab.count, menu_sel - 1, -1);
        menu_sel = (prev >= 0) ? prev : item_last(tab.items, tab.count);
    }
    if (down) {
        const int next = item_find_selectable(tab.items, tab.count, menu_sel + 1, 1);
        menu_sel = (next >= 0) ? next : item_first(tab.items, tab.count);
    }
    if (home) menu_sel = item_first(tab.items, tab.count);
    if (end)  menu_sel = item_last(tab.items, tab.count);
    if (enter && menu_sel >= 0) activate_menu_item(tab.items[menu_sel], &close_osd);

    /* ---- Window layout ---- */
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 work_pos  = vp->WorkPos;
    const ImVec2 work_size = vp->WorkSize;

    float menu_w = osd_core_scaled(320.0f);
    if (menu_w > work_size.x * 0.9f) menu_w = work_size.x * 0.9f;
    float max_h = work_size.y * 0.85f;

    ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.5f,
                                   work_pos.y + work_size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(menu_w, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(menu_w, 0.0f),
                                        ImVec2(menu_w, max_h));

    ImGui::Begin("Mesen OSD", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove   | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoNav);

    if (osd_title[0]) {
        ImGui::TextDisabled("%s", osd_title);
        ImGui::Separator();
    }

    /* ---- ImGui native TabBar (overlapping tab style) ---- */
    if (ImGui::BeginTabBar("OSDTabs")) {
        for (int t = 0; t < TAB_COUNT; t++) {
            if (ImGui::BeginTabItem(osd_label(tabs[t].name))) {
                if ((int)current_tab != t) {
                    current_tab = (OsdTab)t;
                    menu_sel = -1;
                    menu_normalize_selection();
                }
                draw_item_list(tabs[t].items, tabs[t].count, &close_osd);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Selectable(osd_label("Close OSD"), false)) {
        close_osd = true;
    }

    ImGui::End();
    return !close_osd;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void osd_core_set_host(const osd_host_t *host)
{
    if (host) osd_host = *host;
    else      osd_host = osd_host_t{ nullptr, nullptr, nullptr };
}

void osd_core_set_title(const char *title)
{
    if (!title) { osd_title[0] = '\0'; return; }
    strncpy(osd_title, title, sizeof(osd_title) - 1);
    osd_title[sizeof(osd_title) - 1] = '\0';
}

void osd_core_reset_to_menu(void) { show_main_menu(); }

bool osd_core_escape(void)
{
    if (current_view == VIEW_MENU) return true;
    show_main_menu();
    return false;
}

bool osd_core_build_ui(void)
{
    switch (current_view) {
        case VIEW_MENU: return draw_menu();
        default:        return draw_menu();
    }
}

/* ================================================================== */
/*  Pixel-buffer rendering for AVI recording                          */
/* ================================================================== */

/* Bitmap font data — identical to DrawStringCommand's _font table. */
static constexpr uint8_t osd_font[792] = {
    6,  0,  0,  0,  0,  0,  0,  0,
    2,128,128,128,128,128,  0,128,
    5, 80, 80, 80,  0,  0,  0,  0,
    6, 80, 80,248, 80,248, 80, 80,
    6, 32,120,160,112, 40,240, 32,
    6, 64,168, 80, 32, 80,168, 16,
    6, 96,144,160, 64,168,144,104,
    3, 64, 64,  0,  0,  0,  0,  0,
    4, 32, 64, 64, 64, 64, 64, 32,
    4,128, 64, 64, 64, 64, 64,128,
    6,  0, 80, 32,248, 32, 80,  0,
    6,  0, 32, 32,248, 32, 32,  0,
    3,  0,  0,  0,  0,  0, 64,128,
    5,  0,  0,  0,240,  0,  0,  0,
    3,  0,  0,  0,  0,  0,  0, 64,
    5, 16, 16, 32, 32, 32, 64, 64,
    6,112,136,136,136,136,136,112,
    6, 32, 96, 32, 32, 32, 32, 32,
    6,112,136,  8, 48, 64,128,248,
    6,112,136,  8, 48,  8,136,112,
    6, 16, 48, 80,144,248, 16, 16,
    6,248,128,128,240,  8,  8,240,
    6, 48, 64,128,240,136,136,112,
    6,248,  8, 16, 16, 32, 32, 32,
    6,112,136,136,112,136,136,112,
    6,112,136,136,120,  8, 16, 96,
    3,  0,  0, 64,  0,  0, 64,  0,
    3,  0,  0, 64,  0,  0, 64,128,
    4,  0, 32, 64,128, 64, 32,  0,
    5,  0,  0,240,  0,240,  0,  0,
    4,  0,128, 64, 32, 64,128,  0,
    6,112,136,  8, 16, 32,  0, 32,
    6,112,136,136,184,176,128,112,
    6,112,136,136,248,136,136,136,
    6,240,136,136,240,136,136,240,
    6,112,136,128,128,128,136,112,
    6,224,144,136,136,136,144,224,
    6,248,128,128,240,128,128,248,
    6,248,128,128,240,128,128,128,
    6,112,136,128,184,136,136,120,
    6,136,136,136,248,136,136,136,
    4,224, 64, 64, 64, 64, 64,224,
    6,  8,  8,  8,  8,  8,136,112,
    6,136,144,160,192,160,144,136,
    6,128,128,128,128,128,128,248,
    6,136,216,168,168,136,136,136,
    6,136,136,200,168,152,136,136,
    7, 48, 72,132,132,132, 72, 48,
    6,240,136,136,240,128,128,128,
    6,112,136,136,136,168,144,104,
    6,240,136,136,240,144,136,136,
    6,112,136,128,112,  8,136,112,
    6,248, 32, 32, 32, 32, 32, 32,
    6,136,136,136,136,136,136,112,
    6,136,136,136, 80, 80, 32, 32,
    6,136,136,136,136,168,168, 80,
    6,136,136, 80, 32, 80,136,136,
    6,136,136, 80, 32, 32, 32, 32,
    6,248,  8, 16, 32, 64,128,248,
    3,192,128,128,128,128,128,192,
    5, 64, 64, 32, 32, 32, 16, 16,
    3,192, 64, 64, 64, 64, 64,192,
    4, 64,160,  0,  0,  0,  0,  0,
    6,  0,  0,  0,  0,  0,  0,248,
    3,128, 64,  0,  0,  0,  0,  0,
    5,  0,  0, 96, 16,112,144,112,
    5,128,128,224,144,144,144,224,
    5,  0,  0,112,128,128,128,112,
    5, 16, 16,112,144,144,144,112,
    5,  0,  0, 96,144,240,128,112,
    5, 48, 64,224, 64, 64, 64, 64,
    5,  0,112,144,144,112, 16,224,
    5,128,128,224,144,144,144,144,
    2,128,  0,128,128,128,128,128,
    4, 32,  0, 32, 32, 32, 32,192,
    5,128,128,144,160,192,160,144,
    2,128,128,128,128,128,128,128,
    6,  0,  0,208,168,168,168,168,
    5,  0,  0,224,144,144,144,144,
    5,  0,  0, 96,144,144,144, 96,
    5,  0,224,144,144,224,128,128,
    5,  0,112,144,144,112, 16, 16,
    5,  0,  0,176,192,128,128,128,
    5,  0,  0,112,128, 96, 16,224,
    4, 64, 64,224, 64, 64, 64, 32,
    5,  0,  0,144,144,144,144,112,
    5,  0,  0,144,144,144,160,192,
    6,  0,  0,136,136,168,168, 80,
    5,  0,  0,144,144, 96,144,144,
    5,  0,144,144,144,112, 16, 96,
    5,  0,  0,240, 32, 64,128,240,
    4, 32, 64, 64,128, 64, 64, 32,
    3, 64, 64, 64, 64, 64, 64, 64,
    4,128, 64, 64, 32, 64, 64,128,
    6,  0,104,176,  0,  0,  0,  0,
    5,  0,  0,113, 80,113,  0,  0,
};

/* Helper: get character index and width from the bitmap font. */
static int osd_font_char_width(char c)
{
    if (c < 32) return 0;
    int idx = c - 32;
    if (idx > 94) idx = 95;
    return osd_font[idx * 8];
}

/* Minimal pixel-buffer drawing context. */
struct OsdPixBuf {
    uint32_t *pixels;
    uint32_t  w, h;
    double    sx, sy;          /* scale from HUD coords to buffer coords */

    void set_pixel(int x, int y, uint32_t color)
    {
        int bx = (int)(x * sx);
        int by = (int)(y * sy);
        if (bx >= 0 && bx < (int)w && by >= 0 && by < (int)h)
            pixels[by * w + bx] = color;
    }

    void draw_rect_filled(int x, int y, int rw, int rh, uint32_t color)
    {
        int x0 = (int)(x * sx), y0 = (int)(y * sy);
        int x1 = (int)((x + rw) * sx), y1 = (int)((y + rh) * sy);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > (int)w) x1 = (int)w; if (y1 > (int)h) y1 = (int)h;
        for (int row = y0; row < y1; row++)
            for (int col = x0; col < x1; col++)
                pixels[row * w + col] = color;
    }

    void draw_rect_outline(int x, int y, int rw, int rh, uint32_t color)
    {
        int x0 = (int)(x * sx), y0 = (int)(y * sy);
        int x1 = (int)((x + rw) * sx), y1 = (int)((y + rh) * sy);
        for (int col = x0; col < x1 && y0 < (int)h; col++)
            if (col >= 0) pixels[y0 * w + col] = color;
        for (int col = x0; col < x1 && y1 - 1 < (int)h; col++)
            if (col >= 0) pixels[(y1-1) * w + col] = color;
        for (int row = y0; row < y1 && x0 < (int)w; row++)
            if (row >= 0) pixels[row * w + x0] = color;
        for (int row = y0; row < y1 && x1 - 1 < (int)w; row++)
            if (row >= 0) pixels[row * w + (x1-1)] = color;
    }

    void draw_circle_filled(int cx, int cy, int r, uint32_t color)
    {
        int x0 = (int)((cx - r) * sx), y0 = (int)((cy - r) * sy);
        int x1 = (int)((cx + r) * sx), y1 = (int)((cy + r) * sy);
        double dcx = cx * sx, dcy = cy * sy;
        double drx = r * sx, dry = r * sy;
        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;
                double dx = (px - dcx) / drx, dy = (py - dcy) / dry;
                if (dx * dx + dy * dy <= 1.0)
                    pixels[py * w + px] = color;
            }
        }
    }

    void draw_line(int x1, int y1, int x2, int y2, uint32_t color)
    {
        int px1 = (int)(x1 * sx), py1 = (int)(y1 * sy);
        int px2 = (int)(x2 * sx), py2 = (int)(y2 * sy);
        int dx = std::abs(px2 - px1), dy = std::abs(py2 - py1);
        int sx2 = px1 < px2 ? 1 : -1, sy2 = py1 < py2 ? 1 : -1;
        int err = dx - dy;
        while (true) {
            if (px1 >= 0 && px1 < (int)w && py1 >= 0 && py1 < (int)h)
                pixels[py1 * w + px1] = color;
            if (px1 == px2 && py1 == py2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; px1 += sx2; }
            if (e2 < dx)  { err += dx; py1 += sy2; }
        }
    }

    /* Draw a text string using the built-in bitmap font.
       Returns the x position after the last character. */
    int draw_string(int x, int y, const char *text, uint32_t fg, uint32_t bg = 0)
    {
        int cx = x;
        for (const char *p = text; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 32) continue;
            int idx = c - 32;
            if (idx > 94) idx = 95;
            int cw = osd_font[idx * 8];
            for (int row = 0; row < 8; row++) {
                uint8_t rowData = osd_font[idx * 8 + 1 + row];
                for (int col = 0; col < cw; col++) {
                    int drawFg = (rowData >> (7 - col)) & 0x01;
                    set_pixel(cx + col, y + row, drawFg ? fg : bg);
                }
            }
            cx += cw;
        }
        return cx;
    }

    /* Draw outlined text (black outline, then foreground) — matches SystemHud style. */
    void draw_string_outlined(int x, int y, const char *text, uint32_t fg)
    {
        uint32_t bg = 0xFF000000;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) draw_string(x + dx, y + dy, text, bg);
        draw_string(x, y, text, fg);
    }

    /* Measure the pixel width of a string in HUD coordinates. */
    static int measure_string(const char *text)
    {
        int w = 0;
        for (const char *p = text; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 32) continue;
            int idx = c - 32;
            if (idx > 94) idx = 95;
            w += osd_font[idx * 8];
        }
        return w;
    }
};

/* Draw a controller using a data-driven layout (pixel-buffer path). */
static void draw_controller_pb(OsdPixBuf &pb, int ox, int oy,
                                const osd_controller_t *ctrl)
{
    const OsdCtrlLayout *L = get_ctrl_layout(ctrl->layout);
    if (!L) return;

    const uint32_t onC  = 0xFFFFFFFF;
    const uint32_t offC = 0xFF3C3C3C;

    /* Body */
    pb.draw_rect_filled(ox, oy, (int)L->body_w, (int)L->body_h, 0xA0000000);
    pb.draw_rect_outline(ox, oy, (int)L->body_w, (int)L->body_h, 0xFF646464);

    /* Elements */
    for (int i = 0; i < L->elem_count; i++) {
        const OsdCtrlElem &e = L->elems[i];

        if (e.type == OSD_ELEM_LCD) {
            /* Grey semi-transparent LCD decoration area */
            int ex = ox + (int)e.x, ey = oy + (int)e.y;
            int ew = (int)e.w, eh = (int)e.h;
            pb.draw_rect_filled(ex, ey, ew, eh, 0x64505050);
            pb.draw_rect_outline(ex, ey, ew, eh, 0x8C787878);
            continue;
        }

        bool pressed = (e.button_idx >= 0) ? ctrl->buttons[e.button_idx] : false;
        uint32_t col = pressed ? onC : offC;

        if (e.type == OSD_ELEM_RECT) {
            int ex = ox + (int)e.x, ey = oy + (int)e.y;
            int ew = (int)e.w, eh = (int)e.h;
            pb.draw_rect_filled(ex, ey, ew, eh, col);
        } else if (e.type == OSD_ELEM_CIRCLE) {
            pb.draw_circle_filled(ox + (int)e.x, oy + (int)e.y, (int)e.w, col);
        }
    }

    /* Player label */
    char num[8];
    snprintf(num, sizeof(num), "P%d", ctrl->port + 1);
    pb.draw_string(ox + (int)L->label_x, oy + (int)L->label_y, num, 0xFFB4B4B4);
}

/* ------------------------------------------------------------------ */
/*  osd_core_update — tick message expiry                              */
/* ------------------------------------------------------------------ */
void osd_core_update(void)
{
    /* Expire old messages */
    for (int i = 0; i < hud_msg_count; ) {
        hud_messages[i].time_remaining -= 1.0f / 60.0f;
        if (hud_messages[i].time_remaining <= 0.0f) {
            for (int j = i; j < hud_msg_count - 1; j++)
                hud_messages[j] = hud_messages[j + 1];
            hud_msg_count--;
        } else {
            i++;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  osd_core_draw_to_buffer — pixel-level OSD for AVI recording       */
/* ------------------------------------------------------------------ */
void osd_core_draw_to_buffer(uint32_t *buffer, uint32_t width, uint32_t height,
                             uint32_t hud_width, uint32_t hud_height)
{
    if (!buffer || !width || !height || !hud_width || !hud_height) return;

    OsdPixBuf pb;
    pb.pixels = buffer;
    pb.w = width;
    pb.h = height;
    pb.sx = (double)width / hud_width;
    pb.sy = (double)height / hud_height;

    uint32_t white = 0xFFFFFFFF;
    uint32_t yellow = 0xFFFFFF00;
    uint32_t black = 0xFF000000;

    /* ---- Right-aligned counters (FPS / Timer / Frame / Lag) ---- */
    if (osd_emu.is_running) {
        int lineNum = 0;
        int yPos = 10 + 10 * lineNum;

        if (osd_emu.show_fps) {
            char buf[64];
            snprintf(buf, sizeof(buf), "FPS: %u", osd_emu.fps);
            int len = OsdPixBuf::measure_string(buf);
            pb.draw_string_outlined(hud_width - 8 - len, yPos, buf, white);
            lineNum++; yPos = 10 + 10 * lineNum;
        }
        if (osd_emu.show_game_timer) {
            char buf[64];
            uint32_t fc = osd_emu.frame_count;
            double fr = osd_emu.fps_rate;
            uint32_t sec = (uint32_t)(fc / fr) % 60;
            uint32_t min = (uint32_t)(fc / fr / 60) % 60;
            uint32_t hr  = (uint32_t)(fc / fr / 3600);
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hr, min, sec);
            int len = OsdPixBuf::measure_string(buf);
            pb.draw_string_outlined(hud_width - 8 - len, yPos, buf, white);
            lineNum++; yPos = 10 + 10 * lineNum;
        }
        if (osd_emu.show_frame_counter) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Frame: %u", osd_emu.frame_count);
            int len = OsdPixBuf::measure_string(buf);
            pb.draw_string_outlined(hud_width - 8 - len, yPos, buf, white);
            lineNum++; yPos = 10 + 10 * lineNum;
        }
        if (osd_emu.show_lag_counter) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Lag: %u", osd_emu.lag_count);
            int len = OsdPixBuf::measure_string(buf);
            pb.draw_string_outlined(hud_width - 8 - len, yPos, buf, white);
            lineNum++; yPos = 10 + 10 * lineNum;
        }
    }

    /* ---- Status icons (top-left) ---- */
    if (osd_emu.is_running) {
        int xOff = 0;
        if (osd_emu.is_paused) {
            /* Pause icon: two vertical bars */
            pb.draw_rect_filled(10, 7, 5, 12, white);
            pb.draw_rect_filled(17, 7, 5, 12, white);
        } else if (osd_emu.show_movie_icons && osd_emu.is_movie_playing) {
            /* Play icon: triangle */
            for (int i = 0; i < 5; i++) {
                int left = 12 + i * 2;
                int top = 12 + i;
                pb.draw_line(left, top - 1, left, 20 - i + 1, black);
                pb.draw_line(left + 1, top - 1, left + 1, 20 - i + 1, black);
                if (i > 0) pb.draw_line(left, top, left, 20 - i, white);
                if (i < 4) pb.draw_line(left + 1, top, left + 1, 20 - i, white);
            }
            xOff += 12;
        } else if (osd_emu.show_movie_icons && osd_emu.is_movie_recording) {
            /* Record icon: red circle */
            pb.draw_circle_filled(12, 11, 5, 0xFFFF0000);
            xOff += 12;
        }

        if (!osd_emu.is_paused && osd_emu.show_turbo_rewind_icons) {
            if (osd_emu.is_rewind) {
                /* Rewind icon: two left-pointing triangles (orange) */
                for (int j = 0; j < 2; j++) {
                    int bx = 12 + xOff + j * 6 + 5;
                    for (int i = 0; i < 3; i++) {
                        int left = bx - i * 2;
                        int top = 12 + i * 2;
                        pb.draw_line(left, top - 2, left, 20 - i * 2 + 2, 0xFF333333);
                        if (i > 0) pb.draw_line(left, top - 1, left, 20 - i * 2 + 1, 0xFFF0A060);
                    }
                }
            } else if (osd_emu.is_turbo) {
                /* Turbo icon: two right-pointing triangles (green) */
                for (int j = 0; j < 2; j++) {
                    int bx = 12 + xOff + j * 6;
                    for (int i = 0; i < 3; i++) {
                        int left = bx + i * 2;
                        int top = 12 + i * 2;
                        pb.draw_line(left, top - 2, left, 20 - i * 2 + 2, 0xFF333333);
                        if (i > 0) pb.draw_line(left, top - 1, left, 20 - i * 2 + 1, 0xFF80F080);
                    }
                }
            }
        }
    }

    /* ---- Bottom messages ---- */
    {
        int lastHeight = 3;
        int count = hud_msg_count;
        if (count > 4) count = 4;
        for (int i = 0; i < count; i++) {
            float alpha = std::min(hud_messages[i].time_remaining, 1.0f);
            uint8_t a = (uint8_t)(alpha * 255);
            /* Invert alpha for the pixel format (0 = opaque in DrawString convention) */
            uint8_t ia = 255 - a;
            uint32_t fg = 0xFFFFFF | (ia << 24);
            uint32_t bg = 0xFF000000;
            int textLeftMargin = 4;
            int len = OsdPixBuf::measure_string(hud_messages[i].text);
            int maxW = hud_width - textLeftMargin;
            /* Simple: just draw the text, no word wrapping for the pixel buffer */
            int textW = len;
            lastHeight += 9;
            /* Draw outline then text */
            int tx = textLeftMargin;
            int ty = hud_height - lastHeight;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dy) pb.draw_string(tx + dx, ty + dy, hud_messages[i].text, bg);
            pb.draw_string(tx, ty, hud_messages[i].text, fg);
        }
    }

    /* ---- Debug statistics panel ---- */
    if (osd_emu.is_running && osd_emu.show_debug_info) {
        int ox = 10, oy = 30;
        int aw = 130, vw = 130, gap = 8;
        int ah = 68, vh = 90;
        char buf[128];

        /* Audio panel */
        pb.draw_rect_filled(ox, oy, aw, ah, 0xA0000000);
        pb.draw_rect_outline(ox, oy, aw, ah, 0xFF646464);

        int tx = ox + 4, ty = oy + 4;
        pb.draw_string(tx, ty, "Audio Stats", yellow); ty += 9;
        snprintf(buf, sizeof(buf), "Latency: %.2f ms", osd_debug.audio_latency);
        uint32_t latColor = (osd_debug.audio_latency > 0 && std::abs(osd_debug.audio_latency - osd_debug.audio_target_latency) > 3) ?
            0xFFFF5050 : white;
        pb.draw_string(tx, ty, buf, latColor); ty += 9;
        snprintf(buf, sizeof(buf), "Underruns: %u", osd_debug.audio_underruns);
        pb.draw_string(tx, ty, buf, white); ty += 9;
        snprintf(buf, sizeof(buf), "Buffer: %u kb", osd_debug.audio_buffer_size / 1024);
        pb.draw_string(tx, ty, buf, white); ty += 9;
        snprintf(buf, sizeof(buf), "Rate: %u Hz", osd_debug.audio_sample_rate);
        pb.draw_string(tx, ty, buf, white);

        /* Video panel */
        int vx0 = ox + aw + gap;
        pb.draw_rect_filled(vx0, oy, vw, vh, 0xA0000000);
        pb.draw_rect_outline(vx0, oy, vw, vh, 0xFF646464);

        int vx = vx0 + 4, vy = oy + 4;
        pb.draw_string(vx, vy, "Video Stats", yellow); vy += 9;
        snprintf(buf, sizeof(buf), "FPS: %.1f", osd_debug.video_fps);
        pb.draw_string(vx, vy, buf, white); vy += 9;
        snprintf(buf, sizeof(buf), "Last: %.2f ms", osd_debug.video_last_frame_ms);
        pb.draw_string(vx, vy, buf, white); vy += 9;
        double minMs = (osd_debug.video_min_frame_ms < 9999) ? osd_debug.video_min_frame_ms : 0.0;
        snprintf(buf, sizeof(buf), "Min: %.2f  Max: %.2f ms", minMs, osd_debug.video_max_frame_ms);
        pb.draw_string(vx, vy, buf, white); vy += 9;

        /* Frame time graph */
        int gx = vx0 + 4, gy = vy + 2;
        int gw = vw - 8, gh = 30;
        pb.draw_rect_filled(gx, gy, gw, gh, 0xC8000000);
        pb.draw_rect_outline(gx, gy, gw, gh, 0xFF505050);

        double expectedMs = 1000.0 / std::max(1.0, osd_emu.fps_rate);
        for (int i = 0; i < 59; i++) {
            double d = osd_debug.frame_durations[i];
            double nd = osd_debug.frame_durations[i + 1];
            d = std::min(25.0, std::max(10.0, d));
            nd = std::min(25.0, std::max(10.0, nd));
            uint32_t lc = 0xFF00FF00;
            if (std::abs(d - expectedMs) > 2) lc = 0xFFFF0000;
            else if (std::abs(d - expectedMs) > 1) lc = 0xFF00A5FF;
            int x1 = gx + i * gw / 59;
            int x2 = gx + (i + 1) * gw / 59;
            int y1 = gy + gh - (int)((d - 10.0) * gh / 15.0);
            int y2 = gy + gh - (int)((nd - 10.0) * gh / 15.0);
            pb.draw_line(x1, y1, x2, y2, lc);
        }

        /* Misc panel */
        int fullW = aw + gap + vw;
        int my = oy + std::max(ah, vh) + 4;
        int mh = 42;
        pb.draw_rect_filled(ox, my, fullW, mh, 0xA0000000);
        pb.draw_rect_outline(ox, my, fullW, mh, 0xFF646464);
        int mx = ox + 4, mty = my + 4;
        pb.draw_string(mx, mty, "Misc. Stats", yellow); mty += 9;
        snprintf(buf, sizeof(buf), "Rewind mem.: %.2f MB", osd_debug.rewind_memory_mb);
        pb.draw_string(mx, mty, buf, white); mty += 9;
        snprintf(buf, sizeof(buf), "Rewind rate: %.2f MB/min", osd_debug.rewind_per_minute_mb);
        pb.draw_string(mx, mty, buf, white);
    }

    /* ---- Controller display ---- */
    if (osd_emu.is_running && osd_controller_count > 0) {
        int visible = 0;
        for (int i = 0; i < osd_controller_count; i++) {
            if (i < OSD_MAX_CONTROLLERS && osd_controllers[i].port < 8 && osd_input_prefs.display_port[osd_controllers[i].port])
                visible++;
        }
        if (visible > 0) {
            int pos = osd_input_prefs.display_position;
            /* Determine max controller dimensions */
            int ctrlW = 80, ctrlH = 32;
            for (int i = 0; i < osd_controller_count; i++) {
                if (i >= OSD_MAX_CONTROLLERS || osd_controllers[i].port >= 8 || !osd_input_prefs.display_port[osd_controllers[i].port])
                    continue;
                const OsdCtrlLayout *L = get_ctrl_layout(osd_controllers[i].layout);
                if (L) {
                    if ((int)L->body_w > ctrlW) ctrlW = (int)L->body_w;
                    if ((int)L->body_h > ctrlH) ctrlH = (int)L->body_h;
                }
            }
            int ctrlX, ctrlY;
            int gap2 = 4;

            if (pos == 0 || pos == 1) {
                ctrlY = 30;
                ctrlX = (pos == 0) ? 10 : hud_width - 10 - ctrlW;
            } else {
                int totalH = osd_input_prefs.display_horizontally ? ctrlH : visible * ctrlH + (visible - 1) * gap2;
                ctrlY = hud_height - 10 - totalH;
                ctrlX = (pos == 2) ? 10 : hud_width - 10 - ctrlW;
            }

            for (int i = 0; i < osd_controller_count; i++) {
                if (i >= OSD_MAX_CONTROLLERS || osd_controllers[i].port >= 8 || !osd_input_prefs.display_port[osd_controllers[i].port])
                    continue;
                const osd_controller_t *c = &osd_controllers[i];

                draw_controller_pb(pb, ctrlX, ctrlY, c);

                if (osd_input_prefs.display_horizontally)
                    ctrlX += ctrlW + gap2;
                else
                    ctrlY += ctrlH + gap2;
            }
        }
    }

    /* ---- Audio player display ---- */
    if (osd_audio_player_active) {
        const osd_audio_player_t &ap = osd_audio_player;

        /* Full-screen overlay at 256x240 */
        pb.draw_rect_filled(0, 0, 256, 240, 0xFF000000);

        int y = 12;
        auto drawLabel = [&](const char *label, const char *value) {
            if (value[0] != '\0') {
                pb.draw_string(10, y, label, 0xFFBBBBBB);
                pb.draw_string(57, y, value, 0xFFFFFFFF);
                y += 10;
            }
        };
        drawLabel("Game:", ap.game_title);
        drawLabel("Artist:", ap.artist);
        drawLabel("Comment:", ap.comment);

        /* Track info */
        char trackStr[64];
        snprintf(trackStr, sizeof(trackStr), "Track: %d / %d", ap.track_number, ap.track_count);
        int tLen = OsdPixBuf::measure_string(trackStr);
        pb.draw_string(256 - tLen - 14, 218, trackStr, 0xFFFFFFFF);

        /* Track name */
        const char *trackName = ap.song_title[0] != '\0' ? ap.song_title : ap.rom_filename;
        pb.draw_string(15, 208, trackName, 0xFFFFFFFF);

        /* Position */
        auto fmtSec = [](double sec) -> string {
            uint32_t s = (uint32_t)sec;
            string seconds = std::to_string(s % 60);
            if (seconds.size() == 1) seconds = "0" + seconds;
            return std::to_string(s / 60) + ":" + seconds;
        };

        if (ap.length <= 0) {
            char posStr[64];
            snprintf(posStr, sizeof(posStr), " %s   ", fmtSec(ap.position).c_str());
            pb.draw_string(215, 208, posStr, 0xFFFFFFFF);
        } else {
            char posStr[64];
            snprintf(posStr, sizeof(posStr), " %s / %s   ", fmtSec(ap.position).c_str(), fmtSec(ap.length).c_str());
            pb.draw_string(177, 208, posStr, 0xFFFFFFFF);
            constexpr int barWidth = 222;
            pb.draw_rect_outline(15, 199, barWidth + 4, 6, 0xFFBBBBBB);
            double ratio = std::min(1.0, ap.position / ap.length);
            pb.draw_rect_filled(17, 201, (int)(ratio * barWidth), 2, 0xFF77BBFF);
        }

        /* Spectrum analyzer */
        static constexpr double ranges[8][3] = {
            { 20, 150, 0.5 }, { 150, 400, 0.5 }, { 400, 700, 0.75 }, { 700, 1000, 0.75 },
            { 1000, 2000, 1 }, { 2000, 4000, 1 }, { 4000, 6000, 1.25 }, { 6000, 20000, 1.25 }
        };
        static constexpr int maxVal = 140;
        int top = 191 - maxVal;
        int bottom = 190;
        uint32_t fgColor = 0xFF555555;
        uint32_t fgColor2 = 0xFF666666;
        uint32_t bgColor = 0xFF222222;

        pb.draw_line(0, top - 1, 255, top - 1, fgColor);
        pb.draw_line(0, bottom + 1, 255, bottom + 1, fgColor);
        pb.draw_rect_filled(0, top, 256, 140, bgColor);
        pb.draw_line(192, top, 192, bottom, fgColor);
        pb.draw_line(128, top, 128, bottom, fgColor);
        pb.draw_line(64, top, 64, bottom, fgColor);
        pb.draw_line(224, top, 224, bottom, fgColor2);
        pb.draw_line(160, top, 160, bottom, fgColor2);
        pb.draw_line(96, top, 96, bottom, fgColor2);
        pb.draw_line(32, top, 32, bottom, fgColor2);

        pb.draw_string(3, top + 10, "20Hz", fgColor);
        pb.draw_string(19, top + 30, "150Hz", fgColor2);
        pb.draw_string(51, top + 10, "400Hz", fgColor);
        pb.draw_string(83, top + 30, "700Hz", fgColor2);
        pb.draw_string(117, top + 10, "1kHz", fgColor);
        pb.draw_string(150, top + 30, "2kHz", fgColor2);
        pb.draw_string(182, top + 10, "4kHz", fgColor);
        pb.draw_string(214, top + 30, "6kHz", fgColor2);
        pb.draw_string(228, top + 10, "20kHz", fgColor);

        if (ap.amplitudes && ap.amplitudes_count > 0) {
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 32; j++) {
                    double freqRange = ranges[i][1] - ranges[i][0];
                    double startFreq = ranges[i][0] + freqRange * j / 32;
                    double endFreq   = ranges[i][0] + freqRange * (j + 1) / 32;

                    int startIndex = (int)(startFreq / ((double)ap.sample_rate / (double)ap.amplitudes_count * 2));
                    int endIndex   = (int)(endFreq / ((double)ap.sample_rate / (double)ap.amplitudes_count * 2));

                    double avgAmp = 0;
                    for (int ai = startIndex; ai <= endIndex && ai < ap.amplitudes_count; ai++) {
                        avgAmp += ap.amplitudes[ai];
                    }
                    avgAmp /= (endIndex - startIndex + 1);
                    avgAmp *= ranges[i][2];
                    avgAmp = std::min<double>(maxVal, avgAmp);

                    if (avgAmp >= 1) {
                        int red   = std::min(255, (int)(256 * (avgAmp / maxVal) * 2));
                        int green = std::max(0, std::min(255, (int)(256 * ((maxVal - avgAmp) / maxVal) * 2)));
                        uint32_t barCol = (red << 16) | (green << 8);
                        pb.draw_rect_filled(i * 32 + j, 190, 1, (int)-avgAmp, barCol);
                    }
                }
            }
        }
    }
}
