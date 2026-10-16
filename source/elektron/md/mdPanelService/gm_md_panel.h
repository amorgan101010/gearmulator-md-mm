/*
 * gm_md_panel.h: the Machinedrum/Monomachine web front panel as a service of the host, not of
 * the device.
 *
 * libgm_md_panel serves the remote panel page (the one the plug-in serves, index.html /
 * index-mm.html, and its WebSocket protocol, md::RemotePanelServer) on its own threads, sockets
 * and files. It never touches a unit: everything it learns and does goes through the callbacks
 * the host gives it, which map onto the device ABI (gm_device.h):
 *   read_status  -> the unit's GM_CAP_STATUS read_status (the MD/MM status frame)
 *   send_panel   -> a GM_CAP_CONTROLS control message [command, argument] for the unit
 *   send_sysex   -> a SysEx message for the unit's MIDI IN, in render's midi_in
 * so the device library (libgm_md) needs no network, thread or file of its own.
 *
 * The callbacks run on the service's threads, any time between start and stop, possibly
 * concurrently; the host makes them thread-safe (read_status already is). Sound for the page
 * comes from gm_md_panel_feed_audio, from one thread of the host's: copy the unit's main
 * outputs there from a non-real-time thread (the service converts them).
 */
#ifndef GM_MD_PANEL_H
#define GM_MD_PANEL_H

#include "gmDeviceAbi/gm_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gm_md_panel gm_md_panel;

typedef struct gm_md_panel_host
{
	uint32_t struct_size;		/* sizeof(gm_md_panel_host) */
	uint32_t model;				/* GM_MODEL_MACHINEDRUM or GM_MODEL_MONOMACHINE */
	int32_t port;				/* first port to try */
	const char* resource_dir;	/* folder with index.html / index-mm.html, panel.js, ... (UTF-8) */
	void* user;					/* passed to every callback */
	gm_status (*read_status)(void* user, uint8_t* buffer, size_t capacity, size_t* size, uint64_t* generation);
	int (*send_panel)(void* user, uint8_t command, uint8_t argument);	/* 1: queued */
	int (*send_sysex)(void* user, const uint8_t* data, uint32_t size);	/* 1: queued */
} gm_md_panel_host;

/* Starts serving on host->port or the next free port. Null on failure (no free port, bad
   arguments). *bound_port gets the port. */
GM_API gm_md_panel* gm_md_panel_start(const gm_md_panel_host* host, int32_t* bound_port);

/* Pages currently connected. */
GM_API int32_t gm_md_panel_clients(const gm_md_panel* panel);

/* One block of the unit's main outputs at sample_rate (0: not real time, not streamed), from one
   host thread at a time; never blocks. */
GM_API void gm_md_panel_feed_audio(gm_md_panel* panel, const float* left, const float* right, uint32_t frames, uint32_t sample_rate);

/* Stops serving and joins its threads; no callback runs after it returns. */
GM_API void gm_md_panel_stop(gm_md_panel* panel);

#ifdef __cplusplus
}
#endif

#endif
