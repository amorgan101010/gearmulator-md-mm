#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "mdLib/mdfrontpanel.h"

namespace mdJucePlugin
{
	struct RawPanelLedBinding
	{
		const char* id;
		uint8_t bank;
		uint8_t bit;
	};

	struct BicolorPanelLedBinding
	{
		uint8_t greenBank;
		uint8_t greenBit;
		uint8_t redBank;
		uint8_t redBit;
	};

	inline constexpr std::array<RawPanelLedBinding, 20> g_monomachinePanelLeds
	{{
		{ "mmPageLed0", 0x25, 4 }, { "mmPageLed1", 0x25, 5 },
		{ "mmPageLed2", 0x25, 6 }, { "mmPageLed3", 0x25, 7 },
		{ "mmPageLed4", 0x26, 0 }, { "mmPageLed5", 0x26, 1 },
		{ "mmPageLed6", 0x26, 2 },
		{ "mmBankGroupAD", 0x26, 3 }, { "mmBankGroupEH", 0x26, 4 },
		{ "stPattern", 0x26, 5 }, { "stSong", 0x26, 6 },
		{ "mmTempoLed", 0x26, 7 },
		{ "mmRecordLed", 0x27, 0 },
		{ "mmTrigAmp", 0x27, 1 }, { "mmTrigFilter", 0x27, 2 },
		{ "mmTrigLfo", 0x27, 3 },
		{ "mmTrackPage0", 0x27, 4 }, { "mmTrackPage1", 0x27, 5 },
		{ "mmTrackPage2", 0x27, 6 }, { "mmTrackPage3", 0x27, 7 },
	}};

	inline constexpr std::array<BicolorPanelLedBinding, 6> g_monomachineTrackLeds
	{{
		{ 0x25, 0, 0x25, 1 }, { 0x25, 2, 0x25, 3 },
		{ 0x24, 0, 0x24, 1 }, { 0x24, 2, 0x24, 3 },
		{ 0x24, 4, 0x24, 5 }, { 0x24, 6, 0x24, 7 },
	}};

	template<typename PanelView>
	inline bool panelLedLit(const PanelView& _panel, const uint8_t _bank,
		const uint8_t _bit)
	{
		return _bit < 8
			&& (_panel.getLedBankRaw(_bank) & static_cast<uint8_t>(1u << _bit)) == 0;
	}

	template<typename PanelView>
	inline md::FrontPanel::LedColor monomachineTrackLedColor(
		const PanelView& _panel, const size_t _track)
	{
		if(_track >= g_monomachineTrackLeds.size())
			return md::FrontPanel::LedColor::Off;
		const auto& led = g_monomachineTrackLeds[_track];
		const bool green = panelLedLit(_panel, led.greenBank, led.greenBit);
		const bool red = panelLedLit(_panel, led.redBank, led.redBit);
		if(green && red)
			return md::FrontPanel::LedColor::Yellow;
		if(green)
			return md::FrontPanel::LedColor::Green;
		return red ? md::FrontPanel::LedColor::Red : md::FrontPanel::LedColor::Off;
	}

	template<typename PanelView>
	inline std::array<bool, 7> monomachineDataPageLeds(const PanelView& _panel)
	{
		std::array<bool, 7> result{};
		for(size_t i = 0; i < result.size(); ++i)
			result[i] = panelLedLit(_panel, g_monomachinePanelLeds[i].bank,
				g_monomachinePanelLeds[i].bit);
		return result;
	}

	template<typename PanelView>
	inline std::array<bool, 4> monomachineTrigModeLeds(const PanelView& _panel)
	{
		const bool amp = panelLedLit(_panel, 0x27, 1);
		const bool filter = panelLedLit(_panel, 0x27, 2);
		const bool lfo = panelLedLit(_panel, 0x27, 3);
		return {{ amp && !filter && !lfo, !amp && filter && !lfo,
			!amp && !filter && lfo, amp && filter && lfo }};
	}
}
