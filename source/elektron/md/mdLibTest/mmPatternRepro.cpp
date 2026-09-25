// Local repro tool (not a test): restore a saved Monomachine state, play one pattern, and
// write the stereo output as raw float32 for offline analysis.
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mmPatternRepro <device-state.bin> <out.f32> <seconds> <pattern 0-127>
//
// <device-state.bin> is md::Device::getState(StateTypeGlobal) output, i.e. the plugin's "MIDI"
// chunk payload without synthLib::Plugin's two-byte version/type prefix.
// Optional: MM_REPRO_LEVEL0="c1,c2,..." sends CC7=0 on those MIDI channels (0-15) after Play.

#include "mdLib/mddevice.h"
#include "mdLib/mdmemorymap.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace md { void busStatsReset(); }

namespace
{
	// MM_REPRO_PCWIN="start:len,start:len,...": record mixer DSP block PCs inside each cycle window.
	struct PcWindow { uint64_t start = 0, end = 0; std::map<uint32_t, uint32_t> pcs; };
	std::vector<PcWindow> g_pcWindows;
	const dsp56k::DSP* g_pcDsp = nullptr;
	dsp56k::Peripherals56303* g_pcPeriph = nullptr;
	const dsp56k::DSP* g_pcDsp2 = nullptr;
	std::map<uint32_t, uint64_t> g_vectorEntries[2];	// MM_REPRO_VECCENSUS: vector entries per DSP
	void pcTrace(const dsp56k::DSP* _dsp, const uint32_t _pc)
	{
		static const bool census = std::getenv("MM_REPRO_VECCENSUS") != nullptr;
		if(census && _pc < 0x80 && !(_pc & 1))
			++g_vectorEntries[_dsp == g_pcDsp ? 0 : 1][_pc];		// MM_REPRO_P2FROM/_P2TO (producer DSP cycles): print every producer block start PC in that window.
		static const uint64_t p2From = std::getenv("MM_REPRO_P2FROM") ? std::strtoull(std::getenv("MM_REPRO_P2FROM"), nullptr, 10) : ~0ull;
		static const uint64_t p2To = std::getenv("MM_REPRO_P2TO") ? std::strtoull(std::getenv("MM_REPRO_P2TO"), nullptr, 10) : 0;
		if(_dsp == g_pcDsp2 && _dsp->getCycles() >= p2From && _dsp->getCycles() <= p2To)
			std::printf("P2 %06x %llu\n", _pc, static_cast<unsigned long long>(_dsp->getCycles()));
		if(_dsp != g_pcDsp)
			return;
		const auto c = _dsp->getCycles();
		// MM_REPRO_PCLOG_MIN/MAX: also print every block start PC in [min,max] with its cycle.
		static const uint32_t logMin = std::getenv("MM_REPRO_PCLOG_MIN") ? static_cast<uint32_t>(std::strtoul(std::getenv("MM_REPRO_PCLOG_MIN"), nullptr, 16)) : 1;
		static const uint32_t logMax = std::getenv("MM_REPRO_PCLOG_MAX") ? static_cast<uint32_t>(std::strtoul(std::getenv("MM_REPRO_PCLOG_MAX"), nullptr, 16)) : 0;
		if(_pc >= logMin && _pc <= logMax)
			std::printf("PCHIT %06x %llu\n", _pc, static_cast<unsigned long long>(c));
		// MM_REPRO_V3PHASE=1: log voice 3's word-40 read ($e1 block with r6=$728) and clear ($143 with y:$123=$728).
		static const bool v3 = std::getenv("MM_REPRO_V3PHASE") != nullptr;
		if(v3)
		{
			if(_pc == 0xe1 && _dsp->regs().r[6].var == 0x728)
				std::printf("V3READ %llu sr=%06x mask=%u mode=%d iprc=%06x iprp=%06x\n", static_cast<unsigned long long>(c),
					_dsp->regs().sr.var, (_dsp->regs().sr.var >> 8) & 3, static_cast<int>(_dsp->getProcessingMode()),
					g_pcPeriph->read(dsp56k::XIO_IPRC, dsp56k::Nop), g_pcPeriph->read(dsp56k::XIO_IPRP, dsp56k::Nop));
			else if(_pc == 0x143 && _dsp->memory().get(dsp56k::MemArea_Y, 0x123) == 0x728)
				std::printf("V3CLEAR %llu\n", static_cast<unsigned long long>(c));
		}
		static const uint32_t probe = std::getenv("MM_REPRO_PCPROBE") ? static_cast<uint32_t>(std::strtoul(std::getenv("MM_REPRO_PCPROBE"), nullptr, 16)) : 0xffffffff;
		static const uint32_t probeOff = std::getenv("MM_REPRO_PCPROBE_OFF") ? static_cast<uint32_t>(std::strtoul(std::getenv("MM_REPRO_PCPROBE_OFF"), nullptr, 16)) : 0;
		for(size_t i = 0; i < g_pcWindows.size(); ++i)
		{
			auto& w = g_pcWindows[i];
			if(c < w.start || c >= w.end)
				continue;
			++w.pcs[_pc];
			// MM_REPRO_YWATCH=<hex addr>: report each change of that Y word, with the block that ran before.
			static const uint32_t ywatch = std::getenv("MM_REPRO_YWATCH") ? static_cast<uint32_t>(std::strtoul(std::getenv("MM_REPRO_YWATCH"), nullptr, 16)) : 0xffffffff;
			static uint32_t lastValue = 0xffffffff;
			static uint32_t lastPc = 0;
			if(ywatch != 0xffffffff)
			{
				const auto v = _dsp->memory().get(dsp56k::MemArea_Y, ywatch);
				if(v != lastValue)
					std::printf("YCHG win%zu +%llu y:%x %06x -> %06x after block %06x (now at %06x)\n", i,
						static_cast<unsigned long long>(c - w.start), ywatch, lastValue, v, lastPc, _pc);
				lastValue = v;
				lastPc = _pc;
			}
			if(_pc == probe)
			{
				const auto r6 = _dsp->regs().r[6].var;
				const auto v = _dsp->memory().get(dsp56k::MemArea_Y, (r6 + probeOff) & 0xffffff);
				std::printf("PROBE win%zu +%llu r6=%06x y:(r6+%x)=%06x\n", i, static_cast<unsigned long long>(c - w.start), r6, probeOff, v);
			}
		}
	}
}

namespace
{
	constexpr auto g_model = md::MachineModel::Monomachine;

	void require(const bool _condition, const char* _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void advance(md::Hardware& _hardware, const uint32_t _frames)
	{
		for(uint32_t done = 0; done < _frames;)
		{
			const auto chunk = std::min<uint32_t>(256, _frames - done);
			_hardware.advance(chunk);
			done += chunk;
		}
	}

	void tap(md::Hardware& _hardware, const md::PanelControl _control)
	{
		const auto packet = md::panelPacket(g_model, _control);
		require(packet.has_value(), "missing MM panel mapping");
		require(_hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
		advance(_hardware, 2048);
		require(_hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
	}
}

int main(int argc, char** argv)
{
	if(argc != 5)
	{
		std::cerr << "usage: mmPatternRepro <device-state.bin> <out.f32> <seconds> <pattern 0-127>\n";
		return 2;
	}
	const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cerr << "set GEARMULATOR_MM_FIRMWARE_BIN\n";
		return 2;
	}
	try
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), "could not read MM firmware");
		require(md::RomLoader::isRomForModel(rom, g_model), "not an MM firmware image");
		std::vector<uint8_t> state;
		require(baseLib::filesystem::readFile(state, argv[1]), "could not read device state");

		synthLib::DeviceCreateParams params;
		params.romData = std::move(rom);
		params.romName = path;
		params.customData = md::deviceCustomData(g_model);
		auto device = std::make_unique<md::Device>(params);
		require(device->setState(state, synthLib::StateTypeGlobal), "state restore failed");
		if(const char* clock = std::getenv("MM_REPRO_CLOCK"))
			require(device->setDspClockPercent(static_cast<uint32_t>(std::atoi(clock))), "DSP clock change rejected");
		auto& hardware = device->getHardware();

		advance(hardware, md::g_samplerate * 20);
		require(hardware.isAudioReady() && hardware.isFirmwareMidiReady(), "MM boot incomplete");

		// MM_REPRO_DUMP_P=<prefix>: write each DSP's P memory after boot as 24-bit big-endian words
		// (<prefix>-mixer.p, <prefix>-producer.p), the input format of dsp56kDisassemble.
		if(const char* prefix = std::getenv("MM_REPRO_DUMP_P"))
		{
			for(int which = 0; which < 2; ++which)
			{
				auto& mem = (which == 0 ? hardware.getDspMixer() : hardware.getDspProducer()).dsp().memory();
				const std::string name = std::string(prefix) + (which == 0 ? "-mixer.p" : "-producer.p");
				FILE* f = std::fopen(name.c_str(), "wb");
				require(f != nullptr, "could not open P dump");
				for(dsp56k::TWord a = 0; a < mem.sizeP(); ++a)
				{
					const auto v = mem.get(dsp56k::MemArea_P, a);
					const uint8_t bytes[3] = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
					std::fwrite(bytes, 1, 3, f);
				}
				std::fclose(f);
				std::cout << "mmPatternRepro: wrote " << name << " (" << mem.sizeP() << " words)\n";
			}
		}

		// MM_REPRO_DUMP_CS=1: print the chip-select setup the firmware programmed (MCF5206e UM 9.4.2:
		// bank n at MBAR+$64+12n CSAR (16), +$68 CSMR (32), +$6E CSCR (16); CSCR bits 13-10 = WS).
		if(std::getenv("MM_REPRO_DUMP_CS"))
		{
			auto& uc = hardware.getUC();
			for(uint32_t n = 0; n < 8; ++n)
			{
				const uint32_t base = 0x00300000 + 0x64 + 12 * n;
				const auto csar = uc.read16(base);
				const auto csmr = (static_cast<uint32_t>(uc.read16(base + 4)) << 16) | uc.read16(base + 6);
				const auto cscr = uc.read16(base + 10);
				std::printf("CS%u CSAR=%04x (base %08x) CSMR=%08x CSCR=%04x WS=%u AA=%u PS=%u BRST=%u\n", n, csar,
					static_cast<uint32_t>(csar) << 16, csmr, cscr, (cscr >> 10) & 15, (cscr >> 8) & 1, (cscr >> 6) & 3, (cscr >> 4) & 1);
			}
		}

		// MM_REPRO_DUMP_RAM=<file>: write ColdFire main RAM ($200000-$2fffff, where the OS runs) after boot,
		// as a flat big-endian image for static disassembly (load address $200000).
		if(const char* ramDump = std::getenv("MM_REPRO_DUMP_RAM"))
		{
			auto& uc = hardware.getUC();
			std::vector<uint8_t> ram;
			ram.reserve(md::memorymap::g_mainRam.size());
			for(uint32_t a = md::memorymap::g_mainRam.begin; a < md::memorymap::g_mainRam.end; a += 2)
			{
				const auto v = uc.read16(a);
				ram.push_back(static_cast<uint8_t>(v >> 8));
				ram.push_back(static_cast<uint8_t>(v));
			}
			FILE* f = std::fopen(ramDump, "wb");
			require(f != nullptr, "could not open RAM dump");
			std::fwrite(ram.data(), 1, ram.size(), f);
			std::fclose(f);
			std::cout << "mmPatternRepro: wrote " << ramDump << " (" << ram.size() << " bytes)\n";
		}

		const auto body = md::midiProtocol::selectPattern(g_model, std::atoi(argv[4]));
		synthLib::SMidiEvent select(synthLib::MidiEventSource::Host);
		select.sysex = {0xf0};
		select.sysex.insert(select.sysex.end(), body.begin(), body.end());
		select.sysex.push_back(0xf7);
		require(hardware.sendMidi(select), "pattern select rejected");
		advance(hardware, md::g_samplerate);
		tap(hardware, md::PanelControl::Play);

		if(const char* level0 = std::getenv("MM_REPRO_LEVEL0"))
		{
			std::stringstream ss(level0);
			std::string item;
			while(std::getline(ss, item, ','))
			{
				const auto ch = static_cast<uint8_t>(std::atoi(item.c_str()) & 15);
				require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
					static_cast<uint8_t>(0xb0 | ch), 7, 0)), "level CC rejected");
			}
		}

		md::busStatsReset();
		if(const char* win = std::getenv("MM_REPRO_PCWIN"))
		{
			std::stringstream ss(win);
			std::string item;
			while(std::getline(ss, item, ','))
			{
				PcWindow w;
				w.start = std::strtoull(item.c_str(), nullptr, 10);
				w.end = w.start + std::strtoull(item.c_str() + item.find(':') + 1, nullptr, 10);
				g_pcWindows.push_back(std::move(w));
			}
			g_pcDsp = &hardware.getDspMixer().dsp();
			g_pcPeriph = &hardware.getDspMixer().getPeriph();
			g_pcDsp2 = &hardware.getDspProducer().dsp();
			dsp56k::g_pcTraceHook = &pcTrace;
		}
		std::cout << "mmPatternRepro: render starts at ucCycle " << hardware.hostCurrentCycle() << '\n';
		FILE* out = std::fopen(argv[2], "wb");
		require(out != nullptr, "could not open output");
		// MM_REPRO_BLOCK: host block size (default 256).
		// MM_REPRO_FUZZ=<seed>: send random controller changes (CC 16-119, never CC7) on the
		// channels listed in MM_REPRO_FUZZ_CH at random blocks, like live knob activity.
		const uint32_t block = std::getenv("MM_REPRO_BLOCK") ? static_cast<uint32_t>(std::atoi(std::getenv("MM_REPRO_BLOCK"))) : 256;
		std::vector<uint8_t> fuzzChannels;
		if(const char* ch = std::getenv("MM_REPRO_FUZZ_CH"))
		{
			std::stringstream ss(ch);
			std::string item;
			while(std::getline(ss, item, ','))
				fuzzChannels.push_back(static_cast<uint8_t>(std::atoi(item.c_str()) & 15));
		}
		uint64_t rng = std::getenv("MM_REPRO_FUZZ") ? std::strtoull(std::getenv("MM_REPRO_FUZZ"), nullptr, 10) * 0x9E3779B97F4A7C15ull + 1 : 0;
		const auto next = [&rng]
		{
			rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
			return rng;
		};
		const auto blocks = static_cast<uint32_t>(std::atof(argv[3]) * md::g_samplerate / block);
		std::vector<float> interleaved(block * 2);
		for(uint32_t b = 0; b < blocks; ++b)
		{
			if(rng && !fuzzChannels.empty())
			{
				// About eight changes per second, in bursts like a turned knob.
				const auto r = next();
				if(r % (md::g_samplerate / block / 8 + 1) == 0)
				{
					const auto ch = fuzzChannels[next() % fuzzChannels.size()];
					auto cc = static_cast<uint8_t>(16 + next() % 104);
					if(cc == 7)
						cc = 8;
					const auto burst = 1 + next() % 6;
					for(uint64_t k = 0; k < burst; ++k)
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0xb0 | ch), cc, static_cast<uint8_t>(next() & 127))), "fuzz CC rejected");
				}
			}
			hardware.processAudio(block, 0);

			// MM_REPRO_DUMP="t1,t2,...": save the mixer DSP's X and Y memory MM_REPRO_DUMP_DELAY blocks
			// (default 2) after the block containing each time, as dump-<t>-x.bin / -y.bin (u32 words).
			if(const char* dump = std::getenv("MM_REPRO_DUMP"))
			{
				const int delay = std::getenv("MM_REPRO_DUMP_DELAY") ? std::atoi(std::getenv("MM_REPRO_DUMP_DELAY")) : 2;
				std::stringstream ss(dump);
				std::string item;
				while(std::getline(ss, item, ','))
				{
					const auto at = static_cast<uint32_t>(std::atof(item.c_str()) * md::g_samplerate / block) + static_cast<uint32_t>(delay);
					if(at != b)
						continue;
					auto& mem = hardware.getDspMixer().dsp().memory();
					for(const auto area : {dsp56k::MemArea_X, dsp56k::MemArea_Y})
					{
						const std::string name = "dump-" + item + (area == dsp56k::MemArea_X ? "-x.bin" : "-y.bin");
						FILE* f = std::fopen(name.c_str(), "wb");
						if(!f)
							continue;
						for(dsp56k::TWord a = 0; a < mem.size(area); ++a)
						{
							const uint32_t v = mem.get(area, a);
							std::fwrite(&v, sizeof(v), 1, f);
						}
						std::fclose(f);
					}
					std::cout << "mmPatternRepro: dumped mixer memory for " << item << " at block " << b << '\n';
				}
			}

			// MM_REPRO_WATCH="x:6d8,y:63e,..." with MM_REPRO_WATCH_FROM/TO (seconds): print those mixer words per block.
			if(const char* watch = std::getenv("MM_REPRO_WATCH"))
			{
				const double t = static_cast<double>(b) * block / md::g_samplerate;
				const double from = std::getenv("MM_REPRO_WATCH_FROM") ? std::atof(std::getenv("MM_REPRO_WATCH_FROM")) : 0;
				const double to = std::getenv("MM_REPRO_WATCH_TO") ? std::atof(std::getenv("MM_REPRO_WATCH_TO")) : 1e9;
				if(t >= from && t <= to)
				{
					auto& mem = hardware.getDspMixer().dsp().memory();
					std::cout << "WATCH t=" << t;
					std::stringstream ss(watch);
					std::string item;
					while(std::getline(ss, item, ','))
					{
						const auto area = item[0] == 'y' ? dsp56k::MemArea_Y : dsp56k::MemArea_X;
						const auto addr = static_cast<dsp56k::TWord>(std::strtoul(item.c_str() + 2, nullptr, 16));
						char buf[64];
						std::snprintf(buf, sizeof(buf), " %s=%06x", item.c_str(), mem.get(area, addr));
						std::cout << buf;
					}
					std::cout << '\n';
				}
			}

			const auto& outs = hardware.getAudioOutputs();
			for(uint32_t i = 0; i < block; ++i)
			{
				for(uint32_t c = 0; c < 2; ++c)
				{
					int32_t s = static_cast<int32_t>(outs[c][i]) & 0xffffff;
					if(s & 0x800000)
						s -= 0x1000000;
					interleaved[i * 2 + c] = static_cast<float>(s) / 8388608.0f;
				}
			}
			std::fwrite(interleaved.data(), sizeof(float), interleaved.size(), out);
		}
		std::fclose(out);
		for(int d = 0; d < 2; ++d)
			for(const auto& [pc, n] : g_vectorEntries[d])
				std::printf("VECTOR %s $%02x %llu\n", d == 0 ? "mixer" : "producer", pc, static_cast<unsigned long long>(n));
		for(size_t i = 0; i < g_pcWindows.size(); ++i)
			for(const auto& [pc, n] : g_pcWindows[i].pcs)
				std::printf("PCWIN %zu %06x %u\n", i, pc, n);
		std::cout << "mmPatternRepro: wrote " << blocks * 256 << " frames\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mmPatternRepro: " << _e.what() << '\n';
		return 1;
	}
}
