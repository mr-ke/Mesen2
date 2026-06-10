#pragma once
#include "pch.h"
#include <stdint.h>
#include <stddef.h>

// libretro API types - based on libretro.h

// Calling convention
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	#ifdef RETRO_IMPORT_SYMBOLS
		#ifdef __GNUC__
			#define RETRO_CALLCONV __attribute__((cdecl))
			#define RETRO_API __attribute__((dllimport))
		#else
			#define RETRO_CALLCONV __cdecl
			#define RETRO_API __declspec(dllimport)
		#endif
	#else
		#ifdef __GNUC__
			#define RETRO_CALLCONV __attribute__((cdecl))
			#define RETRO_API __attribute__((dllexport))
		#else
			#define RETRO_CALLCONV __cdecl
			#define RETRO_API __declspec(dllexport)
		#endif
	#endif
#else
	#if defined(__GNUC__) && __GNUC__ >= 4
		#define RETRO_CALLCONV
		#define RETRO_API __attribute__((visibility("default")))
	#else
		#define RETRO_CALLCONV
		#define RETRO_API
	#endif
#endif

// API Version
#define RETRO_API_VERSION 1

// Input device types
#define RETRO_DEVICE_NONE         0
#define RETRO_DEVICE_JOYPAD       1
#define RETRO_DEVICE_MOUSE        2
#define RETRO_DEVICE_KEYBOARD     3
#define RETRO_DEVICE_LIGHTGUN     4
#define RETRO_DEVICE_ANALOG       5
#define RETRO_DEVICE_POINTER      6

// Joypad buttons
#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L       10
#define RETRO_DEVICE_ID_JOYPAD_R       11
#define RETRO_DEVICE_ID_JOYPAD_L2      12
#define RETRO_DEVICE_ID_JOYPAD_R2      13
#define RETRO_DEVICE_ID_JOYPAD_L3      14
#define RETRO_DEVICE_ID_JOYPAD_R3      15

// Analog indices
#define RETRO_DEVICE_INDEX_ANALOG_LEFT   0
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT  1
#define RETRO_DEVICE_ID_ANALOG_X         0
#define RETRO_DEVICE_ID_ANALOG_Y         1

// Regions
#define RETRO_REGION_NTSC  0
#define RETRO_REGION_PAL   1

// Memory types
#define RETRO_MEMORY_MASK        0xff
#define RETRO_MEMORY_SAVE_RAM    0
#define RETRO_MEMORY_RTC         1
#define RETRO_MEMORY_SYSTEM_RAM  2
#define RETRO_MEMORY_VIDEO_RAM   3

// Pixel formats
enum retro_pixel_format
{
	RETRO_PIXEL_FORMAT_0RGB1555 = 0,
	RETRO_PIXEL_FORMAT_XRGB8888 = 1,
	RETRO_PIXEL_FORMAT_RGB565   = 2,
	RETRO_PIXEL_FORMAT_UNKNOWN  = INT_MAX
};

// Environment commands (correct values from libretro.h)
#define RETRO_ENVIRONMENT_SET_ROTATION              1
#define RETRO_ENVIRONMENT_GET_OVERSCAN              2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE              3
#define RETRO_ENVIRONMENT_SET_MESSAGE               6
#define RETRO_ENVIRONMENT_SHUTDOWN                  7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL     8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY      9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT         10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS    11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK    12
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE  13
#define RETRO_ENVIRONMENT_SET_HW_RENDER            14
#define RETRO_ENVIRONMENT_GET_VARIABLE             15
#define RETRO_ENVIRONMENT_SET_VARIABLES            16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE      17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME      18
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH        19
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK  21
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK       22
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE     23
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES  24
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE        27
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE       28
#define RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE   29
#define RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY    30
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY 30
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY       31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO       32
#define RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK 33
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO       34
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO      35
#define RETRO_ENVIRONMENT_SET_GEOMETRY             37
#define RETRO_ENVIRONMENT_GET_USERNAME             38
#define RETRO_ENVIRONMENT_GET_LANGUAGE             39
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS 44

// Core options API
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION  52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS          53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL     54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY  55
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2       67
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL  68
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK 69

// Additional commands
#define RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE 65
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE         61
#define RETRO_ENVIRONMENT_GET_LED_INTERFACE         62
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE    (47 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_MIDI_INTERFACE        66
#define RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE  58
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING        70
#define RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE   71
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS        72
#define RETRO_ENVIRONMENT_GET_THROTTLE_STATE        74
#define RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE   78

// Experimental environment commands (0x10000 = 65536)
#define RETRO_ENVIRONMENT_EXPERIMENTAL              0x10000
#define RETRO_ENVIRONMENT_PRIVATE                   0x20000
#define RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE      (25 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE      (26 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS  (42 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE (43 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER (40 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE   (41 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS           (36 | RETRO_ENVIRONMENT_EXPERIMENTAL)

// Structures
struct retro_system_info
{
	const char *library_name;
	const char *library_version;
	const char *valid_extensions;
	bool need_fullpath;
	bool block_extract;
};

struct retro_game_geometry
{
	unsigned base_width;
	unsigned base_height;
	unsigned max_width;
	unsigned max_height;
	float aspect_ratio;
};

struct retro_system_timing
{
	double fps;
	double sample_rate;
};

struct retro_system_av_info
{
	struct retro_game_geometry geometry;
	struct retro_system_timing timing;
};

struct retro_game_info
{
	const char *path;
	const void *data;
	size_t size;
	const char *meta;
};

struct retro_variable
{
	const char *key;
	const char *value;
};

struct retro_message
{
	const char *msg;
	unsigned frames;
};

// Log callback
typedef void (RETRO_CALLCONV *retro_log_printf_t)(int level, const char *fmt, ...);

struct retro_log_callback
{
	retro_log_printf_t log;
};

struct retro_input_descriptor
{
	unsigned port;
	unsigned device;
	unsigned index;
	unsigned id;
	const char *description;
};

// Callback types
typedef bool (RETRO_CALLCONV *retro_environment_t)(unsigned cmd, void *data);
typedef void (RETRO_CALLCONV *retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (RETRO_CALLCONV *retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (RETRO_CALLCONV *retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV *retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

// Core API function types
typedef void (RETRO_CALLCONV *retro_init_t)(void);
typedef void (RETRO_CALLCONV *retro_deinit_t)(void);
typedef unsigned (RETRO_CALLCONV *retro_api_version_t)(void);
typedef void (RETRO_CALLCONV *retro_get_system_info_t)(struct retro_system_info *info);
typedef void (RETRO_CALLCONV *retro_get_system_av_info_t)(struct retro_system_av_info *info);
typedef void (RETRO_CALLCONV *retro_set_environment_t)(retro_environment_t cb);
typedef void (RETRO_CALLCONV *retro_set_video_refresh_t)(retro_video_refresh_t cb);
typedef void (RETRO_CALLCONV *retro_set_audio_sample_t)(retro_audio_sample_t cb);
typedef void (RETRO_CALLCONV *retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t cb);
typedef void (RETRO_CALLCONV *retro_set_input_poll_t)(retro_input_poll_t cb);
typedef void (RETRO_CALLCONV *retro_set_input_state_t)(retro_input_state_t cb);
typedef void (RETRO_CALLCONV *retro_set_controller_port_device_t)(unsigned port, unsigned device);
typedef void (RETRO_CALLCONV *retro_reset_t)(void);
typedef void (RETRO_CALLCONV *retro_run_t)(void);
typedef bool (RETRO_CALLCONV *retro_load_game_t)(const struct retro_game_info *game);
typedef bool (RETRO_CALLCONV *retro_load_game_special_t)(unsigned game_type, const struct retro_game_info *info, size_t num_info);
typedef void (RETRO_CALLCONV *retro_unload_game_t)(void);
typedef size_t (RETRO_CALLCONV *retro_serialize_size_t)(void);
typedef bool (RETRO_CALLCONV *retro_serialize_t)(void *data, size_t size);
typedef bool (RETRO_CALLCONV *retro_unserialize_t)(const void *data, size_t size);
typedef void (RETRO_CALLCONV *retro_cheat_reset_t)(void);
typedef void (RETRO_CALLCONV *retro_cheat_set_t)(unsigned index, bool enabled, const char *code);
typedef unsigned (RETRO_CALLCONV *retro_get_region_t)(void);
typedef void* (RETRO_CALLCONV *retro_get_memory_data_t)(unsigned id);
typedef size_t (RETRO_CALLCONV *retro_get_memory_size_t)(unsigned id);
