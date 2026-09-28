// Quick taps in GRID RECORD: does the firmware register a trig key whose press and release reach
// it at the same instant (a fast click, a tablet tap, a MIDI pad), or a chord of them? Each case
// toggles steps and reads the step LEDs back.
//
//   panelQuickTapFirmwareTest md|mm
#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	void require(bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}
	void advance(md::Hardware& hardware, uint32_t frames)
	{
		while(frames)
		{
			const auto n = std::min(frames, 256u);
			hardware.advance(n);
			frames -= n;
		}
	}
	md::PanelControl trigger(unsigned step)
	{
		return static_cast<md::PanelControl>(static_cast<unsigned>(md::PanelControl::Trigger1) + step);
	}
}

int main(int argc, char** argv)
{
	if(argc < 2) return 1;
	const auto model = std::string_view(argv[1]) == "md"
		? md::MachineModel::Machinedrum : md::MachineModel::Monomachine;
	const char* path = std::getenv(model == md::MachineModel::Machinedrum
		? "GEARMULATOR_MD_FIRMWARE_BIN" : "GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path) { std::cout << "SKIP: firmware not supplied\n"; return 77; }
	try
	{
		synthLib::DeviceCreateParams params;
		require(baseLib::filesystem::readFile(params.romData, path), "cannot read firmware");
		require(md::RomLoader::isRomForModel(params.romData, model), "wrong firmware model");
		params.romName = path;
		params.customData = md::deviceCustomData(model);
		// No homePath: never load or overwrite the user's machine storage.
		auto device = std::make_unique<md::Device>(params);
		auto& hardware = device->getHardware();
		advance(hardware, md::g_samplerate * 25);
		require(hardware.isAudioReady() && hardware.isFirmwareMidiReady(), "boot incomplete");

		const auto lit = [&](unsigned step)
		{
			const auto panel = hardware.getFrontPanelSnapshot();
			return model == md::MachineModel::Machinedrum
				? panel.getStepLed(step)
				: panel.getMonomachineStepLedColor(step) != md::FrontPanel::LedColor::Off;
		};
		const auto steps = [&]
		{
			std::string s;
			for(unsigned i = 0; i < 16; ++i) s += lit(i) ? '#' : '.';
			return s;
		};
		// Press every step, then release every step, with `hold` frames after each packet.
		md::PanelRowState rows;
		const auto chord = [&](const std::vector<unsigned>& which, uint32_t hold)
		{
			for(const auto step : which)
			{
				const auto p = rows.press(*md::panelPacket(model, trigger(step)));
				require(hardware.trySendPanelEvent(p.row, p.mask), "press rejected");
				if(hold) advance(hardware, hold);
			}
			for(const auto step : which)
			{
				const auto p = rows.release(*md::panelPacket(model, trigger(step)));
				require(hardware.trySendPanelEvent(p.row, p.mask), "release rejected");
				if(hold) advance(hardware, hold);
			}
			advance(hardware, md::g_samplerate / 2);
		};
		const auto tap = [&](md::PanelControl control)
		{
			const auto packet = *md::panelPacket(model, control);
			const auto p = rows.press(packet);
			require(hardware.trySendPanelEvent(p.row, p.mask), "press rejected");
			advance(hardware, md::g_samplerate / 8);
			const auto r = rows.release(packet);
			require(hardware.trySendPanelEvent(r.row, r.mask), "release rejected");
			advance(hardware, md::g_samplerate / 2);
		};
		tap(md::PanelControl::Exit);
		tap(md::PanelControl::Record);             // GRID RECORD
		std::cout << "grid:     " << steps() << '\n';

		int failures = 0;
		const auto check = [&](const char* what, const std::vector<unsigned>& which, uint32_t hold)
		{
			std::array<bool, 16> before{};
			for(unsigned i = 0; i < 16; ++i) before[i] = lit(i);
			chord(which, hold);
			std::cout << what << ' ' << steps();
			bool ok = true;
			for(unsigned i = 0; i < 16; ++i)
			{
				const bool want = std::find(which.begin(), which.end(), i) != which.end() ? !before[i] : before[i];
				ok &= lit(i) == want;
			}
			std::cout << (ok ? "  ok\n" : "  WRONG\n");
			failures += ok ? 0 : 1;
		};
		for(int round = 0; round < 2; ++round)    // on, then back off
		{
			check("held:    ", {1}, 2048);
			for(unsigned step : {2u, 3u, 5u, 6u})
				check("0-hold:  ", {step}, 0);
			check("chord 0: ", {9, 10, 11}, 0);
			check("chord 1: ", {13, 14}, 64);
		}
		std::cout << (failures ? "FAIL: " : "PASS: ") << failures << " wrong\n";
		return failures ? 1 : 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
