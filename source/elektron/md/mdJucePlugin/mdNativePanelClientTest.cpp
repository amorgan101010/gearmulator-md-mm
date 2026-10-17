#include "mdNativePanelClient.h"

#include "gmDeviceAbi/gm_device.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
	constexpr size_t headerSize = 16;
	constexpr size_t statusSize = 1038;
	constexpr uint16_t responseBit = 0x8000;

	void require(bool value, const std::string& message)
	{
		if(!value)
			throw std::runtime_error(message);
	}

	void put16(std::vector<uint8_t>& out, size_t at, uint16_t value)
	{
		out.at(at) = static_cast<uint8_t>(value);
		out.at(at + 1) = static_cast<uint8_t>(value >> 8);
	}
	void put32(std::vector<uint8_t>& out, size_t at, uint32_t value)
	{
		for(unsigned i = 0; i < 4; ++i)
			out.at(at + i) = static_cast<uint8_t>(value >> (i * 8));
	}
	void put64(std::vector<uint8_t>& out, size_t at, uint64_t value)
	{
		for(unsigned i = 0; i < 8; ++i)
			out.at(at + i) = static_cast<uint8_t>(value >> (i * 8));
	}
	uint16_t get16(const uint8_t* data) { return data[0] | (uint16_t(data[1]) << 8); }
	uint32_t get32(const uint8_t* data)
	{
		uint32_t value = 0;
		for(unsigned i = 0; i < 4; ++i)
			value |= uint32_t(data[i]) << (i * 8);
		return value;
	}

	std::vector<uint8_t> frame(uint16_t kind, uint32_t id,
		const std::vector<uint8_t>& payload, uint16_t version = 1)
	{
		std::vector<uint8_t> out(headerSize + payload.size(), 0);
		std::memcpy(out.data(), "GMNP", 4);
		put16(out, 4, version);
		put16(out, 6, kind);
		put32(out, 8, id);
		put32(out, 12, static_cast<uint32_t>(payload.size()));
		std::copy(payload.begin(), payload.end(), out.begin() + headerSize);
		return out;
	}

	struct ParsedRequest
	{
		uint16_t version = 0;
		uint16_t kind = 0;
		uint32_t id = 0;
		std::vector<uint8_t> payload;
	};

	ParsedRequest parse(const uint8_t* data, size_t size)
	{
		require(size >= headerSize, "request frame shorter than 16-byte header");
		require(std::memcmp(data, "GMNP", 4) == 0, "request signature is not GMNP");
		const auto payloadSize = get32(data + 12);
		require(size == headerSize + payloadSize, "request payload length mismatch");
		return { get16(data + 4), get16(data + 6), get32(data + 8),
			std::vector<uint8_t>(data + headerSize, data + size) };
	}

	class FakeEndpoint
	{
	public:
		using Handler = std::function<std::vector<uint8_t>(const ParsedRequest&)>;
		explicit FakeEndpoint(Handler handler)
			: m_handler(std::move(handler))
		{
			std::array<char, 64> temp{};
			std::strcpy(temp.data(), "/tmp/gmnp-native-test-XXXXXX");
			const auto* dir = ::mkdtemp(temp.data());
			require(dir != nullptr, "mkdtemp failed");
			m_directory = dir;
			m_socketPath = m_directory + "/engine.sock";
			m_fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
			require(m_fd >= 0, "test endpoint socket failed");
			sockaddr_un address{};
			address.sun_family = AF_UNIX;
			std::strncpy(address.sun_path, m_socketPath.c_str(), sizeof(address.sun_path) - 1);
			require(::bind(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
				"test endpoint bind failed");
			(void)::chmod(m_socketPath.c_str(), 0600);
			m_thread = std::thread([this] { serve(); });
		}
		~FakeEndpoint()
		{
			m_stopping.store(true, std::memory_order_release);
			if(m_thread.joinable())
				m_thread.join();
			if(m_fd >= 0)
				::close(m_fd);
			::unlink(m_socketPath.c_str());
			::rmdir(m_directory.c_str());
		}
		const std::string& socketPath() const { return m_socketPath; }
		const std::string& runtimeDirectory() const { return m_directory; }
		std::string threadError() const
		{
			std::lock_guard<std::mutex> lock(m_errorMutex);
			return m_threadError;
		}
		void removeSocketPath() { ::unlink(m_socketPath.c_str()); }
	private:
		void serve()
		{
			while(!m_stopping.load(std::memory_order_acquire))
			{
				pollfd pollItem{ m_fd, POLLIN, 0 };
				if(::poll(&pollItem, 1, 20) <= 0)
					continue;
				std::array<uint8_t, 2048> bytes{};
				sockaddr_un peer{};
				socklen_t peerSize = sizeof(peer);
				const auto size = ::recvfrom(m_fd, bytes.data(), bytes.size(), 0,
					reinterpret_cast<sockaddr*>(&peer), &peerSize);
				if(size <= 0)
					continue;
				try
				{
					const auto request = parse(bytes.data(), static_cast<size_t>(size));
					const auto response = m_handler(request);
					if(!response.empty())
						(void)::sendto(m_fd, response.data(), response.size(), 0,
							reinterpret_cast<sockaddr*>(&peer), peerSize);
				}
				catch(const std::exception& error)
				{
					std::lock_guard<std::mutex> lock(m_errorMutex);
					m_threadError = error.what();
				}
			}
		}
		Handler m_handler;
		std::atomic<bool> m_stopping{false};
		std::string m_directory;
		std::string m_socketPath;
		std::string m_threadError;
		mutable std::mutex m_errorMutex;
		int m_fd = -1;
		std::thread m_thread;
	};

	std::array<uint8_t, 16> token()
	{
		std::array<uint8_t, 16> result{};
		for(size_t i = 0; i < result.size(); ++i)
			result[i] = static_cast<uint8_t>(0xa0 + i);
		return result;
	}

	std::vector<uint8_t> helloResponse(const ParsedRequest& request,
		uint32_t model = GM_MODEL_MACHINEDRUM, std::string unit = "md",
		uint16_t version = 1)
	{
		const auto instance = token();
		std::vector<uint8_t> payload(25 + unit.size(), 0);
		std::copy(instance.begin(), instance.end(), payload.begin());
		put32(payload, 16, model);
		put32(payload, 20, statusSize);
		payload[24] = static_cast<uint8_t>(unit.size());
		std::copy(unit.begin(), unit.end(), payload.begin() + 25);
		return frame(responseBit | 1, request.id, payload, version);
	}

	std::vector<uint8_t> statusResponse(const ParsedRequest& request, uint64_t generation)
	{
		std::vector<uint8_t> payload(12 + statusSize, 0);
		put64(payload, 0, generation);
		put32(payload, 8, statusSize);
		payload[12] = 0x5a;
		payload[12 + 1024] = 0x7e;
		return frame(responseBit | 2, request.id, payload);
	}

	std::vector<uint8_t> errorResponse(const ParsedRequest& request, uint16_t code)
	{
		std::vector<uint8_t> payload(2);
		put16(payload, 0, code);
		return frame(0xffff, request.id, payload);
	}

	void testFramingStatusGenerationAndPanelBytes()
	{
		std::atomic<unsigned> statuses{0};
		std::atomic<unsigned> inputs{0};
		FakeEndpoint server([&](const ParsedRequest& request)
		{
			require(request.version == 1, "client request version differs from GMNP v1");
			if(request.kind == 1)
			{
				require(request.payload.size() == 19 && request.payload[0] == 2
					&& request.payload[1] == 'm' && request.payload[2] == 'd',
					"HELLO unit/model-token frame is incompatible");
				return helloResponse(request);
			}
			if(request.kind == 2)
			{
				++statuses;
				const auto expectedToken = token();
					require(request.payload.size() == 16 && request.payload ==
					std::vector<uint8_t>(expectedToken.begin(), expectedToken.end()),
					"STATUS did not carry the HELLO instance token");
				return statusResponse(request, statuses == 1 ? 42 : 41);
			}
			if(request.kind == 3)
			{
				++inputs;
				require(request.payload.size() == 18, "PANEL_INPUT payload must be token plus two bytes");
				require(request.payload[16] == 0x20 && request.payload[17] == 0x01,
					"PANEL_INPUT bytes were modified by the client");
				return frame(responseBit | 3, request.id, {});
			}
			throw std::runtime_error("unexpected request kind");
		});
		mdJucePlugin::NativePanelClient client(server.socketPath(), server.runtimeDirectory(),
			"md", md::MachineModel::Machinedrum, 300);
		mdJucePlugin::PanelStatusSnapshot snapshot;
		std::string error;
		require(client.readStatus(snapshot, error), "first STATUS request failed: " + error);
		require(snapshot.generation == 42 && snapshot.panel.getLcdVram(0, 0, 0) == 0x5a,
			"copied LCD status or generation did not decode");
		require(snapshot.panel.getLedBankRaw(0x20) == 0x7e,
			"copied LED-bank status did not decode");
		require(!client.readStatus(snapshot, error)
			&& error.find("moved backwards") != std::string::npos,
			"backwards STATUS generation was not rejected");
		require(client.sendPanel(0x20, 0x01, error), "PANEL_INPUT request failed: " + error);
		require(statuses == 2 && inputs == 1, "fake endpoint did not receive expected requests");
		require(server.threadError().empty(), "endpoint framing check failed: " + server.threadError());
	}

	void testWrongModelAndProtocolVersion()
	{
		FakeEndpoint wrongModel([](const ParsedRequest& request)
		{
			return helloResponse(request, GM_MODEL_MONOMACHINE, "md");
		});
		bool rejected = false;
		try
		{
			mdJucePlugin::NativePanelClient client(wrongModel.socketPath(),
				wrongModel.runtimeDirectory(), "md", md::MachineModel::Machinedrum, 300);
		}
		catch(const std::exception& error)
		{
			rejected = std::string(error.what()).find("model mismatch") != std::string::npos;
		}
		require(rejected, "wrong hosted model was not rejected during HELLO");

		FakeEndpoint wrongVersion([](const ParsedRequest& request)
		{
			return helloResponse(request, GM_MODEL_MACHINEDRUM, "md", 2);
		});
		rejected = false;
		try
		{
			mdJucePlugin::NativePanelClient client(wrongVersion.socketPath(),
				wrongVersion.runtimeDirectory(), "md", md::MachineModel::Machinedrum, 300);
		}
		catch(const std::exception& error)
		{
			rejected = std::string(error.what()).find("protocol version") != std::string::npos;
		}
		require(rejected, "incompatible protocol version was not rejected");

		FakeEndpoint wrongUnit([](const ParsedRequest& request)
		{
			return helloResponse(request, GM_MODEL_MACHINEDRUM, "different-md");
		});
		rejected = false;
		try
		{
			mdJucePlugin::NativePanelClient client(wrongUnit.socketPath(),
				wrongUnit.runtimeDirectory(), "md", md::MachineModel::Machinedrum, 300);
		}
		catch(const std::exception& error)
		{
			rejected = std::string(error.what()).find("different unit identifier") != std::string::npos;
		}
		require(rejected, "wrong logical unit was not rejected during HELLO");
	}

	void testStaleTokenAndEngineDisappearance()
	{
		FakeEndpoint stale([](const ParsedRequest& request)
		{
			if(request.kind == 1)
				return helloResponse(request);
			return errorResponse(request, 4);
		});
		mdJucePlugin::NativePanelClient staleClient(stale.socketPath(), stale.runtimeDirectory(),
			"md", md::MachineModel::Machinedrum, 300);
		mdJucePlugin::PanelStatusSnapshot snapshot;
		std::string error;
		require(!staleClient.readStatus(snapshot, error)
			&& error.find("instance changed") != std::string::npos,
			"stale engine token error was not surfaced");

		FakeEndpoint gone([](const ParsedRequest& request)
		{
			if(request.kind == 1)
				return helloResponse(request);
			return statusResponse(request, 1);
		});
		mdJucePlugin::NativePanelClient client(gone.socketPath(), gone.runtimeDirectory(),
			"md", md::MachineModel::Machinedrum, 300);
		gone.removeSocketPath();
		require(!client.readStatus(snapshot, error), "missing engine endpoint returned status");
		require(error.find("send native-panel request") != std::string::npos,
			"engine disappearance was not reported as a transport error");
	}
}

int main()
{
	try
	{
		testFramingStatusGenerationAndPanelBytes();
		testWrongModelAndProtocolVersion();
		testStaleTokenAndEngineDisappearance();
		std::cout << "GMNP v1 framing, model/token checks, generation, panel bytes, and disconnect passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
