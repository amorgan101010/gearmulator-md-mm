#include "mdFrontPanelLcd.h"

namespace mdJucePlugin
{
	namespace
	{
		constexpr uint32_t g_mdLcdOff = 0xffe0472b;
		constexpr uint32_t g_mdLcdOn = 0xff38100a;
		constexpr uint32_t g_mmLcdOff = 0xffb9c8b2;
		constexpr uint32_t g_mmLcdOn = 0xff1a2b1e;
	}

	uint32_t frontPanelLcdOnColor(const md::MachineModel _model, const uint32_t _skinColor)
	{
		return _skinColor ? _skinColor : _model == md::MachineModel::Monomachine
			? g_mmLcdOn : g_mdLcdOn;
	}

	uint32_t frontPanelLcdOffColor(const md::MachineModel _model, const uint32_t _skinColor)
	{
		return _skinColor ? _skinColor : _model == md::MachineModel::Monomachine
			? g_mmLcdOff : g_mdLcdOff;
	}

	juce::Image makeFrontPanelLcdImage(const md::FrontPanel& _panel,
		const md::MachineModel _model, const uint32_t _lcdOn, const uint32_t _lcdOff)
	{
		const auto on = frontPanelLcdOnColor(_model, _lcdOn);
		const auto off = frontPanelLcdOffColor(_model, _lcdOff);
		juce::Image lcd(juce::Image::ARGB, md::FrontPanel::g_lcdWidth,
			md::FrontPanel::g_lcdHeight, false);
		const juce::Image::BitmapData pixels(lcd, juce::Image::BitmapData::writeOnly);
		for(uint32_t y = 0; y < md::FrontPanel::g_lcdHeight; ++y)
			for(uint32_t x = 0; x < md::FrontPanel::g_lcdWidth; ++x)
				pixels.setPixelColour(x, y,
					juce::Colour(_panel.getLcdPixel(x, y) ? on : off));
		return lcd;
	}
}
