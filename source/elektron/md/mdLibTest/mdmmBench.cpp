// Headless MD/MM render benchmark (local, not upstream).
//
// Boots real firmware, then times Hardware::processAudio in host-sized blocks,
// the same entry point the plug-in's Device::processAudio uses. Reports the
// per-block cost as a fraction of the realtime budget so results line up with
// the plug-in's performance-capture histogram.
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mdmmBench mm [seconds] [block]
//   GEARMULATOR_MD_FIRMWARE_BIN=/path/md.bin mdmmBench md [seconds] [block] [pattern:A01]
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mdmmBench mm [seconds] [block] [pattern:A01]

#include "mdLib/mdhardware.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdpanel.h"
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

// Present only in -fprofile-generate builds; null otherwise.
#if defined(__GNUC__) && !defined(__clang__)
extern "C" void __gcov_reset() __attribute__((weak));
static void resetPgoCounters() { if(__gcov_reset) __gcov_reset(); }
#else
static void resetPgoCounters() {}
#endif

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
	// User-mode hardware counter for this thread, covering the measured window only. Unlike wall
	// time it does not tick while the bench is descheduled, and cycles do not depend on clock
	// speed, so other programs and boost/thermal changes mostly drop out. Reads 0 if unavailable.
	class HwCounter
	{
	public:
		explicit HwCounter(const uint64_t _config)
		{
			(void)_config;
#if defined(__linux__)
			perf_event_attr attr{};
			attr.type = PERF_TYPE_HARDWARE;
			attr.size = sizeof(attr);
			attr.config = _config;
			attr.disabled = 1;
			attr.exclude_kernel = 1;
			attr.exclude_hv = 1;
			m_fd = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
#endif
		}
		~HwCounter()
		{
#if defined(__linux__)
			if(m_fd >= 0)
				close(m_fd);
#endif
		}
		HwCounter(const HwCounter&) = delete;
		HwCounter& operator=(const HwCounter&) = delete;

		void start() const
		{
#if defined(__linux__)
			if(m_fd < 0)
				return;
			ioctl(m_fd, PERF_EVENT_IOC_RESET, 0);
			ioctl(m_fd, PERF_EVENT_IOC_ENABLE, 0);
#endif
		}
		uint64_t stop() const
		{
			uint64_t value = 0;
#if defined(__linux__)
			if(m_fd < 0)
				return 0;
			ioctl(m_fd, PERF_EVENT_IOC_DISABLE, 0);
			if(read(m_fd, &value, sizeof(value)) != static_cast<ssize_t>(sizeof(value)))
				value = 0;
#endif
			return value;
		}
	private:
		int m_fd = -1;
	};

	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto chunk = std::min<uint32_t>(256, _frames);
			_hardware.advance(chunk);
			_frames -= chunk;
		}
	}

	bool tap(md::Hardware& _hardware, const md::MachineModel _model, const md::PanelControl _control)
	{
		const auto packet = md::panelPacket(_model, _control);
		if(!packet)
			return false;
		_hardware.sendPanelEvent(packet->row, packet->mask);
		advance(_hardware, 2048);
		_hardware.sendPanelEvent(packet->row, 0);
		advance(_hardware, 4096);
		return true;
	}

	// Factory banks A--D contain slots 0--63. Accept both the firmware slot
	// number and the panel spelling (for example pattern:0 or pattern:C07).
	int factoryPatternSlot(const std::string_view _mode)
	{
		constexpr std::string_view prefix = "pattern:";
		if(_mode.size() < prefix.size() || _mode.substr(0, prefix.size()) != prefix)
			return -1;
		const auto value = _mode.substr(prefix.size());
		if(value.size() == 3 && value[0] >= 'A' && value[0] <= 'D'
			&& value[1] >= '0' && value[1] <= '1' && value[2] >= '0' && value[2] <= '9')
		{
			const auto withinBank = static_cast<int>((value[1] - '0') * 10 + value[2] - '0');
			return withinBank >= 1 && withinBank <= 16 ? (value[0] - 'A') * 16 + withinBank - 1 : -2;
		}
		if(value.empty())
			return -2;
		int slot = 0;
		for(const char c : value)
		{
			if(c < '0' || c > '9')
				return -2;
			slot = slot * 10 + c - '0';
		}
		return slot < 64 ? slot : -2;
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
	const auto patternSlot = factoryPatternSlot(mode);
	if(patternSlot == -2)
	{
		std::cerr << "factory pattern must be A01 through D16, or slot 0 through 63\n";
		return 2;
	}
	const bool sine = mm && (mode == "sine" || mode.rfind("machine:", 0) == 0);
	const auto machine = static_cast<uint8_t>(mode.rfind("machine:", 0) == 0
		? std::atoi(std::string(mode.substr(8)).c_str()) : 1);
	// Optional voice count: how many tracks get the machine and a note. Poly mode uses six.
	const auto voices = static_cast<uint8_t>(argc >= 6 ? std::max(1, std::min(6, std::atoi(argv[5]))) : 6);
	if(sine)
	{
		for(uint8_t track = 0; track < voices; ++track)
		{
			synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
			assign.sysex = {0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, track, machine, 1, 0xf7};
			hardware->sendMidi(assign);
			advance(*hardware, md::g_samplerate);
		}
	}
	if(patternSlot >= 0)
	{
		const auto body = md::midiProtocol::selectPattern(model, patternSlot);
		synthLib::SMidiEvent select(synthLib::MidiEventSource::Host);
		select.sysex = {0xf0};
		select.sysex.insert(select.sysex.end(), body.begin(), body.end());
		select.sysex.push_back(0xf7);
		if(!hardware->sendMidi(select) || !tap(*hardware, model, md::PanelControl::Play))
		{
			std::cerr << "could not select or start factory pattern\n";
			return 1;
		}
		advance(*hardware, md::g_samplerate);
	}

	// PGO training: boot and setup above are most of the run, so without this the profile mostly
	// describes boot. Drop those counts and keep only playback (JIT warm-up included).
	if(std::getenv("MDMM_BENCH_PGO_PLAYBACK_ONLY"))
		resetPgoCounters();

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

#if defined(__linux__)
	const HwCounter cycleCounter(PERF_COUNT_HW_CPU_CYCLES);
	const HwCounter instructionCounter(PERF_COUNT_HW_INSTRUCTIONS);
#else
	const HwCounter cycleCounter(0);
	const HwCounter instructionCounter(0);
#endif
	cycleCounter.start();
	instructionCounter.start();
	const auto start = clock::now();
	for(uint32_t i = 0; i < blocks; ++i)
	{
		if(sine && i % retriggerBlocks == 0)
		{
			const uint8_t note = static_cast<uint8_t>(48 + (i / retriggerBlocks) % 24);
			for(uint8_t ch = 0; ch < voices; ++ch)
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
	// Includes the hashing loop above, which is identical across builds of the same output.
	const auto cycles = cycleCounter.stop();
	const auto instructions = instructionCounter.stop();

	// Worst blocks with their position in the run: a spike at a fixed time is warm-up or JIT
	// compilation, spikes spread through the run are something recurring.
	{
		std::vector<std::pair<double, uint32_t>> worst;
		worst.reserve(load.size());
		for(uint32_t i = 0; i < load.size(); ++i)
			worst.emplace_back(load[i], i);
		std::partial_sort(worst.begin(), worst.begin() + std::min<size_t>(6, worst.size()), worst.end(),
			[](const auto& _a, const auto& _b) { return _a.first > _b.first; });
		std::cout << "worst blocks:";
		for(size_t i = 0; i < std::min<size_t>(6, worst.size()); ++i)
			std::cout << " " << std::fixed << std::setprecision(2) << worst[i].first
				<< "@" << std::setprecision(2) << (double(worst[i].second) * block / md::g_samplerate) << "s";
		std::cout << '\n';
	}
	std::sort(load.begin(), load.end());
	const auto pct = [&](double _q) { return load[static_cast<size_t>(_q * (load.size() - 1))]; };
	const auto over = std::count_if(load.begin(), load.end(), [](double _l) { return _l > 1.0; });

	std::cout << std::fixed << std::setprecision(3)
		<< (mm ? "MM" : "MD") << " block=" << block << " blocks=" << blocks
		<< " mode=" << (mode.empty() ? "idle" : mode)
		<< " mean=" << (wallNs / blocks / budgetNs)
		<< " p50=" << pct(0.5) << " p90=" << pct(0.9) << " p99=" << pct(0.99)
		<< " max=" << load.back()
		<< " over=" << std::setprecision(1) << (100.0 * over / blocks) << "%"
		<< " realtimeX=" << std::setprecision(3) << (1e9 * seconds / wallNs)
		<< " rms=" << std::scientific << std::sqrt(energy / (blocks * block * buffers.size()))
		<< " hash=" << std::hex << hash << std::dec
		// Per emulated second of playback. 0 when hardware counters are unavailable.
		<< " Mcycles/s=" << std::fixed << std::setprecision(2) << (double(cycles) / seconds / 1e6)
		<< " Minstr/s=" << (double(instructions) / seconds / 1e6) << '\n';
	return 0;
}
