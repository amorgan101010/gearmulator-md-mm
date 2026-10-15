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
 * - Versioning: gm_abi_version() returns GM_DEVICE_ABI_VERSION of the library. Structs begin
 *   with struct_size; the library reads only the fields that size covers, so a struct may grow
 *   at the end in a later version without breaking older callers.
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

typedef int32_t gm_status;
#define GM_OK               0
#define GM_ERROR_ARGUMENT  -1	/* a null pointer, a too-small struct_size, frames over prepare */
#define GM_ERROR_FIRMWARE  -2	/* the ROM is missing or is not for this model */
#define GM_ERROR_STATE     -3	/* a state could not be decoded or applied */
#define GM_ERROR_REJECTED  -4	/* a queue refused the input (full); nothing was changed */
#define GM_ERROR_CAPACITY  -5	/* an output buffer was too small; see the reported size */
#define GM_ERROR_INTERNAL  -6	/* anything else; the device should be destroyed */

#define GM_MODEL_MACHINEDRUM 1u
#define GM_MODEL_MONOMACHINE 2u

typedef struct gm_device gm_device;

typedef struct gm_device_create_info
{
	uint32_t struct_size;		/* sizeof(gm_device_create_info) */
	uint32_t model;				/* GM_MODEL_* */
	const uint8_t* rom;			/* the firmware image; copied */
	size_t rom_size;
} gm_device_create_info;

typedef struct gm_device_info
{
	uint32_t struct_size;
	uint32_t model;
	uint32_t sample_rate;		/* the unit's own rate in Hz; render runs at this rate */
	uint32_t input_channels;
	uint32_t output_channels;
} gm_device_info;

/* A MIDI message. frame is relative to the start of the render call it travels with. */
typedef struct gm_midi_event
{
	uint32_t frame;
	uint32_t size;				/* bytes at data */
	const uint8_t* data;
} gm_midi_event;

/* A MIDI message the unit sent: its bytes are data_offset .. data_offset + size in the
   caller's data buffer. */
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

#ifdef __cplusplus
}
#endif

#endif
