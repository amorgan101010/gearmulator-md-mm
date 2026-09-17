// Headless MD/MM render benchmark (local, not upstream).
//
// Boots real firmware, then times Hardware::processAudio in host-sized blocks,
// the same entry point the plug-in's Device::processAudio uses. Reports the
// per-block cost as a fraction of the realtime budget so results line up with
// the plug-in's performance-capture histogram.
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mdmmBench mm [seconds] [block]
//   GEARMULATOR_MD_FIRMWARE_BIN=/path/md.bin mdmmBench md [seconds] [block]

#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto chunk = std::min<uint32_t>(256, _frames);
			_hardware.advance(chunk);
			_frames -= chunk;
		}
	}
}

int main(int argc, char** argv)
{
	const bool mm = argc < 2 || std::string_view(argv[1]) != "md";
	const double seconds = argc >= 3 ? std::atof(argv[2]) : 30.0;
	const uint32_t block = argc >= 4 ? static_cast<uint32_t>(std::atoi(argv[3])) : 128;

	const auto* path = std::getenv(mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cerr << "set " << (mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN") << '\n';
		return 2;
	}

	std::vector<uint8_t> rom;
	const auto model = mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
	if(!baseLib::filesystem::readFile(rom, path) || !md::RomLoader::isRomForModel(rom, model))
	{
		std::cerr << "firmware missing or wrong model: " << path << '\n';
		return 2;
	}

	auto hardware = std::make_unique<md::Hardware>(rom, path, model);

	// Boot exactly like mmAudioFirmwareTest, then warm the JIT on the host path.
	advance(*hardware, md::g_samplerate * 20);
	if(!hardware->isAudioReady())
	{
		std::cerr << "boot incomplete\n";
		return 1;
	}

	std::array<std::vector<float>, 6> buffers;
	synthLib::TAudioOutputs outputs{};
	for(size_t ch = 0; ch < buffers.size() && ch < outputs.size(); ++ch)
	{
		buffers[ch].resize(block);
		outputs[ch] = buffers[ch].data();
	}

	// "sine": assign GND-SIN (machine 01, init=1) to all six tracks, as
	// mmAudioFirmwareTest --sine-midi does, and retrigger notes during the run so
	// the output fingerprint covers real audio rather than silence.
	// "machine:<id>" does the same with any machine, which measures what poly mode costs:
	// six engines playing one sound. Chords play a note per track, as poly mode does.
	const std::string_view mode = argc >= 5 ? std::string_view(argv[4]) : std::string_view();
	const bool sine = mm && (mode == "sine" || mode.rfind("machine:", 0) == 0);
	const auto machine = static_cast<uint8_t>(mode.rfind("machine:", 0) == 0
		? std::atoi(std::string(mode.substr(8)).c_str()) : 1);
	if(sine)
	{
		for(uint8_t track = 0; track < 6; ++track)
		{
			synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
			assign.sysex = {0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, track, machine, 1, 0xf7};
			hardware->sendMidi(assign);
			advance(*hardware, md::g_samplerate);
		}
	}

	const auto warmupBlocks = md::g_samplerate * 2 / block;
	for(uint32_t i = 0; i < warmupBlocks; ++i)
		hardware->processAudio(outputs, block, 0);

	using clock = std::chrono::steady_clock;
	const auto blocks = static_cast<uint32_t>(seconds * md::g_samplerate / block);
	const double budgetNs = 1e9 * block / md::g_samplerate;
	std::vector<double> load;
	load.reserve(blocks);

	// FNV-1a over every output sample: identical hashes across builds mean the
	// change did not alter emulated behaviour for this run.
	uint64_t hash = 1469598103934665603ull;
	double energy = 0;
	const auto retriggerBlocks = std::max<uint32_t>(1, md::g_samplerate / 2 / block);

	const auto start = clock::now();
	for(uint32_t i = 0; i < blocks; ++i)
	{
		if(sine && i % retriggerBlocks == 0)
		{
			const uint8_t note = static_cast<uint8_t>(48 + (i / retriggerBlocks) % 24);
			for(uint8_t ch = 0; ch < 6; ++ch)
				hardware->sendMidi({synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x90 | ch), note, 100});
		}
		const auto t0 = clock::now();
		hardware->processAudio(outputs, block, 0);
		const auto t1 = clock::now();
		load.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / budgetNs);

		for(const auto& buffer : buffers)
		{
			for(const float sample : buffer)
			{
				energy += static_cast<double>(sample) * sample;
				uint32_t bits = 0;
				std::memcpy(&bits, &sample, sizeof(bits));
				for(int b = 0; b < 4; ++b)
				{
					hash ^= (bits >> (8 * b)) & 0xff;
					hash *= 1099511628211ull;
				}
			}
		}
	}
	const double wallNs = std::chrono::duration<double, std::nano>(clock::now() - start).count();

	std::sort(load.begin(), load.end());
	const auto pct = [&](double _q) { return load[static_cast<size_t>(_q * (load.size() - 1))]; };
	const auto over = std::count_if(load.begin(), load.end(), [](double _l) { return _l > 1.0; });

	std::cout << std::fixed << std::setprecision(3)
		<< (mm ? "MM" : "MD") << " block=" << block << " blocks=" << blocks
		<< " mean=" << (wallNs / blocks / budgetNs)
		<< " p50=" << pct(0.5) << " p90=" << pct(0.9) << " p99=" << pct(0.99)
		<< " max=" << load.back()
		<< " over=" << std::setprecision(1) << (100.0 * over / blocks) << "%"
		<< " realtimeX=" << std::setprecision(3) << (1e9 * seconds / wallNs)
		<< " rms=" << std::scientific << std::sqrt(energy / (blocks * block * buffers.size()))
		<< " hash=" << std::hex << hash << '\n';
	return 0;
}
