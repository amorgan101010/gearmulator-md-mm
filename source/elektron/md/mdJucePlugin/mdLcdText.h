#pragma once

#include <cstdint>

#include "mdLib/mdfrontpanel.h"

// Identifies firmware-drawn text on the Monomachine LCD by hashing the pixels of fixed
// screen regions. No glyph or bitmap data is stored: tables map hashes to text that
// was transcribed by hand from captured screens and the owner's manual.
namespace mdJucePlugin::lcdText
{
	inline uint64_t hashRegion(const md::FrontPanel& _panel, const uint32_t _x, const uint32_t _y,
		const uint32_t _width, const uint32_t _height)
	{
		uint64_t hash = 1469598103934665603ull;
		for(uint32_t y = _y; y < _y + _height; ++y)
		{
			for(uint32_t x = _x; x < _x + _width; ++x)
			{
				hash ^= _panel.getLcdPixel(x, y) ? 1u : 2u;
				hash *= 1099511628211ull;
			}
		}
		return hash;
	}

	inline bool regionBlank(const md::FrontPanel& _panel, const uint32_t _x, const uint32_t _y,
		const uint32_t _width, const uint32_t _height)
	{
		for(uint32_t y = _y; y < _y + _height; ++y)
			for(uint32_t x = _x; x < _x + _width; ++x)
				if(_panel.getLcdPixel(x, y))
					return false;
		return true;
	}

	// The label strip at the top of DATA ENTRY field A-H on the standard 2x4 page grid. It
	// matches lcdInteraction::encoderRect(Standard) minus the dotted field separator.
	struct Region
	{
		uint32_t x, y, width, height;
	};

	inline constexpr Region fieldLabel(const unsigned _encoder)
	{
		return { 48u + 20u * (_encoder % 4), _encoder < 4 ? 2u : 33u, 19u, 7u };
	}

	// The value line under a top-row field on the LFO pages (PAGE and DEST show text here).
	inline constexpr Region lfoValue(const unsigned _encoder)
	{
		auto region = fieldLabel(_encoder);
		region.y += 23;
		return region;
	}

	// The active track's machine name, bottom left (e.g. SWAVE>SAW).
	inline constexpr Region g_machineName{ 0u, 56u, 48u, 7u };

	inline uint64_t hash(const md::FrontPanel& _panel, const Region& _region)
	{
		return hashRegion(_panel, _region.x, _region.y, _region.width, _region.height);
	}

	inline bool blank(const md::FrontPanel& _panel, const Region& _region)
	{
		return regionBlank(_panel, _region.x, _region.y, _region.width, _region.height);
	}
}
