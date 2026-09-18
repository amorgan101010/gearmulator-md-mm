#pragma once

#include "mdLib/mdtypes.h"

#include <cstdint>

namespace md::test
{
	// A deadline in emulated time. A wall-clock deadline makes a firmware test's result depend on
	// how fast and how busy the host is; a budget of emulated frames runs out at the same point on
	// every machine, so a timeout means the emulation really did not get there.
	class EmulatedBudget
	{
	public:
		explicit EmulatedBudget(const double _seconds)
			: m_remaining(static_cast<uint64_t>(_seconds * static_cast<double>(g_samplerate)))
		{
		}

		// Records _frames of emulation. Returns false once the budget is used up.
		bool spend(const uint64_t _frames)
		{
			m_remaining = _frames >= m_remaining ? 0 : m_remaining - _frames;
			return m_remaining > 0;
		}

		bool expired() const { return m_remaining == 0; }

	private:
		uint64_t m_remaining;
	};
}
