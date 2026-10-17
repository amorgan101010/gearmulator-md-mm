#pragma once

#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdtypes.h"

#include "juce_graphics/juce_graphics.h"

namespace mdJucePlugin
{
	uint32_t frontPanelLcdOnColor(md::MachineModel _model, uint32_t _skinColor = 0);
	uint32_t frontPanelLcdOffColor(md::MachineModel _model, uint32_t _skinColor = 0);

	// Shared rendering primitive for the existing RML editor and the hosted
	// panel-only window. The skin and pixel geometry remain the existing ones.
	juce::Image makeFrontPanelLcdImage(const md::FrontPanel& _panel,
		md::MachineModel _model, uint32_t _lcdOn = 0, uint32_t _lcdOff = 0);
}
