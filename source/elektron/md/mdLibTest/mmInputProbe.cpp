// Drives the Monomachine's front panel through a list of steps, capturing the LCD after each one,
// then plays white noise into its audio inputs and records what comes out. Answers "does the
// MM pass its audio input, and with which kit setup?" from the firmware itself.
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mmInputProbe <out dir> [step...]
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mmInputProbe --check    (the ctest; see check())
//
// Steps, in order:
//   kit enter exit up down left right func track1..track6 trig1..trig16  tap that key
//   hold:<key> / release:<key>      press or release a key without the other half
//   turn:<encoder 0-7>:<steps>      turn a data entry knob (negative steps turn it left)
//   wait:<seconds>                  run the machine
//   fxthru:<track>                  assign FX-THRU to track 0..5 over SysEx, data pages initialised
//   inputs:a|b|ab                   which inputs carry the noise (default ab; the other is silent)
//   block:<frames>                  host block size for noise (default 32, at most 1024)
//   noise:<seconds>                 noise into inputs A and B (the same samples, L = R), a note on
//                                   channel 1 every quarter second, recorded to <out>/noise.f32
//                                   as 7 interleaved channels: the noise, then the 6 outputs
// Every step writes <out>/NN-<step>.pbm, the LCD as it is afterwards.

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
	constexpr auto g_model = md::MachineModel::Monomachine;

	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto chunk = std::min<uint32_t>(256, _frames);
			_hardware.advance(chunk);
			_frames -= chunk;
		}
	}

	void capture(const md::Hardware& _hardware, const std::string& _path)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		auto* const file = std::fopen(_path.c_str(), "w");
		if(!file)
			return;
		std::fprintf(file, "P1\n%u %u\n", md::FrontPanel::g_lcdWidth, md::FrontPanel::g_lcdHeight);
		for(uint32_t y = 0; y < md::FrontPanel::g_lcdHeight; ++y)
		{
			for(uint32_t x = 0; x < md::FrontPanel::g_lcdWidth; ++x)
				std::fputc(panel.getLcdPixel(x, y) ? '1' : '0', file);
			std::fputc('\n', file);
		}
		std::fclose(file);
	}

	const std::map<std::string, md::PanelControl>& keys()
	{
		static const std::map<std::string, md::PanelControl> k = []
		{
			std::map<std::string, md::PanelControl> m = {
				{"kit", md::PanelControl::Kit}, {"enter", md::PanelControl::Enter},
				{"exit", md::PanelControl::Exit}, {"up", md::PanelControl::Up},
				{"down", md::PanelControl::Down}, {"left", md::PanelControl::Left},
				{"right", md::PanelControl::Right}, {"func", md::PanelControl::Function},
				{"ser", md::PanelControl::SynthesisEffectsRouting},
			};
			for(int i = 0; i < 6; ++i)
				m["track" + std::to_string(i + 1)] = static_cast<md::PanelControl>(static_cast<int>(md::PanelControl::Track1) + i);
			for(int i = 0; i < 16; ++i)
				m["trig" + std::to_string(i + 1)] = static_cast<md::PanelControl>(static_cast<int>(md::PanelControl::Trigger1) + i);
			return m;
		}();
		return k;
	}

	bool key(md::Hardware& _hardware, const std::string& _name, const bool _down)
	{
		const auto it = keys().find(_name);
		if(it == keys().end())
			return false;
		const auto packet = md::panelPacket(g_model, it->second);
		if(!packet)
			return false;
		_hardware.trySendPanelEvent(packet->row, _down ? packet->mask : 0);
		advance(_hardware, _down ? 2048 : md::g_samplerate / 4);
		return true;
	}

	uint32_t g_block = 32;
	bool g_inputA = true;
	bool g_inputB = true;

	// Noise into the inputs for _seconds in _block-frame host blocks, a note on channel 1 every
	// quarter second; 7 interleaved channels: the noise, then the 6 outputs.
	std::vector<float> record(md::Hardware& _hardware, const double _seconds, const uint32_t _block)
	{
		const uint32_t block = _block;
		std::array<std::vector<float>, 6> buffers;
		synthLib::TAudioOutputs outputs{};
		for(size_t ch = 0; ch < buffers.size(); ++ch)
		{
			buffers[ch].assign(block, 0.0f);
			outputs[ch] = buffers[ch].data();
		}
		std::vector<float> in(block);
		const std::vector<float> silence(block, 0.0f);
		synthLib::TAudioInputs inputs{};
		inputs[0] = g_inputA ? in.data() : silence.data();
		inputs[1] = g_inputB ? in.data() : silence.data();
		uint64_t lcg = 1;
		const auto blocks = static_cast<uint32_t>(_seconds * md::g_samplerate / block);
		std::vector<float> recorded;
		recorded.reserve(static_cast<size_t>(blocks) * block * 7);
		// A note every quarter second (11025 frames, rounded down to a block), at a block start.
		const uint32_t retrig = std::max<uint32_t>(1, 11025 / block);
		for(uint32_t b = 0; b < blocks; ++b)
		{
			if(b % retrig == 0)
				_hardware.sendMidi({synthLib::MidiEventSource::Host, 0x90, 60, 100});
			for(auto& x : in)
			{
				lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
				x = (static_cast<float>(lcg >> 40) / static_cast<float>(1u << 24) - 0.5f) * 0.5f;
			}
			_hardware.processAudio(inputs, outputs, block, 0);
			for(uint32_t i = 0; i < block; ++i)
			{
				recorded.push_back(in[i]);
				for(const auto& ch : buffers)
					recorded.push_back(ch[i]);
			}
		}
		return recorded;
	}

	int noise(md::Hardware& _hardware, const double _seconds, const std::string& _path)
	{
		const auto recorded = record(_hardware, _seconds, g_block);
		auto* const file = std::fopen(_path.c_str(), "wb");
		if(!file)
			return 1;
		std::fwrite(recorded.data(), sizeof(float), recorded.size(), file);
		std::fclose(file);
		std::fprintf(stderr, "noise: %zu frames, underflow %llu overflow %llu\n", recorded.size() / 7,
			static_cast<unsigned long long>(_hardware.hostAudioInputUnderflowCount()),
			static_cast<unsigned long long>(_hardware.hostAudioInputOverflowCount()));
		return 0;
	}

	// Main L (output 0) against the noise: the lag 0..1023 with the largest |normalised
	// correlation|, its score, and main L's RMS.
	struct Match
	{
		int lag = -1;
		double score = 0;
		double rms = 0;
	};

	Match match(const std::vector<float>& _recorded)
	{
		const size_t n = _recorded.size() / 7;
		const auto noiseAt = [&](const size_t _i) { return static_cast<double>(_recorded[_i * 7]); };
		const auto outAt = [&](const size_t _i) { return static_cast<double>(_recorded[_i * 7 + 1]); };
		double ex = 0;
		double ey = 0;
		for(size_t i = 0; i < n; ++i)
		{
			ex += noiseAt(i) * noiseAt(i);
			ey += outAt(i) * outAt(i);
		}
		Match m;
		m.rms = std::sqrt(ey / static_cast<double>(n));
		if(ey == 0)
			return m;
		const double norm = std::sqrt(ex * ey);
		for(size_t lag = 0; lag < 1024 && lag < n; ++lag)
		{
			double acc = 0;
			for(size_t i = lag; i < n; ++i)
				acc += outAt(i) * noiseAt(i - lag);
			if(std::abs(acc / norm) > std::abs(m.score))
			{
				m.score = acc / norm;
				m.lag = static_cast<int>(lag);
			}
		}
		return m;
	}

	// The regression test (ctest mmAudioInputFirmwareTest): boots three units, each with FX-THRU
	// on track 1. On its default INPUT (NEIBOR) it must stay exactly silent with noise coming in;
	// with INPUT set to INP AB on the panel, main L must follow the noise 140 frames late (with
	// inverted polarity), in 32- and in 512-frame blocks, and two identical runs must give
	// identical audio.
	// The 512-frame unit is routed before any audio, as a host does it. Rendering in 512-frame
	// blocks, then running the panel steps through advance(), then rendering again moves the
	// delay to 141 frames about half a second later (no input queue underflow or overflow); in
	// 32-frame blocks it stays at 140. The studio engine renders 32-frame sub-blocks and never
	// advances once running.
	int check(const std::vector<uint8_t>& _rom, const char* _path)
	{
		struct Run
		{
			Match neighbor;
			std::vector<float> routed;
		};
		const auto run = [&](const uint32_t _block, const bool _neighborFirst)
		{
			auto hardware = std::make_unique<md::Hardware>(_rom, _path, g_model);
			advance(*hardware, md::g_samplerate * 20);
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, 0x00, 12, 0x01, 0xf7};
			hardware->sendMidi(e);
			advance(*hardware, md::g_samplerate);
			Run r;
			if(_neighborFirst)
				r.neighbor = match(record(*hardware, 1.0, _block));
			for(const char* k : {"kit", "down", "enter", "right", "right", "right", "right", "right", "up", "up", "up", "enter"})
				key(*hardware, k, true) && key(*hardware, k, false);
			r.routed = record(*hardware, 2.0, _block);
			return r;
		};
		int failures = 0;
		const auto require = [&](const bool _ok, const char* _what)
		{
			std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what);
			failures += _ok ? 0 : 1;
		};
		const auto a = run(32, true);
		const auto b = run(32, true);
		const auto c = run(512, false);
		const auto ma = match(a.routed);
		const auto mc = match(c.routed);
		std::printf("NEIBOR: main L rms %g; INP AB: lag %d score %+.3f (32-frame blocks), lag %d score %+.3f (512)\n",
			a.neighbor.rms, ma.lag, ma.score, mc.lag, mc.score);
		require(a.neighbor.rms == 0 && b.neighbor.rms == 0, "FX-THRU on NEIBOR passes nothing from the inputs");
		require(ma.lag == 140 && ma.score < -0.3, "FX-THRU on INP AB: main L follows the inputs 140 frames late, inverted");
		require(mc.lag == 140 && mc.score < -0.3, "the same in 512-frame blocks");
		require(a.routed == b.routed, "two identical runs give identical audio");
		std::printf("mmAudioInputFirmwareTest: %s\n", failures ? "FAILED" : "PASS");
		return failures ? 1 : 0;
	}
}

int main(int argc, char** argv)
{
	const auto* const path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(argc >= 2 && std::string(argv[1]) == "--check")
	{
		std::vector<uint8_t> rom;
		if(!path || !baseLib::filesystem::readFile(rom, path) || !md::RomLoader::isRomForModel(rom, g_model))
		{
			std::cerr << "mmAudioInputFirmwareTest: no Monomachine firmware (GEARMULATOR_MM_FIRMWARE_BIN); skipped\n";
			return 77;
		}
		return check(rom, path);
	}
	if(argc < 2 || !path)
	{
		std::cerr << "usage: GEARMULATOR_MM_FIRMWARE_BIN=... mmInputProbe <out dir> [step...]\n";
		return 2;
	}
	const std::string out = argv[1];
	std::vector<uint8_t> rom;
	if(!baseLib::filesystem::readFile(rom, path) || !md::RomLoader::isRomForModel(rom, g_model))
	{
		std::cerr << "firmware missing or not a Monomachine image\n";
		return 2;
	}
	auto hardware = std::make_unique<md::Hardware>(rom, path, g_model);
	advance(*hardware, md::g_samplerate * 20);
	if(!hardware->isAudioReady())
	{
		std::cerr << "boot incomplete\n";
		return 1;
	}
	char name[16];
	capture(*hardware, out + "/00-boot.pbm");
	for(int i = 2; i < argc; ++i)
	{
		const std::string step = argv[i];
		const auto colon = step.find(':');
		const auto verb = step.substr(0, colon);
		const auto arg = colon == std::string::npos ? std::string() : step.substr(colon + 1);
		bool ok = true;
		if(verb == "wait")
			advance(*hardware, static_cast<uint32_t>(std::atof(arg.c_str()) * md::g_samplerate));
		else if(verb == "hold" || verb == "release")
			ok = key(*hardware, arg, verb == "hold");
		else if(verb == "turn")
		{
			const auto c2 = arg.find(':');
			const auto encoder = std::atoi(arg.substr(0, c2).c_str());
			const auto steps = c2 == std::string::npos ? 1 : std::atoi(arg.substr(c2 + 1).c_str());
			const auto command = md::panelEncoderCommand(g_model, static_cast<md::PanelEncoder>(encoder));
			ok = command.has_value();
			for(int s = 0; ok && s < std::abs(steps); ++s)
			{
				hardware->trySendPanelEvent(*command, steps > 0 ? 0x01 : 0xff);
				advance(*hardware, 1024);
			}
			advance(*hardware, md::g_samplerate / 8);
		}
		else if(verb == "fxthru")
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, static_cast<uint8_t>(std::atoi(arg.c_str())), 12, 0x01, 0xf7};
			hardware->sendMidi(e);
			advance(*hardware, md::g_samplerate);
		}
		else if(verb == "inputs")
		{
			g_inputA = arg.find('a') != std::string::npos;
			g_inputB = arg.find('b') != std::string::npos;
			ok = g_inputA || g_inputB;
		}
		else if(verb == "block")
		{
			g_block = static_cast<uint32_t>(std::atoi(arg.c_str()));
			ok = g_block > 0 && g_block <= 1024;
		}
		else if(verb == "noise")
		{
			if(noise(*hardware, std::atof(arg.c_str()), out + "/noise.f32"))
				return 1;
		}
		else
			ok = key(*hardware, step, true) && key(*hardware, step, false);
		if(!ok)
		{
			std::cerr << "unknown step " << step << '\n';
			return 2;
		}
		std::snprintf(name, sizeof(name), "%02d-", i - 1);
		std::string file = step;
		for(auto& c : file)
			if(c == ':' || c == '/')
				c = '_';
		capture(*hardware, out + "/" + name + file + ".pbm");
	}
	return 0;
}
