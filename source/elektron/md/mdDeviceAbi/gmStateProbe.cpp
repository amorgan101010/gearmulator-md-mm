// What a host saving state periodically meets, measured through the device ABI: how often a
// unit's state bytes change while a pattern plays with nobody touching it, how long get_state
// takes, and (with tools' rtcount.so or alloccount.so preloaded) how much it allocates. Not a
// test: a local tool.
//
//   GEARMULATOR_MD_FIRMWARE_BIN=... gmStateProbe md|mm [seconds:60] [state file to restore]
//   GEARMULATOR_MD_FIRMWARE_BIN=... gmStateProbe md|mm dumps
//   GEARMULATOR_MD_FIRMWARE_BIN=... gmStateProbe md|mm stall [state file]
//
// "stall": after the boot (and the restore, if a state is given), times every 32-frame render
// for 5 emulated seconds, as the engine's worker would run them right after it starts, and prints
// each that took over 2 ms with its emulated time.
//
// "dumps": requests the global, kit, pattern and song dumps (and the MM's DigiPro waveform) over
// SysEx and prints the size of every SysEx message the unit sends back: the largest messages a
// host must carry in and out without allocating.
//
// Boots for 20 emulated seconds, restores the state file if given, plays factory pattern C04 (MD)
// or B15 (MM) as mdmmBench does, then renders in 32-frame blocks and once per emulated second
// takes the state: its size, the time get_state took, the bytes that differ from the previous
// second's and the address range they lie in.

#include "gmDeviceAbi/gm_device.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	std::vector<uint8_t> readFile(const char* _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), {}};
	}

	void advance(gm_device* _d, uint32_t _frames)
	{
		while(_frames)
		{
			const auto n = _frames < 256 ? _frames : 256u;
			gm_device_advance(_d, n);
			_frames -= n;
		}
	}
}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::fprintf(stderr, "usage: gmStateProbe md|mm [seconds] [state file]\n");
		return 2;
	}
	const bool mm = std::strcmp(argv[1], "mm") == 0;
	const int seconds = argc > 2 ? std::atoi(argv[2]) : 60;
	const char* romPath = std::getenv(mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
	if(!romPath)
		return 77;
	const auto rom = readFile(romPath);
	gm_device_create_info info{};
	info.struct_size = sizeof(info);
	info.model = mm ? GM_MODEL_MONOMACHINE : GM_MODEL_MACHINEDRUM;
	info.rom = rom.data();
	info.rom_size = rom.size();
	gm_device* d = nullptr;
	if(gm_device_create(&info, &d) != GM_OK)
		return 1;
	advance(d, 44100 * 20);
	gm_device_prepare(d, 32);
	std::vector<std::vector<float>> out(6, std::vector<float>(32));
	float* outs[6];
	for(int c = 0; c < 6; ++c)
		outs[c] = out[c].data();
	gm_render_io io{};
	io.struct_size = sizeof(io);
	io.frames = 32;
	io.outputs = outs;
	const auto render = [&] { io.midi_in_count = 0; gm_device_render(d, &io); };
	if(argc > 3)
	{
		const auto state = readFile(argv[3]);
		const auto rc = gm_device_set_state(d, state.data(), state.size());
		int tries = 0;
		while(rc == GM_OK && gm_device_is_restore_pending(d) && tries++ < 200000)
		{
			render();
			if(gm_device_service_restore(d) == GM_OK)
				break;
		}
		std::printf("restored %s (%zu bytes): %s after %d renders\n", argv[3], state.size(), rc == GM_OK ? "ok" : gm_device_last_error(d), tries);
	}
	// "stall": right after the boot and the restore, as the engine starts a unit.
	if(argc > 2 && std::strcmp(argv[2], "stall") == 0)
	{
		double worst = 0, total = 0;
		int slow = 0;
		const int stallSeconds = std::getenv("STALL_SECONDS") ? std::atoi(std::getenv("STALL_SECONDS")) : 5;
		const int blocks = 44100 * stallSeconds / 32;
		for(int b = 0; b < blocks; ++b)
		{
			const auto t0 = std::chrono::steady_clock::now();
			render();
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			total += ms;
			worst = std::max(worst, ms);
			if(ms > 2.0)
			{
				++slow;
				std::printf("render %5d at %.3f s: %.2f ms (CLOCK_MONOTONIC %.6f s at its start)\n", b, b * 32.0 / 44100, ms,
					std::chrono::duration<double>(t0.time_since_epoch()).count());
			}
		}
		std::printf("stall: %d renders, mean %.3f ms, worst %.2f ms, %d over 2 ms\n", blocks, total / blocks, worst, slow);
		gm_device_destroy(d);
		return 0;
	}
	// Factory pattern and PLAY, as mdmmBench's pattern mode.
	const uint8_t slot = mm ? 16 + 14 : 32 + 3;	// B15 / C04
	const uint8_t load[] = {0xf0, 0x00, 0x20, 0x3c, static_cast<uint8_t>(mm ? 3 : 2), 0x00, 0x71, 0x04, slot, 0xf7};
	gm_device_send_midi(d, load, sizeof(load));
	advance(d, 44100);
	const uint8_t playRow = mm ? 0x25 : 0x22, playMask = mm ? 0x01 : 0x04;
	gm_device_send_panel(d, playRow, playMask);
	advance(d, 2048);
	gm_device_send_panel(d, playRow, 0);
	advance(d, 44100);

	if(argc > 2 && std::strcmp(argv[2], "dumps") == 0)
	{
		std::vector<gm_midi_out_event> events(4096);
		std::vector<uint8_t> bytes(4 << 20);
		io.midi_out = events.data();
		io.midi_out_capacity = static_cast<uint32_t>(events.size());
		io.midi_out_data = bytes.data();
		io.midi_out_data_capacity = static_cast<uint32_t>(bytes.size());
		const uint8_t product = mm ? 3 : 2;
		struct Request { const char* name; uint8_t command; uint8_t slot; };
		const Request requests[] = {{"global", 0x51, 0}, {"kit", 0x53, 0}, {"pattern", 0x68, slot}, {"song", 0x6a, 0},
			{"waveform (MM DigiPro)", 0x5e, 0}};
		size_t largest = 0;
		for(const auto& r : requests)
		{
			if(!mm && r.command == 0x5e)
				continue;
			const uint8_t msg[] = {0xf0, 0x00, 0x20, 0x3c, product, 0x00, r.command, r.slot, 0xf7};
			gm_device_send_midi(d, msg, sizeof(msg));
			std::vector<uint8_t> sysex;
			std::vector<size_t> sizes;
			for(int b = 0; b < 44100 * 3 / 32; ++b)
			{
				render();
				for(uint32_t e = 0; e < io.midi_out_count; ++e)
				{
					const auto* p = bytes.data() + events[e].data_offset;
					for(uint32_t i = 0; i < events[e].size; ++i)
					{
						if(p[i] == 0xf0)
							sysex.assign(1, 0xf0);
						else if(!sysex.empty())
						{
							sysex.push_back(p[i]);
							if(p[i] == 0xf7)
							{
								sizes.push_back(sysex.size());
								sysex.clear();
							}
						}
					}
				}
			}
			std::printf("%s request %02x: %zu SysEx message(s) back:", r.name, r.command, sizes.size());
			for(const auto n : sizes)
			{
				std::printf(" %zu", n);
				largest = std::max(largest, n);
			}
			std::printf(" bytes\n");
		}
		std::printf("largest: %zu bytes\n", largest);
		gm_device_destroy(d);
		return 0;
	}

	using ArmFn = void (*)(int);
	using GetFn = unsigned long long (*)();
	const auto arm = reinterpret_cast<ArmFn>(dlsym(RTLD_DEFAULT, "gm_alloccount_arm"));
	const auto allocs = reinterpret_cast<GetFn>(dlsym(RTLD_DEFAULT, "gm_alloccount_get"));

	std::vector<uint8_t> previous, state(16 << 20);
	int changedSeconds = 0;
	for(int s = 0; s <= seconds; ++s)
	{
		if(s > 0)
			for(int b = 0; b < 44100 / 32; ++b)
				render();
		size_t size = 0;
		if(arm)
			arm(1);
		const auto t0 = std::chrono::steady_clock::now();
		const auto rc = gm_device_get_state(d, state.data(), state.size(), &size);
		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
		const auto n = allocs ? allocs() : 0ull;
		if(arm)
			arm(0);
		if(rc != GM_OK)
		{
			std::printf("second %d: get_state failed: %s\n", s, gm_device_last_error(d));
			continue;
		}
		size_t diff = 0, first = SIZE_MAX, last = 0;
		if(previous.size() == size)
		{
			for(size_t i = 0; i < size; ++i)
				if(previous[i] != state[i])
				{
					++diff;
					first = std::min(first, i);
					last = i;
				}
		}
		changedSeconds += (s > 0 && diff) ? 1 : 0;
		std::printf("second %2d: state %zu bytes, get_state %lld us, allocations %llu, changed bytes %zu",
			s, size, static_cast<long long>(us), n, diff);
		if(diff)
			std::printf(" at offsets 0x%zx..0x%zx", first, last);
		std::printf("\n");
		previous.assign(state.begin(), state.begin() + static_cast<std::ptrdiff_t>(size));
	}
	std::printf("summary: the state changed in %d of %d seconds of playback\n", changedSeconds, seconds);
	gm_device_destroy(d);
	return 0;
}
