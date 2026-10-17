#pragma once

#include <cstdint>
#include <string>

#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdtypes.h"

namespace mdJucePlugin
{
	struct PanelStatusSnapshot
	{
		md::FrontPanel panel;
		uint64_t generation = 0;
		uint64_t ledSequence = 0;
	};

	// The small boundary shared by the plug-in editor and a hosted panel process.
	// Implementations only expose copied panel state and exact device panel bytes.
	class PanelBackend
	{
	public:
		virtual ~PanelBackend() = default;

		virtual md::MachineModel model() const = 0;
		virtual bool readStatus(PanelStatusSnapshot& _snapshot, std::string& _error) = 0;
		virtual bool sendPanel(uint8_t _command, uint8_t _argument, std::string& _error) = 0;
		virtual std::string connectionError() const = 0;
	};
}
