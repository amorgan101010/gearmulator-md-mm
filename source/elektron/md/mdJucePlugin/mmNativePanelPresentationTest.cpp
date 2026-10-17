#include "mdNativePanelPresentation.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <iostream>

int main()
{
	std::array<uint8_t, 1024> lcd{};
	std::array<uint8_t, md::FrontPanel::g_ledBankCount> banks{};
	banks.fill(0xff);
	const auto light = [&banks](const uint8_t bank, const uint8_t bit)
	{
		banks[bank - md::FrontPanel::g_firstLedBank]
			&= static_cast<uint8_t>(~(1u << bit));
	};

	// MM trigger 1 is yellow, trigger 2 is red.
	light(0x20, 0);
	light(0x20, 1);
	light(0x20, 3);
	// Track 1 is green; track 5 is yellow.
	light(0x25, 0);
	light(0x24, 4);
	light(0x24, 5);
	// Page 3 and LFO trigger mode are selected.
	light(0x25, 6);
	light(0x27, 3);

	md::FrontPanel panel;
	panel.setRaw(lcd.data(), banks.data());
	assert(panel.getMonomachineStepLedColor(0) == md::FrontPanel::LedColor::Yellow);
	assert(panel.getMonomachineStepLedColor(1) == md::FrontPanel::LedColor::Red);
	assert(mdJucePlugin::monomachineTrackLedColor(panel, 0)
		== md::FrontPanel::LedColor::Green);
	assert(mdJucePlugin::monomachineTrackLedColor(panel, 4)
		== md::FrontPanel::LedColor::Yellow);
	assert(mdJucePlugin::monomachineDataPageLeds(panel)
		== (std::array<bool, 7>{{false, false, true, false, false, false, false}}));
	assert(mdJucePlugin::monomachineTrigModeLeds(panel)
		== (std::array<bool, 4>{{false, false, true, false}}));
	assert(mdJucePlugin::panelLedLit(panel, 0x25, 6));

	std::cout << "Monomachine native panel LED and page status rendering passed\n";
	return 0;
}
