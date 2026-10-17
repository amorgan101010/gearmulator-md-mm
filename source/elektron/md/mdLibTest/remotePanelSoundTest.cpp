// The remote panel's sound stream, end to end over real sockets, without the emulator: a feeder
// thread plays the audio thread's part at real-time pace, and two raw WebSocket clients check the
// protocol ('A <rate>', 'a 1' / 'a 0', binary 'A' PCM). Linux sockets (MSG_NOSIGNAL): built there only.

#include "mdLib/mdremotepanel.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr uint32_t g_rate = 48000;
	constexpr size_t g_blockFrames = 480;			// 10 ms
	constexpr float g_left = 0.25f;					// -> 8192
	constexpr float g_right = -0.75f;				// -> -24575

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	class WsClient
	{
	public:
		explicit WsClient(const int _port)
		{
			m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
			require(m_fd >= 0, "socket failed");
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(static_cast<uint16_t>(_port));
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			require(::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "connect failed");
			timeval tv{0, 50 * 1000};
			::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

			const std::string request = "GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
				"Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
			writeAll(request.data(), request.size());
			const auto until = Clock::now() + std::chrono::seconds(2);
			while(m_buf.find("\r\n\r\n") == std::string::npos)
			{
				require(Clock::now() < until, "no handshake reply");
				readSome();
			}
			require(m_buf.compare(0, 12, "HTTP/1.1 101") == 0, "handshake refused");
			m_buf.erase(0, m_buf.find("\r\n\r\n") + 4);
		}

		~WsClient() { ::close(m_fd); }

		void sendText(const std::string& _text)
		{
			// client frames are masked; a zero mask keeps the payload as is
			std::string frame;
			frame += static_cast<char>(0x81);
			frame += static_cast<char>(0x80 | _text.size());
			frame.append(4, '\0');
			frame += _text;
			writeAll(frame.data(), frame.size());
		}

		// read frames for _ms, noting what arrived
		void pump(const int _ms)
		{
			const auto until = Clock::now() + std::chrono::milliseconds(_ms);
			while(Clock::now() < until)
			{
				readSome();
				while(parseFrame())
				{
				}
			}
		}

		std::vector<std::string> texts;			// 'A <rate>' messages, in order
		size_t pcmBytes = 0;					// binary 'A' payload, without the 'A'
		size_t pcmFrames = 0;
		size_t wrongSamples = 0;

		std::string lastRate() const
		{
			for(auto it = texts.rbegin(); it != texts.rend(); ++it)
				if(it->compare(0, 2, "A ") == 0)
					return it->substr(2);
			return {};
		}

	private:
		void writeAll(const char* _data, size_t _size)
		{
			while(_size)
			{
				const auto n = ::send(m_fd, _data, _size, MSG_NOSIGNAL);
				require(n > 0, "send failed");
				_data += n;
				_size -= static_cast<size_t>(n);
			}
		}

		void readSome()
		{
			char tmp[65536];
			const auto n = ::recv(m_fd, tmp, sizeof(tmp), 0);
			if(n > 0)
				m_buf.append(tmp, static_cast<size_t>(n));
		}

		bool parseFrame()
		{
			if(m_buf.size() < 2)
				return false;
			const auto* p = reinterpret_cast<const uint8_t*>(m_buf.data());
			const auto opcode = p[0] & 0x0f;
			uint64_t len = p[1] & 0x7f;
			size_t header = 2;
			if(len == 126)
			{
				if(m_buf.size() < 4)
					return false;
				len = (static_cast<uint64_t>(p[2]) << 8) | p[3];
				header = 4;
			}
			else if(len == 127)
			{
				if(m_buf.size() < 10)
					return false;
				len = 0;
				for(int i = 0; i < 8; ++i)
					len = (len << 8) | p[2 + i];
				header = 10;
			}
			if(m_buf.size() < header + len)
				return false;
			const auto payload = m_buf.substr(header, static_cast<size_t>(len));
			m_buf.erase(0, header + static_cast<size_t>(len));

			if(opcode == 1)
				texts.push_back(payload);
			else if(opcode == 2 && !payload.empty() && payload[0] == 'A')
			{
				const auto bytes = payload.size() - 1;
				require((bytes & 3) == 0, "PCM frame is not whole stereo frames");
				pcmBytes += bytes;
				pcmFrames += bytes / 4;
				for(size_t i = 1; i + 3 < payload.size(); i += 4)
				{
					const auto* s = reinterpret_cast<const uint8_t*>(payload.data() + i);
					const auto l = static_cast<int16_t>(s[0] | (s[1] << 8));
					const auto r = static_cast<int16_t>(s[2] | (s[3] << 8));
					if(l != 8192 || r != -24575)
						++wrongSamples;
				}
			}
			return true;
		}

		int m_fd = -1;
		std::string m_buf;
	};

	// plays the audio thread: one block every 10 ms, paced to the wall clock
	class Feeder
	{
	public:
		Feeder(md::RemotePanelServer& _server, const uint32_t _rate) : m_server(_server), m_rate(_rate)
		{
			m_thread = std::thread([this]
			{
				std::vector<float> left(g_blockFrames, g_left), right(g_blockFrames, g_right);
				auto next = Clock::now();
				while(!m_stop)
				{
					m_server.feedAudio(left.data(), right.data(), g_blockFrames, m_rate);
					next += std::chrono::milliseconds(10);
					std::this_thread::sleep_until(next);
				}
			});
		}
		~Feeder()
		{
			m_stop = true;
			m_thread.join();
		}

	private:
		md::RemotePanelServer& m_server;
		const uint32_t m_rate;
		std::atomic<bool> m_stop{false};
		std::thread m_thread;
	};

	void run()
	{
		md::RemotePanelServer server(md::MachineModel::Machinedrum, 18790, {});
		require(server.start(), "server did not start");
		const auto port = server.getPort();

		WsClient listener(port), bystander(port);
		listener.pump(100);
		require(listener.lastRate() == "0", "a rate offered before any block: '" + listener.lastRate() + "'");

		{
			Feeder feeder(server, g_rate);
			listener.pump(150);
			bystander.pump(0);
			require(listener.lastRate() == std::to_string(g_rate), "rate not offered while blocks flow");

			listener.sendText("a 1");
			listener.pump(300);						// the page's start: not counted
			listener.pcmBytes = 0;
			const auto t0 = Clock::now();
			listener.pump(2000);
			bystander.pump(0);
			const auto seconds = std::chrono::duration<double>(Clock::now() - t0).count();
			const auto perSecond = static_cast<double>(listener.pcmBytes) / seconds;
			std::cout << "streamed " << perSecond << " B/s, expected " << g_rate * 4 << "\n";
			require(perSecond > g_rate * 4 * 0.9 && perSecond < g_rate * 4 * 1.1, "not streamed at real-time pace");
			require(listener.wrongSamples == 0, std::to_string(listener.wrongSamples) + " samples came out wrong");

			bystander.pump(200);
			require(bystander.pcmBytes == 0, "sound sent to a page that did not ask");
			require(bystander.lastRate() == std::to_string(g_rate), "the other page was not told the rate");

			listener.sendText("a 0");
			listener.pump(200);						// frames already on the way
			listener.pcmBytes = 0;
			listener.pump(500);
			require(listener.pcmBytes == 0, "sound kept coming after 'a 0'");
		}

		// no blocks: nothing to offer
		listener.pump(800);
		require(listener.lastRate() == "0", "rate still offered after the blocks stopped");

		// a host rendering offline feeds blocks at rate 0: still nothing to offer, none streamed
		listener.sendText("a 1");
		{
			Feeder offline(server, 0);
			listener.pcmBytes = 0;
			listener.pump(600);
			require(listener.lastRate() == "0", "rate offered while rendering offline");
			require(listener.pcmBytes == 0, "offline blocks were streamed");
		}

		server.stop();
	}
}

int main()
{
	try
	{
		run();
	}
	catch(const std::exception& e)
	{
		std::cerr << "FAIL: " << e.what() << "\n";
		return 1;
	}
	std::cout << "remotePanelSoundTest: ok\n";
	return 0;
}
