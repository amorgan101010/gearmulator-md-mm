/*
 * gmMdDeviceTest: drives libgm_md only through gm_device.h, from C.
 *
 * Per model with firmware (GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN):
 * argument and firmware errors, boot to audio/MIDI readiness, audible output from MIDI sent at
 * frame offsets, MIDI out, a state round trip, and two instances driven alike producing the same
 * output hash. Exits 77 (skipped) when neither firmware is set, 1 on any failure.
 */
#include "gmDeviceAbi/gm_device.h"

#include <stddef.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 512u
#define RATE 44100u

static int g_failures = 0;

#define CHECK(cond, ...) do { if(!(cond)) { ++g_failures; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while(0)

static unsigned char* readFile(const char* path, size_t* size)
{
	FILE* f = fopen(path, "rb");
	if(!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* data = n > 0 ? malloc((size_t)n) : NULL;
	if(data && fread(data, 1, (size_t)n, f) != (size_t)n)
	{
		free(data);
		data = NULL;
	}
	fclose(f);
	*size = data ? (size_t)n : 0;
	return data;
}

static gm_device* boot(const unsigned char* rom, size_t romSize, uint32_t model)
{
	gm_device_create_info info;
	memset(&info, 0, sizeof(info));
	info.struct_size = sizeof(info);
	info.model = model;
	info.rom = rom;
	info.rom_size = romSize;
	gm_device* device = NULL;
	gm_status status = gm_device_create(&info, &device);
	CHECK(status == GM_OK && device, "create returned %d", status);
	{
		/* Minor 3: the same ROM as a GM_ROM_MAIN image creates the unit too. */
		const gm_rom_image image = {GM_ROM_MAIN, rom, romSize};
		gm_device_create_info byImage = info;
		byImage.rom = NULL;
		byImage.rom_size = 0;
		byImage.images = &image;
		byImage.image_count = 1;
		gm_device* other = NULL;
		status = gm_device_create(&byImage, &other);
		CHECK(status == GM_OK && other, "create from a GM_ROM_MAIN image returned %d", status);
		gm_device_destroy(other);
	}
	if(!device)
		return NULL;
	/* 20 emulated seconds in 256-frame steps, as mdmmBench boots. */
	for(uint32_t i = 0; i < RATE * 20 / 256; ++i)
		gm_device_advance(device, 256);
	CHECK(gm_device_is_audio_ready(device), "audio not ready after boot");
	CHECK(gm_device_is_midi_ready(device), "MIDI not ready after boot");
	CHECK(gm_device_prepare(device, BLOCK) == GM_OK, "prepare: %s", gm_device_last_error(device));
	return device;
}

typedef struct Run
{
	unsigned long long hash;
	double energy;
	uint32_t midiOut;
} Run;

/* Plays a note on every MIDI channel 0-5 every half second for _seconds, sent as events at a
   frame offset inside the block, and hashes all six outputs (FNV-1a over the float bits). */
static Run play(gm_device* device, double seconds)
{
	static float buffers[6][BLOCK];
	float* outputs[6];
	for(int ch = 0; ch < 6; ++ch)
		outputs[ch] = buffers[ch];
	static gm_midi_out_event midiOut[4096];
	static uint8_t midiOutData[65536];
	Run run = {1469598103934665603ull, 0.0, 0};
	const uint32_t blocks = (uint32_t)(seconds * RATE / BLOCK);
	for(uint32_t b = 0; b < blocks; ++b)
	{
		uint8_t notes[6][3];
		gm_midi_event events[6];
		uint32_t count = 0;
		if(b % (RATE / 2 / BLOCK) == 0)
		{
			for(uint8_t ch = 0; ch < 6; ++ch, ++count)
			{
				notes[ch][0] = (uint8_t)(0x90 | ch);
				notes[ch][1] = (uint8_t)(48 + (b / (RATE / 2 / BLOCK)) % 24);
				notes[ch][2] = 100;
				events[count].frame = 100;
				events[count].size = 3;
				events[count].data = notes[ch];
			}
		}
		gm_render_io io;
		memset(&io, 0, sizeof(io));
		io.struct_size = sizeof(io);
		io.frames = BLOCK;
		io.outputs = outputs;
		io.midi_in = events;
		io.midi_in_count = count;
		io.midi_out = midiOut;
		io.midi_out_capacity = 4096;
		io.midi_out_data = midiOutData;
		io.midi_out_data_capacity = sizeof(midiOutData);
		const gm_status status = gm_device_render(device, &io);
		if(status != GM_OK)
		{
			CHECK(0, "render: %d %s", status, gm_device_last_error(device));
			break;
		}
		run.midiOut += io.midi_out_count;
		CHECK(io.midi_out_dropped == 0, "MIDI out dropped %u", io.midi_out_dropped);
		for(uint32_t i = 0; i < io.midi_out_count; ++i)
			CHECK(midiOut[i].frame < BLOCK, "MIDI out at frame %u of a %u-frame block", midiOut[i].frame, BLOCK);
		for(int ch = 0; ch < 6; ++ch)
			for(uint32_t i = 0; i < BLOCK; ++i)
			{
				uint32_t bits;
				memcpy(&bits, &buffers[ch][i], 4);
				for(int k = 0; k < 4; ++k)
				{
					run.hash ^= (bits >> (8 * k)) & 0xff;
					run.hash *= 1099511628211ull;
				}
				run.energy += (double)buffers[ch][i] * buffers[ch][i];
			}
	}
	return run;
}

static void testErrors(const unsigned char* otherRom, size_t otherSize, uint32_t model)
{
	gm_device* device = (gm_device*)1;
	CHECK(gm_device_create(NULL, &device) == GM_ERROR_ARGUMENT && device == NULL, "null info");
	gm_device_create_info info;
	memset(&info, 0, sizeof(info));
	info.struct_size = offsetof(gm_device_create_info, images) - 1;	/* shorter than any minor's */
	info.model = model;
	info.rom = otherRom;
	info.rom_size = otherSize;
	CHECK(gm_device_create(&info, &device) == GM_ERROR_ARGUMENT, "short struct_size accepted");
	info.struct_size = sizeof(info);
	info.model = 99;
	CHECK(gm_device_create(&info, &device) == GM_ERROR_ARGUMENT, "bad model accepted");
	info.model = model;
	if(otherRom)
		CHECK(gm_device_create(&info, &device) == GM_ERROR_FIRMWARE, "the other model's ROM accepted");
	CHECK(gm_device_render(NULL, NULL) == GM_ERROR_ARGUMENT, "null device render");
	CHECK(gm_device_send_midi(NULL, NULL, 0) == GM_ERROR_ARGUMENT, "null device send");
	CHECK(strcmp(gm_device_last_error(NULL), "") == 0, "last_error(NULL)");
	gm_device_destroy(NULL);
}

static void testModel(const char* name, const unsigned char* rom, size_t romSize, uint32_t model,
	const unsigned char* otherRom, size_t otherSize)
{
	fprintf(stderr, "== %s\n", name);
	testErrors(otherRom, otherSize, model);

	gm_device* a = boot(rom, romSize, model);
	gm_device* b = boot(rom, romSize, model);
	if(!a || !b)
		return;

	gm_device_info info;
	memset(&info, 0, sizeof(info));
	info.struct_size = sizeof(info);
	CHECK(gm_device_get_info(a, &info) == GM_OK && info.sample_rate == RATE && info.output_channels == 6
		&& info.input_channels == 2 && info.model == model, "info");
	{
		/* Per-unit capabilities sit inside the library's, and a minor-0 sized struct still works. */
		const gm_device_api* api = NULL;
		CHECK(gm_get_device_api(GM_DEVICE_ABI_VERSION, &api) == GM_OK && api, "api");
		CHECK(api && (info.capabilities & ~api->capabilities) == 0, "unit caps 0x%x outside library caps", info.capabilities);
		CHECK((info.capabilities & GM_CAP_STATE) && (info.capabilities & GM_CAP_MIDI_OUT), "unit caps 0x%x", info.capabilities);
		CHECK(!!(info.capabilities & GM_CAP_DEFERRED_RESTORE) == (model == GM_MODEL_MACHINEDRUM), "deferred restore bit");
		CHECK((info.capabilities & GM_CAP_AUDIO_INPUT) && info.input_channels == 2, "audio input bit");
		gm_device_info old;
		memset(&old, 0xab, sizeof(old));
		old.struct_size = offsetof(gm_device_info, capabilities);
		CHECK(gm_device_get_info(a, &old) == GM_OK && old.output_channels == 6
			&& old.capabilities == 0xabababab, "a minor-0 info struct must be accepted and not written past");
		memset(&old, 0xab, sizeof(old));
		old.struct_size = offsetof(gm_device_info, firmware_id);
		CHECK(gm_device_get_info(a, &old) == GM_OK && old.capabilities == info.capabilities
			&& *(const unsigned char*)&old.firmware_id == 0xab, "a minor-2 info struct must not be written past");
		CHECK(info.firmware_id == NULL, "libgm_md does not identify the release");
	}

	/* Render guards. */
	{
		float buffer[BLOCK * 2];
		float* outputs[6] = {buffer, buffer, NULL, NULL, NULL, NULL};
		gm_render_io io;
		memset(&io, 0, sizeof(io));
		io.struct_size = sizeof(io);
		io.frames = BLOCK * 2;
		io.outputs = outputs;
		CHECK(gm_device_render(a, &io) == GM_ERROR_ARGUMENT, "frames over prepare accepted");
		CHECK(strlen(gm_device_last_error(a)) > 0, "no error message");
		/* A minor-4 host's struct ends before control_in; both units render it, so they stay alike. */
		io.frames = BLOCK;
		io.struct_size = offsetof(gm_render_io, control_in);
		CHECK(gm_device_render(a, &io) == GM_OK && gm_device_render(b, &io) == GM_OK, "a minor-4 render struct refused");
		io.struct_size = offsetof(gm_render_io, control_in) - 1;
		CHECK(gm_device_render(a, &io) == GM_ERROR_ARGUMENT, "a struct shorter than any minor's accepted");
	}

	/* The Monomachine needs a machine on its tracks to sound: GND-SIN on all six. */
	if(model == GM_MODEL_MONOMACHINE)
		for(uint8_t track = 0; track < 6; ++track)
		{
			const uint8_t assign[] = {0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, track, 1, 1, 0xf7};
			CHECK(gm_device_send_midi(a, assign, sizeof(assign)) == GM_OK, "assign a");
			CHECK(gm_device_send_midi(b, assign, sizeof(assign)) == GM_OK, "assign b");
			for(uint32_t i = 0; i < RATE / 256; ++i)
			{
				gm_device_advance(a, 256);
				gm_device_advance(b, 256);
			}
		}

	const Run ra = play(a, 3.0);
	const Run rb = play(b, 3.0);
	fprintf(stderr, "   hash %016llx / %016llx  rms %.4g  MIDI out %u\n", ra.hash, rb.hash,
		sqrt(ra.energy / (3.0 * RATE * 6)), ra.midiOut);
	CHECK(ra.hash == rb.hash, "two instances driven alike differ");
	CHECK(ra.energy > 1e-3, "no audible output");

	/* State: size query, too-small buffer, full copy, restore into the other instance. */
	size_t size = 0;
	CHECK(gm_device_get_state(a, NULL, 0, &size) == GM_OK && size > 0, "state size query");
	unsigned char small[4];
	size_t smallSize = 0;
	CHECK(gm_device_get_state(a, small, sizeof(small), &smallSize) == GM_ERROR_CAPACITY && smallSize == size,
		"too-small state buffer");
	unsigned char* state = malloc(size);
	size_t got = 0;
	CHECK(state && gm_device_get_state(a, state, size, &got) == GM_OK && got == size, "state copy");
	if(state)
	{
		const gm_status set = gm_device_set_state(b, state, size);
		CHECK(set == GM_OK, "set_state: %d %s", set, gm_device_last_error(b));
		CHECK(gm_device_set_state(b, state, 0) == GM_ERROR_ARGUMENT, "empty state accepted");
		/* Run until any deferred restore lands, then the unit must still play. */
		for(int i = 0; i < 400 && gm_device_is_restore_pending(b); ++i)
		{
			play(b, 0.1);
			if(gm_device_service_restore(b) == GM_ERROR_STATE)
				CHECK(0, "deferred restore failed: %s", gm_device_last_error(b));
		}
		CHECK(!gm_device_is_restore_pending(b), "restore still pending");
		free(state);
	}

	gm_device_destroy(a);
	gm_device_destroy(b);
}

int main(void)
{
	CHECK(gm_abi_version() == GM_DEVICE_ABI_VERSION, "abi version %u", gm_abi_version());
	{
		/* Negotiation: the table matches the named exports, and its optional entries are null
		   exactly when their capability bit is clear. */
		const gm_device_api* api = (const gm_device_api*)1;
		CHECK(gm_get_device_api(GM_DEVICE_ABI_VERSION + 1, &api) == GM_ERROR_REJECTED && api == NULL, "other major");
		CHECK(gm_get_device_api(GM_DEVICE_ABI_VERSION, NULL) == GM_ERROR_ARGUMENT, "null out");
		CHECK(gm_get_device_api(GM_DEVICE_ABI_VERSION, &api) == GM_OK && api, "api");
		if(api)
		{
			CHECK(api->struct_size == sizeof(gm_device_api) && api->abi_major == GM_DEVICE_ABI_VERSION
				&& api->abi_minor == GM_DEVICE_ABI_MINOR && api->implementation, "api header");
			CHECK(api->create == gm_device_create && api->destroy == gm_device_destroy && api->render == gm_device_render
				&& api->get_info == gm_device_get_info && api->prepare == gm_device_prepare
				&& api->send_midi == gm_device_send_midi && api->last_error == gm_device_last_error
				&& api->is_audio_ready == gm_device_is_audio_ready && api->is_midi_ready == gm_device_is_midi_ready,
				"core entries");
			const uint32_t c = api->capabilities;
#define CAP_MATCHES(bit, entry) (!!(c & (bit)) == ((entry) != NULL))
			CHECK(CAP_MATCHES(GM_CAP_ADVANCE, api->advance) && CAP_MATCHES(GM_CAP_PANEL_INPUT, api->send_panel)
				&& CAP_MATCHES(GM_CAP_STATE, api->get_state) && CAP_MATCHES(GM_CAP_STATE, api->set_state)
				&& CAP_MATCHES(GM_CAP_DEFERRED_RESTORE, api->service_restore)
				&& CAP_MATCHES(GM_CAP_DEFERRED_RESTORE, api->is_restore_pending)
				&& CAP_MATCHES(GM_CAP_POST_SYSEX, api->post_sysex)
				&& CAP_MATCHES(GM_CAP_REMOTE_PANEL, api->remote_panel_start)
				&& CAP_MATCHES(GM_CAP_REMOTE_PANEL, api->remote_panel_stop)
				&& CAP_MATCHES(GM_CAP_REMOTE_PANEL, api->remote_panel_clients), "caps 0x%x vs entries", c);
#undef CAP_MATCHES
		}
	}
	const char* mdPath = getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const char* mmPath = getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	size_t mdSize = 0, mmSize = 0;
	unsigned char* md = mdPath ? readFile(mdPath, &mdSize) : NULL;
	unsigned char* mm = mmPath ? readFile(mmPath, &mmSize) : NULL;
	if(!md && !mm)
	{
		fprintf(stderr, "skipped: set GEARMULATOR_MD_FIRMWARE_BIN and/or GEARMULATOR_MM_FIRMWARE_BIN\n");
		return 77;
	}
	if(md)
		testModel("Machinedrum", md, mdSize, GM_MODEL_MACHINEDRUM, mm, mmSize);
	if(mm)
		testModel("Monomachine", mm, mmSize, GM_MODEL_MONOMACHINE, md, mdSize);
	free(md);
	free(mm);
	if(g_failures)
		fprintf(stderr, "%d failure(s)\n", g_failures);
	else
		fprintf(stderr, "gmMdDeviceTest: all checks passed\n");
	return g_failures ? 1 : 0;
}
