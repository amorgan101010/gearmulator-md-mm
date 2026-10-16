/*
 * gm_device.h: the Gearmulator device ABI, version 1.
 *
 * A small, product-independent C interface to one emulated unit, so a host that is not this
 * code base (the Rust studio engine, a test harness) can run it in-process. The first
 * implementation is the Machinedrum/Monomachine core (libgm_md); other Gearmulator devices can
 * implement the same functions later.
 *
 * Rules every implementation keeps:
 *
 * - Plain C. No exception, C++ unwinding or panic crosses these functions: failures come back as
 *   a gm_status, with a message from gm_device_last_error.
 * - A device is single-threaded. One thread at a time may call into a given device; the host
 *   provides the happens-before when it moves a device between threads. Different devices are
 *   independent and may run on different threads at the same time.
 * - The core owns no host resources: no files, threads, clocks, sockets or audio/MIDI devices.
 *   Firmware and state come in as bytes and go out as bytes.
 * - The caller owns every buffer it passes. The device copies what it needs and keeps no pointer
 *   past the call.
 * - Real time: after gm_device_prepare, gm_device_render and gm_device_send_panel do not allocate,
 *   lock or log, except that MIDI carrying SysEx may allocate (in or out). Create, destroy, state
 *   and gm_device_send_midi are control calls and may allocate.
 * - Versioning: gm_abi_version() returns GM_DEVICE_ABI_VERSION of the library, the major
 *   version: a change there breaks callers. Structs begin with struct_size; the library reads
 *   and writes only the fields that size covers, so a struct may grow at the end in a later minor
 *   version without breaking older callers.
 * - Negotiation: a host asks gm_get_device_api for the major version it was built for and gets a
 *   table of the library's functions, its minor version and its capabilities (GM_CAP_*). An
 *   optional function a library lacks is null in the table and its bit is clear; a host checks
 *   the bits it needs once, instead of finding out from a missing symbol. gm_device_info says
 *   which of those a given unit (model) supports. The table is the supported way in: a process
 *   may hold several device libraries (dlopen RTLD_LOCAL), whose same-named exports would
 *   otherwise interpose on each other. The named exports remain for version 1 callers.
 * - This header is the one definition. The Rust bindings are generated from it (bindgen in the
 *   studio's gm-device build); nothing restates it by hand.
 *
 * Minor versions of major 1:
 *   0  the core functions.
 *   1  remote front panel and post_sysex (were added without a version step; see GM_CAP_*).
 *   2  gm_get_device_api, GM_CAP_*, gm_device_info.capabilities.
 *   3  GM_MODEL_DRUMSTATION; ROM images with roles and create flags in gm_device_create_info;
 *      gm_device_info.firmware_id.
 *   4  GM_CAP_MIDI_IN_PACED.
 */
#ifndef GM_DEVICE_H
#define GM_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#	define GM_API __declspec(dllexport)
#else
#	define GM_API __attribute__((visibility("default")))
#endif

#define GM_DEVICE_ABI_VERSION 1u
#define GM_DEVICE_ABI_MINOR 4u

/* Capabilities. In gm_device_api.capabilities: what the library implements (its optional table
   entries are non-null exactly when their bit is set). In gm_device_info.capabilities: what one
   unit supports. A bit is a promise about behaviour, not only that a function exists. */
#define GM_CAP_AUDIO_INPUT       (1u << 0)	/* render reads inputs; input_channels > 0 */
#define GM_CAP_MIDI_OUT          (1u << 1)	/* render can return midi_out events */
#define GM_CAP_ADVANCE           (1u << 2)	/* advance */
#define GM_CAP_PANEL_INPUT       (1u << 3)	/* send_panel */
#define GM_CAP_STATE             (1u << 4)	/* get_state / set_state */
#define GM_CAP_DEFERRED_RESTORE  (1u << 5)	/* is_restore_pending / service_restore can be needed */
#define GM_CAP_POST_SYSEX        (1u << 6)	/* post_sysex, from any thread */
#define GM_CAP_REMOTE_PANEL      (1u << 7)	/* remote_panel_start / _stop / _clients */
#define GM_CAP_RATE_PROVISIONAL  (1u << 8)	/* sample_rate is not established against hardware */
#define GM_CAP_MIDI_IN_PACED     (1u << 9)	/* the unit takes MIDI IN bytes at the UART rate itself, from
											   a message's frame on: a host modelling a cable passes
											   a message on when its first byte has arrived (minor 4) */

typedef int32_t gm_status;
#define GM_OK               0
#define GM_ERROR_ARGUMENT  -1	/* a null pointer, a too-small struct_size, frames over prepare */
#define GM_ERROR_FIRMWARE  -2	/* the ROM is missing or is not for this model */
#define GM_ERROR_STATE     -3	/* a state could not be decoded or applied */
#define GM_ERROR_REJECTED  -4	/* a queue refused the input (full); nothing was changed */
#define GM_ERROR_CAPACITY  -5	/* an output buffer was too small; see the reported size */
#define GM_ERROR_INTERNAL  -6	/* anything else; the device should be destroyed */

/* Models, one registry for every device library. */
#define GM_MODEL_MACHINEDRUM 1u		/* libgm_md */
#define GM_MODEL_MONOMACHINE 2u		/* libgm_md */
#define GM_MODEL_DRUMSTATION 3u		/* libgm_drumstation */

/* Roles of the ROM images a unit is created from (minor 3). */
#define GM_ROM_MAIN    0u	/* a unit with one image; libgm_md also takes it as rom/rom_size */
#define GM_ROM_DS_U15  1u	/* DrumStation controller program, 32 KiB */
#define GM_ROM_DS_U28  2u	/* DrumStation DSP code and samples, 512 KiB */

typedef struct gm_rom_image
{
	uint32_t role;				/* GM_ROM_* */
	const uint8_t* data;		/* copied */
	size_t size;
} gm_rom_image;

/* gm_device_create_info.flags (minor 3) */
#define GM_CREATE_ALLOW_UNKNOWN_FIRMWARE (1u << 0)	/* accept images whose checksum no known release has */

typedef struct gm_device gm_device;

typedef struct gm_device_create_info
{
	uint32_t struct_size;		/* sizeof(gm_device_create_info) */
	uint32_t model;				/* GM_MODEL_* */
	const uint8_t* rom;			/* the firmware image; copied. Null when images are given. */
	size_t rom_size;
	/* minor 3 */
	const gm_rom_image* images;	/* for units with several images; image_count entries */
	uint32_t image_count;
	uint32_t flags;				/* GM_CREATE_* */
} gm_device_create_info;

typedef struct gm_device_info
{
	uint32_t struct_size;
	uint32_t model;
	uint32_t sample_rate;		/* the unit's own rate in Hz; render runs at this rate */
	uint32_t input_channels;
	uint32_t output_channels;
	uint32_t capabilities;		/* GM_CAP_* this unit supports (minor 2; 0 from older libraries) */
	const char* firmware_id;	/* the release the ROMs were recognised as, e.g. "drumstation-v1.2";
								   null when the library does not check (minor 3); static */
} gm_device_info;

/* A MIDI message. frame is relative to the start of the render call it travels with. */
typedef struct gm_midi_event
{
	uint32_t frame;
	uint32_t size;				/* bytes at data */
	const uint8_t* data;
} gm_midi_event;

/* A MIDI message the unit sent: its bytes are data_offset .. data_offset + size in the
   caller's data buffer. frame is within the block (0 .. frames - 1), so a host can pass it
   on as another unit's midi_in frame unchanged. */
typedef struct gm_midi_out_event
{
	uint32_t frame;
	uint32_t size;
	uint32_t data_offset;
} gm_midi_out_event;

typedef struct gm_render_io
{
	uint32_t struct_size;
	uint32_t frames;
	const float* const* inputs;	/* input_channels pointers, or null for silence; a null entry is silence */
	float* const* outputs;		/* output_channels pointers; a null entry is not written */
	const gm_midi_event* midi_in;	/* sorted by frame; may be null when midi_in_count is 0 */
	uint32_t midi_in_count;
	gm_midi_out_event* midi_out;	/* may be null when midi_out_capacity is 0 */
	uint32_t midi_out_capacity;
	uint32_t midi_out_count;	/* out: events written */
	uint8_t* midi_out_data;
	uint32_t midi_out_data_capacity;
	uint32_t midi_out_data_size;	/* out: bytes written */
	uint32_t midi_out_dropped;	/* out: events that did not fit and were discarded */
} gm_render_io;

GM_API uint32_t gm_abi_version(void);

/* Boots nothing yet: the unit starts running on the first render or advance. */
GM_API gm_status gm_device_create(const gm_device_create_info* info, gm_device** device);
GM_API void gm_device_destroy(gm_device* device);

/* Message of the last failure on this device, or "" . Valid until the next call into it. */
GM_API const char* gm_device_last_error(const gm_device* device);

GM_API gm_status gm_device_get_info(const gm_device* device, gm_device_info* info);

/* Sizes internal buffers for render calls of up to max_frames. Call before real-time use. */
GM_API gm_status gm_device_prepare(gm_device* device, uint32_t max_frames);

/* Runs the unit for frames without producing output, as during boot or setup. MIDI the unit
   sends meanwhile is kept and comes out of the next render (frame 0), so size that render's
   midi_out for it, or accept midi_out_dropped. */
GM_API gm_status gm_device_advance(gm_device* device, uint32_t frames);

/* One host block: delivers midi_in at its frames, renders frames of audio, collects MIDI out. */
GM_API gm_status gm_device_render(gm_device* device, gm_render_io* io);

/* Queues one MIDI message (short or SysEx) for the unit's MIDI IN now, ahead of the next
   advance or render, the way a byte stream on the DIN cable arrives. */
GM_API gm_status gm_device_send_midi(gm_device* device, const uint8_t* data, uint32_t size);

/* Queues a front-panel row snapshot (row 0x20.., mask of held keys) or an encoder packet. */
GM_API gm_status gm_device_send_panel(gm_device* device, uint8_t row, uint8_t mask);

/* 1 once the unit's DSPs have booted and it produces audio; 1 once it accepts MIDI. */
GM_API int gm_device_is_audio_ready(const gm_device* device);
GM_API int gm_device_is_midi_ready(const gm_device* device);

/* The unit's persistent memory (patch RAM, flash), in the same bytes the plug-in stores as the
   device part of its project state. With buffer null, *size gets the needed size and the result
   is GM_OK; with capacity too small, *size gets it and the result is GM_ERROR_CAPACITY. */
GM_API gm_status gm_device_get_state(gm_device* device, uint8_t* buffer, size_t capacity, size_t* size);
/* Restores such a state. Most states apply at once. A Machinedrum state that needs its factory
   flash baseline first boots a candidate unit in the background of later render calls (not
   advance); restore_pending reports that, and the host then calls service_restore (a control
   call: not concurrently with render) until it returns GM_OK, which swaps the restored unit in. */
GM_API gm_status gm_device_set_state(gm_device* device, const uint8_t* data, size_t size);
GM_API int gm_device_is_restore_pending(const gm_device* device);
/* GM_OK: nothing pending, or the restore finished now. GM_ERROR_REJECTED: still preparing, call
   again later. GM_ERROR_STATE: the candidate failed; the running unit is unchanged. */
GM_API gm_status gm_device_service_restore(gm_device* device);

/* ---- Remote front panel (additive to version 1) --------------------------------------------

   The unit's front panel as a web page: an HTTP server on 127.0.0.1-reachable `port` (or the next
   free one) serving the panel app from `resource_dir` and a WebSocket that pushes the LCD and LEDs
   and takes button, encoder and SysEx input (the protocol of md::RemotePanelServer). It runs on
   threads of its own; button and encoder input go to the unit's panel queue, SysEx is handed to
   the next render, and the main outputs are streamed to pages that ask (sound) from render.
   start and stop are control calls (not concurrently with render or destroy); destroy stops it. */
typedef struct gm_remote_panel_info
{
	uint32_t struct_size;
	int32_t port;				/* first port to try */
	const char* resource_dir;	/* folder with index.html / index-mm.html, panel.js, ... (UTF-8) */
} gm_remote_panel_info;

GM_API gm_status gm_device_remote_panel_start(gm_device* device, const gm_remote_panel_info* info, int32_t* bound_port);
GM_API void gm_device_remote_panel_stop(gm_device* device);

/* Posts a complete SysEx message (F0 .. F7) for the unit's MIDI IN, delivered at frame 0 of the
   next render. Unlike the rest of this API it may be called from any thread, concurrently with
   render: for a host whose MIDI arrives on a thread of its own. GM_ERROR_REJECTED: 256 messages
   are already waiting. */
GM_API gm_status gm_device_post_sysex(gm_device* device, const uint8_t* data, uint32_t size);
/* Pages currently connected. */
GM_API int32_t gm_device_remote_panel_clients(const gm_device* device);

/* ---- Negotiation (minor 2) ---------------------------------------------------------------- */

typedef struct gm_device_api
{
	uint32_t struct_size;		/* sizeof the library's table; read no entry past it */
	uint32_t abi_major;			/* GM_DEVICE_ABI_VERSION */
	uint32_t abi_minor;			/* GM_DEVICE_ABI_MINOR of the library */
	uint32_t capabilities;		/* GM_CAP_* of the library: the union of its models' */
	const char* implementation;	/* e.g. "libgm_md: Machinedrum, Monomachine"; static */

	/* Always present. */
	gm_status (*create)(const gm_device_create_info* info, gm_device** device);
	void (*destroy)(gm_device* device);
	const char* (*last_error)(const gm_device* device);
	gm_status (*get_info)(const gm_device* device, gm_device_info* info);
	gm_status (*prepare)(gm_device* device, uint32_t max_frames);
	gm_status (*render)(gm_device* device, gm_render_io* io);
	gm_status (*send_midi)(gm_device* device, const uint8_t* data, uint32_t size);
	int (*is_audio_ready)(const gm_device* device);
	int (*is_midi_ready)(const gm_device* device);

	/* Null unless the GM_CAP_ bit named is set. */
	gm_status (*advance)(gm_device* device, uint32_t frames);					/* ADVANCE */
	gm_status (*send_panel)(gm_device* device, uint8_t row, uint8_t mask);		/* PANEL_INPUT */
	gm_status (*get_state)(gm_device* device, uint8_t* buffer, size_t capacity, size_t* size);	/* STATE */
	gm_status (*set_state)(gm_device* device, const uint8_t* data, size_t size);	/* STATE */
	int (*is_restore_pending)(const gm_device* device);							/* DEFERRED_RESTORE */
	gm_status (*service_restore)(gm_device* device);								/* DEFERRED_RESTORE */
	gm_status (*post_sysex)(gm_device* device, const uint8_t* data, uint32_t size);	/* POST_SYSEX */
	gm_status (*remote_panel_start)(gm_device* device, const gm_remote_panel_info* info, int32_t* bound_port);	/* REMOTE_PANEL */
	void (*remote_panel_stop)(gm_device* device);								/* REMOTE_PANEL */
	int32_t (*remote_panel_clients)(const gm_device* device);					/* REMOTE_PANEL */
} gm_device_api;

/* The library's function table for a host built against major version `abi_major`. GM_OK and
   *api set (static, valid while the library is loaded); GM_ERROR_REJECTED if the library does not
   implement that major version, *api null. Any thread, any time. */
GM_API gm_status gm_get_device_api(uint32_t abi_major, const gm_device_api** api);

#ifdef __cplusplus
}
#endif

#endif
