/*
 * Mesen2 OSD core - ImGui-based on-screen display overlay.
 *
 * Two rendering layers:
 *   1. osd_hud_draw()  — always-on HUD (FPS, messages, status icons)
 *   2. osd_core_build_ui() — toggleable OSD menu (File/Game tabs)
 *
 * Both are called from SdlRenderer::Render() inside the same ImGui frame.
 * osd_hud_draw() must be called BEFORE osd_core_build_ui().
 */
#include <cstdint>
#ifndef MES2_OSD_CORE_HPP
#define MES2_OSD_CORE_HPP

/*
 * Emulator state snapshot, filled by the host (SdlRenderer) every frame.
 * This decouples osd_core from the Emulator class.
 */
typedef struct osd_emu_state_t {
    uint32_t fps;               // current frames per second
    uint32_t frame_count;       // total frames emulated
    uint32_t lag_count;         // lag frame counter
    double   fps_rate;          // nominal frame rate (e.g. 60.0988)
    bool     is_running;        // emulator is running
    bool     is_paused;         // emulator is paused
    bool     is_turbo;          // turbo mode active
    bool     is_rewind;         // rewind mode active
    bool     is_movie_playing;  // movie playback active
    bool     is_movie_recording;// movie recording active
    bool     show_fps;          // user preference: show FPS
    bool     show_game_timer;   // user preference: show game timer
    bool     show_frame_counter;// user preference: show frame counter
    bool     show_lag_counter;  // user preference: show lag counter
    bool     show_turbo_rewind_icons; // user preference: show turbo/rewind icons
    bool     show_movie_icons;  // user preference: show movie icons
    bool     show_debug_info;   // user preference: show debug stats panel
} osd_emu_state_t;

/*
 * Frontend callbacks supplied by the active OSD host (the SDL renderer).
 * Mesen's SdlRenderer installs these during initialization.
 *
 * execute_shortcut: fires an EmulatorShortcut through the notification
 *                   system so the C# UI handles it exactly as if the user
 *                   pressed the corresponding keyboard shortcut.
 */
typedef struct osd_host_t {
    void (*toggle_fullscreen)(void);
    void (*request_exit)(void);
    void (*execute_shortcut)(int shortcut);
} osd_host_t;

void osd_core_set_host(const osd_host_t *host);

/* Provide the current emulator state snapshot for this frame. */
void osd_core_set_emu_state(const osd_emu_state_t *state);

float osd_core_scaled(float value);

float osd_core_layout_scale_for_output(int output_w, int output_h);

void osd_core_set_layout_scale(float scale);

int  osd_core_default_font_size(void);
void osd_core_rebuild_default_font(int pixel_size);

void osd_core_setup_style(void);

void osd_core_set_title(const char *title);

void osd_core_reset_to_menu(void);

/* Always-on HUD layer (FPS, messages, status icons). */
void osd_hud_draw(void);

/* Toggleable OSD menu layer (File/Game tabs). */
bool osd_core_build_ui(void);

bool osd_core_escape(void);

void osd_core_log_push(const char *line);

/* Show a titled message in the HUD (e.g. "[SaveState] State saved").
 * Formats as "[title] message" and displays for 3 seconds with fade-out. */
void osd_core_show_message(const char *title, const char *message);

/* ------------------------------------------------------------------ */
/*  Debug statistics (replaces DebugStats pixel drawing with ImGui)    */
/* ------------------------------------------------------------------ */

typedef struct osd_debug_stats_t {
    /* Audio stats */
    double   audio_latency;           /* average audio latency in ms */
    double   audio_target_latency;    /* configured audio latency in ms */
    uint32_t audio_underruns;         /* buffer underrun count */
    uint32_t audio_buffer_size;       /* buffer size in bytes */
    uint32_t audio_sample_rate;       /* effective sample rate in Hz */

    /* Video stats */
    double   video_fps;               /* measured FPS */
    double   video_last_frame_ms;     /* last frame duration in ms */
    double   video_min_frame_ms;      /* min frame duration */
    double   video_max_frame_ms;      /* max frame duration */
    double   frame_durations[60];     /* last 60 frame durations for graph */

    /* Misc stats */
    double   rewind_memory_mb;        /* rewind buffer memory usage */
    double   rewind_per_minute_mb;    /* rewind memory per minute */
} osd_debug_stats_t;

/* Set debug statistics for this frame. Only used when show_debug_info is true. */
void osd_core_set_debug_stats(const osd_debug_stats_t *stats);

/* ------------------------------------------------------------------ */
/*  Controller display (InputHud replacement via ImGui)                */
/* ------------------------------------------------------------------ */

#define OSD_MAX_CONTROLLERS  8
#define OSD_MAX_BUTTONS      16

enum OsdControllerLayout {
    OSD_LAYOUT_NONE = 0,
    OSD_LAYOUT_NES,       /* NES / GameBoy: D-pad + Select/Start + B/A */
    OSD_LAYOUT_SNES,      /* SNES:          D-pad + Select/Start + diamond + L/R */
    OSD_LAYOUT_GBA,       /* GBA:           D-pad + Select/Start + B/A + L/R */
    OSD_LAYOUT_PCE,       /* PCE:           D-pad + Select/Run + I/II */
    OSD_LAYOUT_SMS,       /* SMS:           D-pad + Pause + B/A */
    OSD_LAYOUT_WS,        /* WS:            2×D-pad + Sound/Start + B/A */
    OSD_LAYOUT_NDS,       /* NDS:           handheld shape, D-pad + LCD + diamond + L/R */
    OSD_LAYOUT_3DS,       /* 3DS:           handheld shape, D-pad + LCD + diamond + L/R/ZL/ZR */
    OSD_LAYOUT_GENERIC
};

typedef struct osd_controller_t {
    OsdControllerLayout layout;
    uint8_t port;
    bool     buttons[OSD_MAX_BUTTONS];
} osd_controller_t;

typedef struct osd_input_prefs_t {
    int      display_position;   /* 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight */
    bool     display_port[OSD_MAX_CONTROLLERS];
    bool     display_horizontally;
} osd_input_prefs_t;

void osd_core_set_controllers(const osd_controller_t *controllers, int count);
void osd_core_set_input_prefs(const osd_input_prefs_t *prefs);

/* ------------------------------------------------------------------ */
/*  Pixel-buffer rendering (for AVI recording without DebugHud)       */
/* ------------------------------------------------------------------ */

/* Render all OSD elements (counters, messages, status icons, controllers,
   debug stats) directly into an ARGB pixel buffer.  Used by AVI recording
   to composite the HUD onto recorded frames without going through ImGui.
   buffer:  ARGB pixel buffer (width * height uint32_t values)
   width/height:  buffer dimensions in pixels
   hud_width/hud_height:  logical HUD coordinate space (typically the
              unscaled frame size, e.g. 256x240) — the function scales
              all drawing to map hud coords into the buffer. */
void osd_core_draw_to_buffer(uint32_t *buffer, uint32_t width, uint32_t height,
                             uint32_t hud_width, uint32_t hud_height);

/* Tick message expiry timers.  Call once per frame from the render thread. */
void osd_core_update(void);

/* ------------------------------------------------------------------ */
/*  Audio player display (AudioPlayerHud replacement via ImGui)       */
/* ------------------------------------------------------------------ */

#define OSD_AUDIO_FFT_SIZE  8192  /* 2048*4, must match AudioPlayer::N */

typedef struct osd_audio_player_t {
    /* Track info */
    char     game_title[128];
    char     artist[128];
    char     comment[128];
    char     song_title[128];
    char     rom_filename[128];
    uint32_t track_number;
    uint32_t track_count;
    double   position;         /* current position in seconds */
    double   length;           /* total length in seconds, <=0 if unknown */
    double   fade_length;      /* fade-out length in seconds */

    /* FFT spectrum data */
    const double *amplitudes;  /* array of N/2 doubles, or NULL */
    int           amplitudes_count;
    uint32_t      sample_rate;
} osd_audio_player_t;

/* Set audio player state for this frame.  Pass NULL when no audio player
   is active (e.g. normal game emulation). */
void osd_core_set_audio_player(const osd_audio_player_t *player);

#endif /* MES2_OSD_CORE_HPP */
