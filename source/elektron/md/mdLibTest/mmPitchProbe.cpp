// Records one note from one Monomachine machine and writes the audio as raw floats, so the
// pitch can be tracked over time. Answers questions like "does this machine bend by itself?".
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mmPitchProbe <machine id> <out.f32> [note] [seconds] [hold]
//
// The machine is assigned to track 1 with init=1, so every page starts from its default.

#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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
	const auto* const path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(argc < 3 || !path)
	{
		std::cerr << "usage: GEARMULATOR_MM_FIRMWARE_BIN=... mmPitchProbe <machine> <out.f32> [note] [seconds]\n";
		return 2;
	}
	const auto machine = static_cast<uint8_t>(std::atoi(argv[1]));
	const std::string outPath = argv[2];
	const auto note = static_cast<uint8_t>(argc >= 4 ? std::atoi(argv[3]) : 60);
	const auto seconds = argc >= 5 ? std::atof(argv[4]) : 3.0;
	const auto hold = argc >= 6 ? std::atof(argv[5]) : seconds / 2.0;

	std::vector<uint8_t> rom;
	if(!baseLib::filesystem::readFile(rom, path)
		|| !md::RomLoader::isRomForModel(rom, md::MachineModel::Monomachine))
	{
		std::cerr << "firmware missing or not a Monomachine image\n";
		return 2;
	}

	auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Monomachine);
	advance(*hardware, md::g_samplerate * 20);
	if(!hardware->isAudioReady())
	{
		std::cerr << "boot incomplete\n";
		return 1;
	}

	// Assign the machine to track 1, initialising all of its data pages.
	synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
	assign.sysex = { 0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, 0x00, machine, 0x01, 0xf7 };
	hardware->sendMidi(assign);
	advance(*hardware, md::g_samplerate);

	constexpr uint32_t block = 128;
	std::array<std::vector<float>, 6> buffers;
	synthLib::TAudioOutputs outputs{};
	for(size_t ch = 0; ch < buffers.size() && ch < outputs.size(); ++ch)
	{
		buffers[ch].resize(block);
		outputs[ch] = buffers[ch].data();
	}

	for(uint32_t i = 0; i < md::g_samplerate / block; ++i)		// settle, and warm the JIT
		hardware->processAudio(outputs, block, 0);

	hardware->sendMidi({synthLib::MidiEventSource::Host, 0x90, note, 100});

	const auto blocks = static_cast<uint32_t>(seconds * md::g_samplerate / block);
	const auto releaseBlock = static_cast<uint32_t>(hold * md::g_samplerate / block);
	std::vector<float> recorded;
	recorded.reserve(blocks * block);
	for(uint32_t i = 0; i < blocks; ++i)
	{
		if(i == releaseBlock)
			hardware->sendMidi({synthLib::MidiEventSource::Host, 0x80, note, 0});
		hardware->processAudio(outputs, block, 0);
		recorded.insert(recorded.end(), buffers[0].begin(), buffers[0].end());
	}

	auto* const file = std::fopen(outPath.c_str(), "wb");
	if(!file)
	{
		std::cerr << "cannot write " << outPath << '\n';
		return 1;
	}
	std::fwrite(recorded.data(), sizeof(float), recorded.size(), file);
	std::fclose(file);
	std::fprintf(stderr, "machine %u, note %u: %zu samples, note off at %.2fs\n",
		machine, note, recorded.size(), double(releaseBlock * block) / md::g_samplerate);
	return 0;
}
