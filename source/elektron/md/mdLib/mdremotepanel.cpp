#include "mdremotepanel.h"
#include "mdmidiprotocol.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"
#include "networkLib/tcpServer.h"
#include "networkLib/tcpStream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <chrono>
#include <cstring>
#include <sstream>

namespace md
{
	namespace
	{
		constexpr int g_maxPortAttempts = 20;
		constexpr int g_publishIntervalMs = 16;
		constexpr size_t g_maxMessageSize = 4096;
		constexpr int g_soundIntervalMs = 20;			// send the sound this often
		constexpr size_t g_soundRingFrames = 1 << 15;	// most sound held: 0.68 s at 48 kHz
		constexpr int g_soundIdleMs = 500;				// no block for this long: nothing to offer

		// --- SHA-1 and base64, needed for the WebSocket handshake only ---

		uint32_t rol(const uint32_t _v, const uint32_t _bits) { return (_v << _bits) | (_v >> (32 - _bits)); }

		std::array<uint8_t, 20> sha1(const std::string& _input)
		{
			uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
			std::vector<uint8_t> msg(_input.begin(), _input.end());
			const uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
			msg.push_back(0x80);
			while((msg.size() % 64) != 56)
				msg.push_back(0);
			for(int i=7; i>=0; --i)
				msg.push_back(static_cast<uint8_t>(bitLen >> (i * 8)));
			for(size_t chunk = 0; chunk < msg.size(); chunk += 64)
			{
				uint32_t w[80];
				for(int i=0; i<16; ++i)
					w[i] = (static_cast<uint32_t>(msg[chunk + i*4]) << 24) | (static_cast<uint32_t>(msg[chunk + i*4 + 1]) << 16) | (static_cast<uint32_t>(msg[chunk + i*4 + 2]) << 8) | msg[chunk + i*4 + 3];
				for(int i=16; i<80; ++i)
					w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
				uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
				for(int i=0; i<80; ++i)
				{
					uint32_t f, k;
					if(i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
					else if(i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
					else if(i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
					else            { f = b ^ c ^ d; k = 0xCA62C1D6; }
					const uint32_t temp = rol(a, 5) + f + e + k + w[i];
					e = d; d = c; c = rol(b, 30); b = a; a = temp;
				}
				h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
			}
			std::array<uint8_t, 20> out{};
			for(int i=0; i<5; ++i)
				for(int j=0; j<4; ++j)
					out[i*4 + j] = static_cast<uint8_t>(h[i] >> (24 - j*8));
			return out;
		}

		std::string base64(const uint8_t* _data, const size_t _size)
		{
			static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			size_t i = 0;
			while(i + 2 < _size)
			{
				const uint32_t v = (static_cast<uint32_t>(_data[i]) << 16) | (static_cast<uint32_t>(_data[i+1]) << 8) | _data[i+2];
				out += table[(v >> 18) & 63]; out += table[(v >> 12) & 63]; out += table[(v >> 6) & 63]; out += table[v & 63];
				i += 3;
			}
			if(i + 1 == _size)
			{
				const uint32_t v = static_cast<uint32_t>(_data[i]) << 16;
				out += table[(v >> 18) & 63]; out += table[(v >> 12) & 63]; out += "==";
			}
			else if(i + 2 == _size)
			{
				const uint32_t v = (static_cast<uint32_t>(_data[i]) << 16) | (static_cast<uint32_t>(_data[i+1]) << 8);
				out += table[(v >> 18) & 63]; out += table[(v >> 12) & 63]; out += table[(v >> 6) & 63]; out += '=';
			}
			return out;
		}

		std::string lower(std::string _s)
		{
			std::transform(_s.begin(), _s.end(), _s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return _s;
		}

		std::optional<PanelControl> controlByName(const std::string& _name)
		{
			for(uint32_t i = 0; i <= static_cast<uint32_t>(PanelControl::ClassicExtended); ++i)
			{
				const auto control = static_cast<PanelControl>(i);
				if(_name == panelControlName(control))
					return control;
			}
			return {};
		}

		std::optional<PanelEncoder> encoderByName(const std::string& _name)
		{
			for(uint32_t i = 0; i <= static_cast<uint32_t>(PanelEncoder::SoundSelection); ++i)
			{
				const auto encoder = static_cast<PanelEncoder>(i);
				if(_name == panelEncoderName(encoder))
					return encoder;
			}
			return {};
		}
	}

	RemotePanelServer::RemotePanelServer(const MachineModel _model, const int _port, Callbacks _callbacks)
		: m_model(_model), m_port(_port), m_callbacks(std::move(_callbacks))
		, m_soundRing(g_soundRingFrames)
	{
	}

	RemotePanelServer::~RemotePanelServer()
	{
		stop();
	}

	int RemotePanelServer::sceneEditSide() const
	{
		return m_callbacks.getSceneEditSide ? m_callbacks.getSceneEditSide()
			: m_sceneEditSide.load(std::memory_order_acquire);
	}

	void RemotePanelServer::setSceneEditSide(const int _side)
	{
		if(m_callbacks.setSceneEditSide)
			m_callbacks.setSceneEditSide(_side);
		else
			m_sceneEditSide.store(_side, std::memory_order_release);
	}

	bool RemotePanelServer::start()
	{
		if(m_tcpServer)
			return true;

		for(int attempt = 0; attempt < g_maxPortAttempts; ++attempt)
		{
			const int port = m_port + attempt;
			try
			{
				m_tcpServer = std::make_unique<networkLib::TcpServer>([this](std::unique_ptr<networkLib::TcpStream> _stream)
				{
					onClientConnected(std::move(_stream));
				}, port);
				m_port = port;
				break;
			}
			catch(const std::exception& e)
			{
				if(attempt == g_maxPortAttempts - 1)
				{
					LOGNET(networkLib::LogLevel::Error, "Remote panel: no free port from " << m_port << " on: " << e.what());
					return false;
				}
			}
		}

		m_exit = false;
		m_publisherThread = std::make_unique<std::thread>([this] { publisherThreadFunc(); });
		m_soundThread = std::make_unique<std::thread>([this] { soundThreadFunc(); });
		LOGNET(networkLib::LogLevel::Info, "Remote panel listening on port " << m_port);
		return true;
	}

	void RemotePanelServer::stop()
	{
		m_exit = true;
		m_tcpServer.reset();

		std::vector<std::shared_ptr<Client>> clients;
		{
			std::lock_guard lock(m_clientsMutex);
			clients.swap(m_clients);
		}
		for(auto& c : clients)
		{
			c->closed = true;
			if(c->stream)
				c->stream->close();
		}
		for(auto& c : clients)
		{
			if(c->thread && c->thread->joinable())
				c->thread->join();
		}
		if(m_publisherThread && m_publisherThread->joinable())
			m_publisherThread->join();
		m_publisherThread.reset();
		if(m_soundThread && m_soundThread->joinable())
			m_soundThread->join();
		m_soundThread.reset();
	}

	size_t RemotePanelServer::getClientCount() const
	{
		std::lock_guard lock(m_clientsMutex);
		return m_clients.size();
	}

	std::vector<uint8_t> RemotePanelServer::encodeState(const MachineModel _model, const FrontPanel& _panel)
	{
		std::vector<uint8_t> out;
		out.reserve(2 + 2 * 8 * 64 + FrontPanel::g_ledBankCount);
		out.push_back('S');
		out.push_back(_model == MachineModel::Monomachine ? 1 : 0);
		for(uint32_t half = 0; half < 2; ++half)
			for(uint32_t page = 0; page < 8; ++page)
				for(uint32_t col = 0; col < 64; ++col)
					out.push_back(_panel.getLcdVram(half, page, col));
		for(uint8_t cmd = FrontPanel::g_firstLedBank; cmd <= FrontPanel::g_lastLedBank; ++cmd)
			out.push_back(_panel.getLedBankRaw(cmd));
		return out;
	}

	void RemotePanelServer::onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream)
	{
		auto client = std::make_shared<Client>();
		client->stream = std::shared_ptr<networkLib::TcpStream>(std::move(_stream));
		{
			std::lock_guard lock(m_clientsMutex);
			// drop finished clients
			m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(), [](const std::shared_ptr<Client>& _c)
			{
				if(!_c->closed)
					return false;
				if(_c->thread && _c->thread->joinable())
					_c->thread->join();
				return true;
			}), m_clients.end());
			m_clients.push_back(client);
		}
		client->thread = std::make_unique<std::thread>([this, client] { clientThreadFunc(client); });
	}

	bool RemotePanelServer::readLine(networkLib::Stream& _stream, std::string& _line)
	{
		_line.clear();
		char c;
		while(_stream.read(&c, 1))
		{
			if(c == '\r')
				continue;
			if(c == '\n')
				return true;
			_line += c;
			if(_line.size() > g_maxMessageSize)
				return false;
		}
		return false;
	}

	void RemotePanelServer::clientThreadFunc(const std::shared_ptr<Client>& _client)
	{
		try
		{
			networkLib::Stream& stream = *_client->stream;
			std::string requestLine;
			if(readLine(stream, requestLine))
			{
				std::istringstream ls(requestLine);
				std::string method, path, version;
				ls >> method >> path >> version;

				std::vector<std::pair<std::string, std::string>> headers;
				std::string line;
				while(readLine(stream, line) && !line.empty())
				{
					const auto colon = line.find(':');
					if(colon == std::string::npos)
						continue;
					auto value = line.substr(colon + 1);
					while(!value.empty() && value.front() == ' ')
						value.erase(value.begin());
					headers.emplace_back(lower(line.substr(0, colon)), value);
				}
				serveHttp(*_client, method, path, headers);
			}
		}
		catch(const networkLib::NetException&)
		{
		}
		catch(const std::exception& e)
		{
			LOGNET(networkLib::LogLevel::Warning, "Remote panel client error: " << e.what());
		}
		const auto wasWebsocket = _client->websocket.load();
		_client->websocket = false;
		_client->closed = true;
		wantSound(*_client, false);
		if(_client->stream)
			_client->stream->close();
		// a panel that goes away must not leave keys held down on the machine
		if(wasWebsocket)
			releaseAllRows();
	}

	void RemotePanelServer::releaseAllRows()
	{
		std::lock_guard lock(m_inputMutex);
		for(uint8_t row = 0x20; row <= 0x26; ++row)
		{
			if(m_rows.mask(row) == 0)
				continue;
			if(m_callbacks.sendPanelEvent)
				m_callbacks.sendPanelEvent(row, 0);
		}
		m_rows.reset();
	}

	std::string RemotePanelServer::mimeForPath(const std::string& _path)
	{
		const auto dot = _path.rfind('.');
		const auto ext = dot == std::string::npos ? std::string() : lower(_path.substr(dot + 1));
		if(ext == "html") return "text/html; charset=utf-8";
		if(ext == "js") return "text/javascript; charset=utf-8";
		if(ext == "css") return "text/css; charset=utf-8";
		if(ext == "png") return "image/png";
		if(ext == "svg") return "image/svg+xml";
		if(ext == "json") return "application/json";
		if(ext == "ttf") return "font/ttf";
		if(ext == "woff2") return "font/woff2";
		if(ext == "ico") return "image/x-icon";
		return "application/octet-stream";
	}

	bool RemotePanelServer::serveHttp(Client& _client, const std::string& _method, const std::string& _path, const std::vector<std::pair<std::string, std::string>>& _headers)
	{
		auto header = [&](const std::string& _name) -> std::string
		{
			for(const auto& [k, v] : _headers)
				if(k == _name)
					return v;
			return {};
		};

		networkLib::Stream& stream = *_client.stream;

		auto writeAll = [&](const std::string& _s)
		{
			return stream.write(_s.data(), static_cast<uint32_t>(_s.size()));
		};

		if(lower(header("upgrade")) == "websocket")
		{
			const auto key = header("sec-websocket-key");
			const auto digest = sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
			const auto accept = base64(digest.data(), digest.size());
			std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
			if(!writeAll(response) || !stream.flush())
				return false;
			_client.websocket = true;
			return websocketLoop(_client);
		}

		// static resource
		std::string path = _path;
		const auto q = path.find('?');
		if(q != std::string::npos)
			path.resize(q);
		if(path.empty() || path == "/")
			path = m_model == MachineModel::Monomachine ? "/index-mm.html" : "/index.html";
		if(path.front() == '/')
			path.erase(path.begin());
		if(path.find("..") != std::string::npos || _method != "GET")
		{
			writeAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
			return false;
		}

		std::string data, mime;
		if(!m_callbacks.resource || !m_callbacks.resource(path, data, mime))
		{
			LOGNET(networkLib::LogLevel::Debug, "Remote panel: no resource " << path);
			writeAll("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
			return false;
		}
		if(mime.empty())
			mime = mimeForPath(path);

		std::string response = "HTTP/1.1 200 OK\r\nContent-Type: " + mime + "\r\nContent-Length: " + std::to_string(data.size()) + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
		if(!writeAll(response))
			return false;
		return stream.write(data.data(), static_cast<uint32_t>(data.size())) && stream.flush();
	}

	bool RemotePanelServer::sendWebSocketFrame(Client& _client, const uint8_t _opcode, const uint8_t* _data, const size_t _size)
	{
		std::vector<uint8_t> frame;
		frame.reserve(_size + 10);
		frame.push_back(static_cast<uint8_t>(0x80 | (_opcode & 0x0f)));
		if(_size < 126)
			frame.push_back(static_cast<uint8_t>(_size));
		else if(_size < 65536)
		{
			frame.push_back(126);
			frame.push_back(static_cast<uint8_t>(_size >> 8));
			frame.push_back(static_cast<uint8_t>(_size & 0xff));
		}
		else
		{
			frame.push_back(127);
			for(int i=7; i>=0; --i)
				frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(_size) >> (i * 8)) & 0xff));
		}
		frame.insert(frame.end(), _data, _data + _size);

		std::lock_guard lock(_client.writeMutex);
		if(_client.closed)
			return false;
		try
		{
			networkLib::Stream& stream = *_client.stream;
			return stream.write(frame.data(), static_cast<uint32_t>(frame.size())) && stream.flush();
		}
		catch(const networkLib::NetException&)
		{
			return false;
		}
	}

	bool RemotePanelServer::websocketLoop(Client& _client)
	{
		networkLib::Stream& stream = *_client.stream;

		// the current state right away, the publisher only sends changes
		if(m_callbacks.snapshot)
		{
			const auto state = encodeState(m_model, m_callbacks.snapshot());
			_client.lastState = state;
			if(!sendWebSocketFrame(_client, 2, state.data(), state.size()))
				return false;
		}

		std::string fragmented;

		while(!m_exit && !_client.closed)
		{
			uint8_t hdr[2];
			if(!stream.read(hdr, 2))
				return false;
			const bool fin = (hdr[0] & 0x80) != 0;
			const uint8_t opcode = hdr[0] & 0x0f;
			const bool masked = (hdr[1] & 0x80) != 0;
			uint64_t len = hdr[1] & 0x7f;
			if(len == 126)
			{
				uint8_t ext[2];
				if(!stream.read(ext, 2)) return false;
				len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
			}
			else if(len == 127)
			{
				uint8_t ext[8];
				if(!stream.read(ext, 8)) return false;
				len = 0;
				for(int i=0; i<8; ++i)
					len = (len << 8) | ext[i];
			}
			if(len > g_maxMessageSize)
				return false;
			uint8_t mask[4] = {0,0,0,0};
			if(masked && !stream.read(mask, 4))
				return false;
			std::vector<uint8_t> payload(static_cast<size_t>(len));
			if(len && !stream.read(payload.data(), static_cast<uint32_t>(len)))
				return false;
			if(masked)
				for(size_t i=0; i<payload.size(); ++i)
					payload[i] ^= mask[i & 3];

			switch(opcode)
			{
			case 0x0:	// continuation
			case 0x1:	// text
			case 0x2:	// binary
				fragmented.append(payload.begin(), payload.end());
				if(fin)
				{
					handleMessage(_client, fragmented);
					fragmented.clear();
				}
				break;
			case 0x8:	// close
				sendWebSocketFrame(_client, 0x8, payload.data(), std::min<size_t>(payload.size(), 2));
				return true;
			case 0x9:	// ping
				sendWebSocketFrame(_client, 0xA, payload.data(), payload.size());
				break;
			default:
				break;
			}
		}
		return true;
	}

	void RemotePanelServer::handleMessage(Client& _client, const std::string& _message)
	{
		std::istringstream ss(_message);
		std::string type;
		ss >> type;

		if(type == "hello")
		{
			// force a full state on the next publish
			std::lock_guard lock(m_clientsMutex);
			for(auto& c : m_clients)
			{
				c->lastState.clear();
				c->lastMachineInfo.clear();
				c->lastSceneInfo.clear();
				c->lastRate = -1;
			}
			m_infoForce = true;
			return;
		}

		if(type == "a")
		{
			int on = -1;
			ss >> on;
			if(on == 0 || on == 1)
				wantSound(_client, on == 1);
			return;
		}

		if(type == "m")
		{
			int id = -1;
			ss >> id;
			if(id >= 0 && id < 256 && m_callbacks.assignMachine)
				m_callbacks.assignMachine(static_cast<uint16_t>(id));
			return;
		}

		if(type == "t")
		{
			int track = 0;
			ss >> track;
			if(m_callbacks.sendSysex && m_model == MachineModel::Machinedrum)
			{
				const auto body = midiProtocol::selectTrack(track);
				std::vector<uint8_t> sysex;
				sysex.reserve(body.size() + 2);
				sysex.push_back(0xf0);
				sysex.insert(sysex.end(), body.begin(), body.end());
				sysex.push_back(0xf7);
				m_callbacks.sendSysex(sysex);
			}
			return;
		}

		if(type == "sc")
		{
			std::string action;
			ss >> action;
			std::lock_guard lock(m_inputMutex);
			if(action == "edit")
			{
				int side = -1;
				ss >> side;
				if(side >= -1 && side <= 1)
					setSceneEditSide(side);
			}
			else if(action == "assign")
			{
				std::string side;
				int scene = -1;
				ss >> side >> scene;
				if((side == "A" || side == "B") && scene >= 0 && scene < 16
					&& m_callbacks.assignScene)
					m_callbacks.assignScene(side == "B", static_cast<uint8_t>(scene));
			}
			else if(action == "fader")
			{
				int value = -1;
				ss >> value;
				if(value >= 0 && value <= 127 && m_callbacks.setSceneFader)
					m_callbacks.setSceneFader(static_cast<uint8_t>(value));
			}
			else if(action == "mute")
			{
				std::string side;
				int muted = -1;
				ss >> side >> muted;
				if((side == "A" || side == "B") && (muted == 0 || muted == 1)
					&& m_callbacks.setSceneMuted)
					m_callbacks.setSceneMuted(side == "B", muted != 0);
			}
			else if(action == "clear")
			{
				const auto side = sceneEditSide();
				if(side >= 0 && m_callbacks.clearSceneLock)
					m_callbacks.clearSceneLock(side == 1);
			}
			else if(action == "erase")
			{
				const auto side = sceneEditSide();
				if(side >= 0 && m_callbacks.clearScene)
					m_callbacks.clearScene(side == 1);
			}
			m_infoForce.store(true, std::memory_order_release);
			return;
		}

		if(!m_callbacks.sendPanelEvent)
			return;

		std::lock_guard lock(m_inputMutex);

		if(type == "b")
		{
			std::string name; int down = 0;
			ss >> name >> down;
			const auto control = controlByName(name);
			if(!control)
				return;
			const auto editSide = sceneEditSide();
			if(down && editSide >= 0
				&& *control >= PanelControl::Trigger1
				&& *control <= PanelControl::Trigger16)
			{
				const auto scene = static_cast<uint8_t>(static_cast<unsigned>(*control)
					- static_cast<unsigned>(PanelControl::Trigger1));
				if(m_callbacks.assignScene)
					m_callbacks.assignScene(editSide == 1, scene);
				setSceneEditSide(-1);
				m_infoForce.store(true, std::memory_order_release);
				return;
			}
			const auto packet = panelPacket(m_model, *control);
			if(!packet)
				return;
			const auto combined = down ? m_rows.press(*packet) : m_rows.release(*packet);
			m_callbacks.sendPanelEvent(combined.row, combined.mask);
		}
		else if(type == "e")
		{
			std::string name; int delta = 0;
			ss >> name >> delta;
			const auto encoder = encoderByName(name);
			if(!encoder || delta == 0)
				return;
			const auto editSide = sceneEditSide();
			if(editSide >= 0 && *encoder >= PanelEncoder::DataEntryA
				&& *encoder <= PanelEncoder::DataEntryH)
			{
				if(m_callbacks.editSceneParameter)
					m_callbacks.editSceneParameter(editSide == 1, *encoder, delta);
				m_infoForce.store(true, std::memory_order_release);
				return;
			}
			const auto command = panelEncoderCommand(m_model, *encoder);
			if(!command)
				return;
			const auto argument = static_cast<uint8_t>(delta > 0 ? 0x01 : 0xff);
			const auto count = std::min(std::abs(delta), 8);
			for(int i=0; i<count; ++i)
				m_callbacks.sendPanelEvent(*command, argument);
		}
		else if(type == "p")
		{
			std::string name; int down = 0;
			ss >> name >> down;
			const auto encoder = encoderByName(name);
			if(!encoder)
				return;
			const auto packet = panelEncoderPressPacket(m_model, *encoder);
			if(!packet)
				return;
			const auto combined = down ? m_rows.press(*packet) : m_rows.release(*packet);
			m_callbacks.sendPanelEvent(combined.row, combined.mask);
		}
	}

	void RemotePanelServer::publisherThreadFunc()
	{
		while(!m_exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(g_publishIntervalMs));

			std::vector<std::shared_ptr<Client>> clients;
			{
				std::lock_guard lock(m_clientsMutex);
				for(auto& c : m_clients)
					if(c->websocket && !c->closed)
						clients.push_back(c);
			}
			if(clients.empty())
				continue;

			// the rate of the sound a page can ask for, told whenever it changes
			const auto rate = static_cast<int>(m_streamRate.load(std::memory_order_acquire));
			for(auto& c : clients)
			{
				if(c->lastRate == rate)
					continue;
				const auto text = "A " + std::to_string(rate);
				if(!sendWebSocketFrame(*c, 1, reinterpret_cast<const uint8_t*>(text.data()), text.size()))
				{
					c->closed = true;
					c->stream->close();
					continue;
				}
				c->lastRate = rate;
			}

			if(!m_callbacks.snapshot)
				continue;

			const auto state = encodeState(m_model, m_callbacks.snapshot());

			// the machine info changes rarely; build it about four times a second
			std::string machineInfo;
			const bool infoDue = m_callbacks.machineInfo && ((++m_infoTick % 16) == 0 || m_infoForce.exchange(false));
			if(infoDue)
				machineInfo = "M " + m_callbacks.machineInfo();
			const auto sceneInfo = m_callbacks.sceneInfo
				? "C " + m_callbacks.sceneInfo(
					sceneEditSide())
				: std::string();

			for(auto& c : clients)
			{
				if(!sceneInfo.empty() && c->lastSceneInfo != sceneInfo)
				{
					c->lastSceneInfo = sceneInfo;
					if(!sendWebSocketFrame(*c, 1,
						reinterpret_cast<const uint8_t*>(sceneInfo.data()), sceneInfo.size()))
					{
						c->closed = true;
						c->stream->close();
						continue;
					}
				}
				if(infoDue && c->lastMachineInfo != machineInfo)
				{
					c->lastMachineInfo = machineInfo;
					if(!sendWebSocketFrame(*c, 1, reinterpret_cast<const uint8_t*>(machineInfo.data()), machineInfo.size()))
					{
						c->closed = true;
						c->stream->close();
						continue;
					}
				}
				if(c->lastState == state)
					continue;
				c->lastState = state;
				if(!sendWebSocketFrame(*c, 2, state.data(), state.size()))
				{
					c->closed = true;
					c->stream->close();
				}
			}
		}
	}

	void RemotePanelServer::feedAudio(const float* _left, const float* _right, const size_t _frames, const uint32_t _sampleRate)
	{
		m_feedRate.store(_sampleRate, std::memory_order_relaxed);
		m_feedCount.fetch_add(1, std::memory_order_release);

		if(!_left || !_frames || !_sampleRate || m_soundListeners.load(std::memory_order_acquire) <= 0)
			return;
		if(!_right)
			_right = _left;

		const auto size = m_soundRing.size();
		const auto w = m_soundWrite.load(std::memory_order_relaxed);
		const auto r = m_soundRead.load(std::memory_order_acquire);
		// the sound thread is not taking it: this block goes, the ring keeps what it has
		if(w - r + _frames > size)
			return;

		auto toInt16 = [](const float _v) -> uint32_t
		{
			// NaN goes to silence
			const float v = _v >= 1.0f ? 1.0f : _v >= -1.0f ? _v : _v < -1.0f ? -1.0f : 0.0f;
			return static_cast<uint16_t>(static_cast<int16_t>(std::lrint(v * 32767.0f)));
		};
		for(size_t i = 0; i < _frames; ++i)
			m_soundRing[(w + i) & (size - 1)] = toInt16(_left[i]) | (toInt16(_right[i]) << 16);
		m_soundWrite.store(w + _frames, std::memory_order_release);
	}

	void RemotePanelServer::wantSound(Client& _client, bool _on)
	{
		_on = _on && !_client.closed;
		if(_client.sound.exchange(_on) == _on)
			return;
		m_soundListeners.fetch_add(_on ? 1 : -1, std::memory_order_acq_rel);
	}

	void RemotePanelServer::soundThreadFunc()
	{
		const auto idle = std::chrono::milliseconds(g_soundIdleMs);
		const auto mask = m_soundRing.size() - 1;
		std::vector<uint8_t> frame;
		frame.reserve(1 + m_soundRing.size() * 4);
		uint64_t lastCount = m_feedCount.load(std::memory_order_acquire);
		auto lastFed = std::chrono::steady_clock::now() - idle;

		while(!m_exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(g_soundIntervalMs));

			// offer the sound only while blocks come: a stopped audio device or a host that suspends
			// the plugin sends nothing, and a page would only run dry
			const auto now = std::chrono::steady_clock::now();
			const auto count = m_feedCount.load(std::memory_order_acquire);
			if(count != lastCount)
			{
				lastCount = count;
				lastFed = now;
			}
			const auto rate = now - lastFed < idle ? m_feedRate.load(std::memory_order_relaxed) : 0u;
			m_streamRate.store(rate, std::memory_order_release);

			const auto w = m_soundWrite.load(std::memory_order_acquire);
			const auto r = m_soundRead.load(std::memory_order_relaxed);
			if(w == r)
				continue;
			if(!rate || m_soundListeners.load(std::memory_order_acquire) <= 0)
			{
				m_soundRead.store(w, std::memory_order_release);	// nobody to send it to: none kept
				continue;
			}

			const auto n = static_cast<size_t>(w - r);
			frame.resize(1 + n * 4);
			frame[0] = 'A';
			for(size_t i = 0; i < n; ++i)
			{
				const auto v = m_soundRing[(r + i) & mask];
				auto* const out = &frame[1 + i * 4];
				out[0] = static_cast<uint8_t>(v);
				out[1] = static_cast<uint8_t>(v >> 8);
				out[2] = static_cast<uint8_t>(v >> 16);
				out[3] = static_cast<uint8_t>(v >> 24);
			}
			m_soundRead.store(w, std::memory_order_release);

			std::vector<std::shared_ptr<Client>> clients;
			{
				std::lock_guard lock(m_clientsMutex);
				for(auto& c : m_clients)
					if(c->websocket && !c->closed && c->sound)
						clients.push_back(c);
			}
			for(auto& c : clients)
			{
				// a page whose socket is backed up (Wi-Fi fading, a tablet asleep) misses this block:
				// waiting on it would hold up every other page
				if(!c->stream->canWrite())
					continue;
				if(!sendWebSocketFrame(*c, 2, frame.data(), frame.size()))
				{
					c->closed = true;
					c->stream->close();
				}
			}
		}
	}
}
