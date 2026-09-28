#pragma once

#include "mdfrontpanel.h"
#include "mdpanel.h"
#include "mdtypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace networkLib
{
	class TcpServer;
	class TcpStream;
	class Stream;
}

namespace md
{
	/*	Serves the machine's front panel to a browser on the local network, a tablet typically.

		HTTP GET /            the panel web app (index.html or index-mm.html for the Monomachine, from the resource callback)
		HTTP GET /ws          WebSocket. The server pushes the panel state whenever it changes:
		                      'S', model byte, 1024 bytes LCD VRAM (half, page, column), 14 LED bank bytes.
		                      The client sends text messages:
		                        b <PanelControl name> <1|0>    button press / release
		                        e <PanelEncoder name> <delta>  encoder turned, signed step count
		                        p <PanelEncoder name> <1|0>    encoder pushed / released
		                        t <track 0..15>                select a track (Machinedrum, via sysex)
		                      m <machine id>                 assign a machine to the current track
		                      sc edit <-1|0|1>                leave edit mode, or edit scene A/B
		                      sc assign <A|B> <0..15>         assign one of sixteen scenes
		                      sc fader <0..127>               set the scene crossfader
		                      sc mute <A|B> <0|1>              mute a side to the clean Kit value
				                      sc clear                        clear the edited side's current lock
				                      sc erase                        clear all locks from the edited scene
		                      The server also pushes a text frame "M <json>" with the machine catalogue,
		                      the current track and the UW sample slot names whenever they change.
		                      hello                          ask for the current state right away
		                    The server pushes "C <json>" with the current Kit's scene assignments,
		                    fader, mute flags, stored locks, and edit side.

		Sound, the same protocol as the digiemu and octemu remotes:
		                      "A <rate>"   (server, text) the sample rate of the sound it can stream; 0 while
		                                   there is none (no blocks, or a host rendering offline)
		                      a <1|0>      (page) stream the sound to this page, or stop
		                      binary 'A', then 16-bit LE stereo PCM: Main A/B (not C/D, E/F), every 20 ms,
		                                   only to pages that asked
		                    The sound is taken before the plugin's output gain, so the host can be turned
		                    down while a tablet plays. The audio thread only copies into a lock-free ring
		                    (feedAudio); a page whose socket is backed up misses blocks rather than holding
		                    up the others.
	*/
	class RemotePanelServer
	{
	public:
		struct Callbacks
		{
			std::function<bool(uint8_t _command, uint8_t _argument)> sendPanelEvent;
			std::function<FrontPanel()> snapshot;
			// complete sysex message including F0/F7, sent to the machine's MIDI in
			std::function<void(const std::vector<uint8_t>&)> sendSysex;
			// path without leading slash -> content. Returns false when unknown.
			std::function<bool(const std::string& _path, std::string& _data, std::string& _mime)> resource;
			// machine selector: catalogue and state as JSON (see mdmachines.h), and assignment to the current track
			std::function<std::string()> machineInfo;
			std::function<bool(uint16_t _machineId)> assignMachine;
			// Optional remote scenes controls. Scene state is JSON; scene numbers are zero based.
			std::function<std::string(int _editSide)> sceneInfo;
			std::function<bool(bool _sideB, uint8_t _scene)> assignScene;
			std::function<bool(uint8_t _value)> setSceneFader;
			std::function<bool(bool _sideB, bool _muted)> setSceneMuted;
			std::function<bool(bool _sideB, PanelEncoder _encoder, int _steps)> editSceneParameter;
			std::function<bool(bool _sideB)> clearSceneLock;
			std::function<bool(bool _sideB)> clearScene;
		};

		RemotePanelServer(MachineModel _model, int _port, Callbacks _callbacks);
		~RemotePanelServer();

		// tries _port and the following ones, returns false if none could be bound
		bool start();
		void stop();

		bool isRunning() const { return m_tcpServer != nullptr; }
		int getPort() const { return m_port; }
		size_t getClientCount() const;

		// Audio thread: one block of Main A/B. _sampleRate 0 means the block is not real time (offline
		// render) and is not streamed. Never blocks; does nothing but count while no page listens.
		void feedAudio(const float* _left, const float* _right, size_t _frames, uint32_t _sampleRate);

		static std::vector<uint8_t> encodeState(MachineModel _model, const FrontPanel& _panel);

	private:
		struct Client
		{
			std::shared_ptr<networkLib::TcpStream> stream;
			std::mutex writeMutex;
			std::vector<uint8_t> lastState;
			std::string lastMachineInfo;
			std::string lastSceneInfo;
			std::atomic<int> lastRate{-1};		// the sound rate last told to the page
			std::atomic<bool> websocket{false};
			std::atomic<bool> closed{false};
			std::atomic<bool> sound{false};		// it asked for the sound
			std::unique_ptr<std::thread> thread;
		};

		void onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream);
		void clientThreadFunc(const std::shared_ptr<Client>& _client);
		void publisherThreadFunc();
		void soundThreadFunc();

		bool serveHttp(Client& _client, const std::string& _method, const std::string& _path, const std::vector<std::pair<std::string, std::string>>& _headers);
		bool websocketLoop(Client& _client);
		bool sendWebSocketFrame(Client& _client, uint8_t _opcode, const uint8_t* _data, size_t _size);
		void handleMessage(Client& _client, const std::string& _message);
		void wantSound(Client& _client, bool _on);
		void releaseAllRows();

		static bool readLine(networkLib::Stream& _stream, std::string& _line);
		static std::string mimeForPath(const std::string& _path);

		const MachineModel m_model;
		int m_port;
		Callbacks m_callbacks;

		std::unique_ptr<networkLib::TcpServer> m_tcpServer;
		std::unique_ptr<std::thread> m_publisherThread;
		std::unique_ptr<std::thread> m_soundThread;
		std::atomic<bool> m_exit{false};

		// sound: a single producer (the audio thread) / single consumer (the sound thread) ring of
		// stereo frames, 16-bit left in the low half, right in the high half
		std::vector<uint32_t> m_soundRing;
		std::atomic<uint64_t> m_soundWrite{0};
		std::atomic<uint64_t> m_soundRead{0};
		std::atomic<int> m_soundListeners{0};
		std::atomic<uint32_t> m_feedRate{0};		// the rate of the last block fed, 0 offline
		std::atomic<uint64_t> m_feedCount{0};		// blocks fed, so the sound thread sees them flow
		std::atomic<uint32_t> m_streamRate{0};		// what pages are offered: m_feedRate while blocks flow

		mutable std::mutex m_clientsMutex;
		std::vector<std::shared_ptr<Client>> m_clients;

		std::mutex m_inputMutex;
		uint32_t m_infoTick = 0;
		std::atomic<bool> m_infoForce{false};
		std::atomic<int> m_sceneEditSide{-1};
		PanelRowState m_rows;
	};
}
