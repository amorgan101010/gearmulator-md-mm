#pragma once

#include <algorithm>
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

	// The value line of a cell in the Machinedrum's LFO window (LayoutKind::Lfo), which prints
	// the target track, the target parameter and the update mode as text.
	inline constexpr Region mdLfoValue(const unsigned _encoder)
	{
		return { 10u + 24u * (_encoder % 4), _encoder < 4 ? 23u : 45u, 22u, 7u };
	}
	// The active track's machine name, bottom left (e.g. SWAVE>SAW).
	inline constexpr Region g_machineName{ 0u, 56u, 48u, 7u };

	// Machinedrum: the same strip reads e.g. TRX>B2>SYNT. Only the machine part (TRX>B2), not
	// the page suffix, which changes with SYNTHESIS/EFFECTS/ROUTING.
	inline constexpr Region g_mdMachineName{ 0u, 56u, 25u, 7u };

	inline uint64_t hash(const md::FrontPanel& _panel, const Region& _region)
	{
		return hashRegion(_panel, _region.x, _region.y, _region.width, _region.height);
	}

	// Hash of the lit pixels cropped to their bounding box, so the same text hashes the same
	// wherever it is centred. The Machinedrum centres column D labels one pixel differently.
	inline uint64_t inkHash(const md::FrontPanel& _panel, const Region& _region)
	{
		uint32_t minX = _region.x + _region.width, maxX = 0, minY = _region.y + _region.height, maxY = 0;
		for(uint32_t y = _region.y; y < _region.y + _region.height; ++y)
		{
			for(uint32_t x = _region.x; x < _region.x + _region.width; ++x)
			{
				if(!_panel.getLcdPixel(x, y))
					continue;
				minX = std::min(minX, x); maxX = std::max(maxX, x);
				minY = std::min(minY, y); maxY = std::max(maxY, y);
			}
		}
		if(minX > maxX)
			return 0;
		uint64_t hash = 1469598103934665603ull;
		for(const auto byte : { static_cast<uint8_t>(maxX - minX), static_cast<uint8_t>(maxY - minY) })
		{
			hash ^= byte;
			hash *= 1099511628211ull;
		}
		return hash ^ hashRegion(_panel, minX, minY, maxX - minX + 1, maxY - minY + 1);
	}

	inline bool blank(const md::FrontPanel& _panel, const Region& _region)
	{
		return regionBlank(_panel, _region.x, _region.y, _region.width, _region.height);
	}
}
