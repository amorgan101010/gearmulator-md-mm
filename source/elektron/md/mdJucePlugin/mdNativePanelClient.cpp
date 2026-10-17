#include "mdNativePanelClient.h"

#include "gmDeviceAbi/gm_device.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace mdJucePlugin
{
	namespace
	{
		constexpr uint16_t g_protocolVersion = 1;
		constexpr uint16_t g_hello = 1;
		constexpr uint16_t g_status = 2;
		constexpr uint16_t g_panelInput = 3;
		constexpr uint16_t g_error = 0xffff;
		constexpr uint16_t g_responseBit = 0x8000;
		constexpr size_t g_headerSize = 16;
		constexpr size_t g_maxDatagram = 2048;
		constexpr size_t g_statusSize = 1024 + md::FrontPanel::g_ledBankCount;

		void put16(std::vector<uint8_t>& _out, size_t _at, uint16_t _value)
		{
			_out[_at] = static_cast<uint8_t>(_value);
			_out[_at + 1] = static_cast<uint8_t>(_value >> 8);
		}

		void put32(std::vector<uint8_t>& _out, size_t _at, uint32_t _value)
		{
			for(unsigned i = 0; i < 4; ++i)
				_out[_at + i] = static_cast<uint8_t>(_value >> (8 * i));
		}

		uint16_t get16(const uint8_t* _data)
		{
			return static_cast<uint16_t>(_data[0] | (static_cast<uint16_t>(_data[1]) << 8));
		}

		uint32_t get32(const uint8_t* _data)
		{
			uint32_t value = 0;
			for(unsigned i = 0; i < 4; ++i)
				value |= static_cast<uint32_t>(_data[i]) << (8 * i);
			return value;
		}

		uint64_t get64(const uint8_t* _data)
		{
			uint64_t value = 0;
			for(unsigned i = 0; i < 8; ++i)
				value |= static_cast<uint64_t>(_data[i]) << (8 * i);
			return value;
		}

		std::vector<uint8_t> frame(uint16_t _kind, uint32_t _requestId,
			const uint8_t* _payload, size_t _payloadSize)
		{
			if(_payloadSize > g_maxDatagram - g_headerSize)
				throw std::runtime_error("GMNP request exceeds datagram limit");
			std::vector<uint8_t> result(g_headerSize + _payloadSize, 0);
			std::memcpy(result.data(), "GMNP", 4);
			put16(result, 4, g_protocolVersion);
			put16(result, 6, _kind);
			put32(result, 8, _requestId);
			put32(result, 12, static_cast<uint32_t>(_payloadSize));
			if(_payloadSize)
				std::memcpy(result.data() + g_headerSize, _payload, _payloadSize);
			return result;
		}

		std::string systemError(const char* _operation)
		{
			return std::string(_operation) + ": " + std::strerror(errno);
		}

		const char* protocolError(uint16_t _code)
		{
			switch(_code)
			{
			case 1: return "malformed native-panel frame";
			case 2: return "incompatible native-panel protocol version";
			case 3: return "native-panel endpoint belongs to a different unit";
			case 4: return "native-panel engine instance changed; reconnect to the current engine";
			case 5: return "hosted front-panel status is unavailable";
			case 6: return "hosted panel input queue is full";
			default: return "native-panel endpoint returned an unknown protocol error";
			}
		}

		struct BoundSocket
		{
			int fd = -1;
			std::filesystem::path localPath;
			~BoundSocket()
			{
				if(fd >= 0)
					::close(fd);
				if(!localPath.empty())
					::unlink(localPath.c_str());
			}
		};
	}

	NativePanelClient::NativePanelClient(std::filesystem::path _socketPath,
		std::filesystem::path _runtimeDirectory, std::string _unit,
		const md::MachineModel _expectedModel, const unsigned _timeoutMilliseconds)
		: m_socketPath(std::move(_socketPath))
		, m_runtimeDirectory(std::move(_runtimeDirectory))
		, m_unit(std::move(_unit))
		, m_model(_expectedModel)
		, m_timeoutMilliseconds(std::clamp(_timeoutMilliseconds, 1u, 5000u))
	{
		if(m_unit.empty() || m_unit.size() > 255)
			throw std::invalid_argument("native-panel unit name must contain 1..255 bytes");
		if(m_socketPath.empty())
			throw std::invalid_argument("native-panel socket path is required");
		if(m_runtimeDirectory.empty())
			m_runtimeDirectory = "/tmp";
		if(m_socketPath.string().size() >= sizeof(sockaddr_un::sun_path))
			throw std::invalid_argument("native-panel socket path exceeds the Unix socket path limit");

		handshake();
	}

	void NativePanelClient::handshake()
	{
		std::vector<uint8_t> hello(1 + m_unit.size() + m_instance.size(), 0);
		hello[0] = static_cast<uint8_t>(m_unit.size());
		std::memcpy(hello.data() + 1, m_unit.data(), m_unit.size());
		// A zero expected token asks the current endpoint for its instance token. This is
		// required after gm-engine restarts, when the old token can no longer be valid.
		const auto response = exchange(g_hello, hello.data(), hello.size(),
			static_cast<uint16_t>(g_hello | g_responseBit));
		if(response.size() < 25)
			throw std::runtime_error("native-panel HELLO response is truncated");
		std::array<uint8_t, 16> newInstance{};
		std::copy_n(response.data(), newInstance.size(), newInstance.begin());
		const auto actualModel = get32(response.data() + 16);
		const auto actualStatusSize = get32(response.data() + 20);
		const auto actualUnitSize = response[24];
		if(response.size() != 25 + actualUnitSize
			|| std::string_view(reinterpret_cast<const char*>(response.data() + 25), actualUnitSize) != m_unit)
			throw std::runtime_error("native-panel HELLO returned a different unit identifier");
		const auto expectedModel = m_model == md::MachineModel::Machinedrum
			? GM_MODEL_MACHINEDRUM : GM_MODEL_MONOMACHINE;
		if(actualModel != expectedModel)
			throw std::runtime_error("native-panel model mismatch: endpoint is "
				+ std::string(actualModel == GM_MODEL_MACHINEDRUM ? "Machinedrum" : "Monomachine"));
		if(actualStatusSize != g_statusSize)
			throw std::runtime_error("native-panel status layout is incompatible (expected 1038 bytes)");
		m_instance = newInstance;
		m_lastGeneration = 0;
		m_haveGeneration = false;
		m_error.clear();
	}

	std::vector<uint8_t> NativePanelClient::exchange(const uint16_t _kind,
		const uint8_t* _payload, const size_t _payloadSize, const uint16_t _expectedKind)
	{
		const auto requestId = m_nextRequestId++;
		const auto request = frame(_kind, requestId, _payload, _payloadSize);
		BoundSocket local;
		local.fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if(local.fd < 0)
			throw std::runtime_error(systemError("create native-panel datagram socket"));

		const auto unique = "gmpn-" + std::to_string(::getpid()) + "-" + std::to_string(requestId) + ".sock";
		local.localPath = m_runtimeDirectory / unique;
		if(local.localPath.string().size() >= sizeof(sockaddr_un::sun_path))
			throw std::runtime_error("native-panel runtime path exceeds the Unix socket path limit");
		sockaddr_un localAddress{};
		localAddress.sun_family = AF_UNIX;
		std::strncpy(localAddress.sun_path, local.localPath.c_str(), sizeof(localAddress.sun_path) - 1);
		if(::bind(local.fd, reinterpret_cast<sockaddr*>(&localAddress), sizeof(localAddress)) != 0)
			throw std::runtime_error(systemError("bind native-panel reply socket"));
		(void)::chmod(local.localPath.c_str(), 0600);

		sockaddr_un serverAddress{};
		serverAddress.sun_family = AF_UNIX;
		std::strncpy(serverAddress.sun_path, m_socketPath.c_str(), sizeof(serverAddress.sun_path) - 1);
		if(::sendto(local.fd, request.data(), request.size(), 0,
			reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress))
			!= static_cast<ssize_t>(request.size()))
			throw std::runtime_error(systemError("send native-panel request"));

		pollfd ready{ local.fd, POLLIN, 0 };
		const int pollResult = ::poll(&ready, 1, static_cast<int>(m_timeoutMilliseconds));
		if(pollResult == 0)
			throw std::runtime_error("hosted engine did not answer the native-panel request before timeout");
		if(pollResult < 0)
			throw std::runtime_error(systemError("wait for native-panel response"));
		std::array<uint8_t, g_maxDatagram> response{};
		const auto size = ::recv(local.fd, response.data(), response.size(), 0);
		if(size < static_cast<ssize_t>(g_headerSize))
			throw std::runtime_error("native-panel response frame is truncated");
		if(std::memcmp(response.data(), "GMNP", 4) != 0)
			throw std::runtime_error("native-panel response has an invalid GMNP signature");
		const auto version = get16(response.data() + 4);
		const auto kind = get16(response.data() + 6);
		const auto responseId = get32(response.data() + 8);
		const auto payloadLength = get32(response.data() + 12);
		if(static_cast<size_t>(size) != g_headerSize + payloadLength)
			throw std::runtime_error("native-panel response length does not match its frame");
		if(version != g_protocolVersion)
			throw std::runtime_error("incompatible native-panel protocol version " + std::to_string(version));
		if(responseId != requestId)
			throw std::runtime_error("native-panel response request identifier does not match");
		if(kind == g_error)
		{
			if(payloadLength != 2)
				throw std::runtime_error("native-panel error response is malformed");
			throw std::runtime_error(protocolError(get16(response.data() + g_headerSize)));
		}
		if(kind != _expectedKind)
			throw std::runtime_error("native-panel response kind does not match the request");
		return { response.data() + g_headerSize, response.data() + g_headerSize + payloadLength };
	}

	void NativePanelClient::setError(const std::string& _error, std::string* _target)
	{
		m_error = _error;
		if(_target)
			*_target = _error;
	}

	bool NativePanelClient::reconnect(std::string& _error)
	{
		try
		{
			handshake();
			m_error.clear();
			_error.clear();
			return true;
		}
		catch(const std::exception& e)
		{
			setError(e.what(), &_error);
			return false;
		}
	}

	bool NativePanelClient::readStatus(PanelStatusSnapshot& _snapshot, std::string& _error)
	{
		try
		{
			const auto response = exchange(g_status, m_instance.data(), m_instance.size(),
				static_cast<uint16_t>(g_status | g_responseBit));
			if(response.size() != 12 + g_statusSize)
				throw std::runtime_error("native-panel status has an incompatible length");
			const auto generation = get64(response.data());
			const auto statusSize = get32(response.data() + 8);
			if(statusSize != g_statusSize)
				throw std::runtime_error("native-panel status layout changed after HELLO");
			if(m_haveGeneration && generation < m_lastGeneration)
				throw std::runtime_error("native-panel status generation moved backwards");
			_snapshot.panel.setRaw(response.data() + 12, response.data() + 12 + 1024);
			_snapshot.generation = generation;
			m_lastGeneration = generation;
			m_haveGeneration = true;
			m_error.clear();
			_error.clear();
			return true;
		}
		catch(const std::exception& e)
		{
			setError(e.what(), &_error);
			return false;
		}
	}

	bool NativePanelClient::sendPanel(const uint8_t _command, const uint8_t _argument,
		std::string& _error)
	{
		std::array<uint8_t, 18> payload{};
		std::copy(m_instance.begin(), m_instance.end(), payload.begin());
		payload[16] = _command;
		payload[17] = _argument;
		try
		{
			(void)exchange(g_panelInput, payload.data(), payload.size(),
				static_cast<uint16_t>(g_panelInput | g_responseBit));
			m_error.clear();
			_error.clear();
			return true;
		}
		catch(const std::exception& e)
		{
			setError(e.what(), &_error);
			return false;
		}
	}
}
