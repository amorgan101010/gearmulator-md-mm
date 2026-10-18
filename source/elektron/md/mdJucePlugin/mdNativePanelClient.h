#pragma once

#include "mdPanelBackend.h"

#include <array>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	// Client for Studio's GMNP v1 per-unit Unix datagram endpoint. It owns no
	// emulator and opens no audio or MIDI devices.
	class NativePanelClient final : public PanelBackend
	{
	public:
		NativePanelClient(std::string _socketPath,
			std::string _runtimeDirectory, std::string _unit,
			md::MachineModel _expectedModel, unsigned _timeoutMilliseconds = 120);
		~NativePanelClient() override = default;

		md::MachineModel model() const override { return m_model; }
		bool readStatus(PanelStatusSnapshot& _snapshot, std::string& _error) override;
		bool sendPanel(uint8_t _command, uint8_t _argument, std::string& _error) override;
		bool outputGain(float& _gain, std::string& _error);
		bool setOutputGain(float _gain, std::string& _error);
		bool reconnect(std::string& _error);
		std::string connectionError() const override { return m_error; }
		const std::array<uint8_t, 16>& instanceToken() const { return m_instance; }

	private:
		std::vector<uint8_t> exchange(uint16_t _kind, const uint8_t* _payload,
			size_t _payloadSize, uint16_t _expectedKind);
		void handshake();
		void setError(const std::string& _error, std::string* _target);

		std::string m_socketPath;
		std::string m_runtimeDirectory;
		std::string m_unit;
		md::MachineModel m_model;
		unsigned m_timeoutMilliseconds;
		std::array<uint8_t, 16> m_instance{};
		uint32_t m_nextRequestId = 1;
		uint64_t m_lastGeneration = 0;
		bool m_haveGeneration = false;
		std::string m_error;
	};
}
