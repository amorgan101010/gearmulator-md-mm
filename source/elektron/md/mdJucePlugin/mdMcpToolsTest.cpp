#include "mdPluginProcessor.h"
#include "mdMonomachineDump.h"
#include "mcpServerLib/mcpPluginServer.h"
#include "mcpServerLib/jsonHelpers.h"

#include "juce_audio_utils/juce_audio_utils.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	using Json = mcpServer::JsonValue;

	void require(const bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}

	struct Reply
	{
		int status = 0;
		std::string body;
	};

	// A plain socket: juce::URL's Linux client ends its generated headers with an extra
	// CRLF, which shifts a Content-Length body by two bytes.
	Reply request(const int port, const std::string& method, const std::string& body)
	{
		juce::StreamingSocket socket;
		require(socket.connect("127.0.0.1", port, 5000), "MCP HTTP connection failed");
		const auto message = method + " /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
			"Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size())
			+ "\r\n\r\n" + body;
		require(socket.write(message.data(), static_cast<int>(message.size()))
			== static_cast<int>(message.size()), "MCP HTTP request failed");
		std::string data;
		size_t headerEnd = std::string::npos;
		size_t length = 0;
		while(headerEnd == std::string::npos || data.size() < headerEnd + 4 + length)
		{
			require(socket.waitUntilReady(true, 20000) == 1, "MCP HTTP response timed out");
			char buffer[4096];
			const auto count = socket.read(buffer, sizeof(buffer), false);
			require(count > 0, "MCP HTTP response was cut short");
			data.append(buffer, static_cast<size_t>(count));
			if(headerEnd != std::string::npos)
				continue;
			headerEnd = data.find("\r\n\r\n");
			if(headerEnd == std::string::npos)
				continue;
			const auto header = juce::String(data.substr(0, headerEnd)).toLowerCase();
			const auto field = header.indexOf("content-length:");
			if(field >= 0)
				length = static_cast<size_t>(header.substring(field + 15).getIntValue());
		}
		Reply reply;
		reply.status = juce::String(data.substr(0, headerEnd)).fromFirstOccurrenceOf(" ", false, false)
			.getIntValue();
		reply.body = data.substr(headerEnd + 4, length);
		return reply;
	}

	Json post(const int port, const std::string& body)
	{
		const auto reply = request(port, "POST", body);
		require(reply.status == 200, "MCP HTTP request was rejected");
		return Json::parse(reply.body);
	}

	Json call(const int port, const std::string& name, const std::string& arguments)
	{
		const auto response = post(port, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\""
			+ name + "\",\"arguments\":" + arguments + "}}");
		const auto result = response.get("result");
		require(result.isObject() && !result.get("isError").getBool(), "MCP tool failed");
		return Json::parse(result.get("content").getArrayElement(0).get("text").getString().toStdString());
	}
}

// A never-written pattern (H09) as a Monomachine with OS 1.32B sends it.
const std::vector<uint8_t> g_monomachineEmptyPattern = {
		0xf0,0x00,0x20,0x3c,0x03,0x00,0x67,0x06,0x01,0x78,0x55,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x67,0x3b,0x00,0x04,0x2a,0x04,0x00,
		0x04,0x2a,0x5d,0x04,0x00,0x04,0x2a,0x04,0x00,0x04,0x6e,0x2a,0x04,0x00,0x04,0x2a,0x04,0x00,0x77,0x04,0x2a,0x34,0x00,0x04,
		0x2a,0x04,0x3b,0x00,0x04,0x2a,0x04,0x00,0x04,0x2a,0x5d,0x04,0x00,0x04,0x2a,0x04,0x00,0x04,0x6e,0x2a,0x04,0x00,0x04,0x2a,
		0x34,0x00,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x03,0x55,0x7f,0x10,0x39,0x00,0x06,0x05,0x06,0x2a,0x07,0x06,0x08,0x60,0x40,
		0x12,0x00,0x55,0x06,0x05,0x06,0x08,0x60,0x40,0x04,0x6f,0x7f,0x05,0x00,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
		0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
		0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
		0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
		0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x49,0x40,0x7f,0x7b,0x39,0x01,0x47,0xf7,
};

void testMonomachineDumps()
{
	using namespace mdJucePlugin::mnmDump;
	auto dump = unpack(g_monomachineEmptyPattern);
	require(dump.id == g_patternId && dump.position == 120 && dump.data.size() == g_patternSize,
		"Monomachine pattern dump was not decoded to its full size");
	require(dump.data[g_length] == 16 && dump.data[g_lockRowCount] == 0 && trigs(dump.data, AmpTrigs, 0) == 0,
		"Monomachine empty pattern was not decoded correctly");
	require(pack(dump) == g_monomachineEmptyPattern, "Monomachine dump was not re-encoded byte for byte");

	// two locks on track 2 (filter width, then synthesis A given second): rows come out in parameter order
	auto table = readLocks(dump.data);
	table[1][17].assign(g_steps, 0xff);
	table[1][17][4] = 100;
	table[1][0].assign(g_steps, 0xff);
	table[1][0][63] = 0x90 & 0x7f;
	table[3][5].assign(g_steps, 0xff);	// a row without any lock is dropped
	writeLocks(dump.data, table);
	setTrigs(dump.data, AmpTrigs, 1, (uint64_t{1} << 4) | (uint64_t{1} << 63));
	const auto again = unpack(pack(dump));
	require(again.data == dump.data, "Monomachine dump did not survive encoding");
	require(again.data[g_lockRowCount] == 2 && again.data[g_lockBitmaps + 8] == 0x01
		&& again.data[g_lockBitmaps + 8 + 2] == 0x02
		&& again.data[g_lockRows + 63] == 0x10 && again.data[g_lockRows + g_steps + 4] == 100
		&& trigs(again.data, AmpTrigs, 1) == ((uint64_t{1} << 4) | (uint64_t{1} << 63)),
		"Monomachine lock rows or trigs were not laid out as the unit has them");
	auto broken = g_monomachineEmptyPattern;
	broken[20] ^= 1;
	bool rejected = false;
	try { unpack(broken); } catch(const std::runtime_error&) { rejected = true; }
	require(rejected, "A Monomachine dump with a bad checksum was accepted");
}

int main()
{
	try
	{
		juce::ScopedJuceInitialiser_GUI juce;
		testMonomachineDumps();
		mdJucePlugin::AudioPluginAudioProcessor processor(md::MachineModel::Machinedrum,
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{std::string{}}, false);
		juce::AudioProcessor& audioProcessor = processor;
		audioProcessor.setRateAndBufferSizeDetails(48000, 64);
		audioProcessor.prepareToPlay(48000, 64);
		processor.setMcpServerEnabled(true);
		const auto* server = processor.getMcpServer();
		require(server != nullptr && server->isRunning(), "MCP server did not start");
		const auto port = server->getPort();
		const auto initialized = post(port,
			"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1\"}}}");
		require(initialized.get("result").get("protocolVersion").getString() == "2025-03-26",
			"Streamable HTTP negotiated an incompatible protocol");

		const auto listed = post(port,
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{}}");
		const auto tools = listed.get("result").get("tools");
		bool panelFound = false, trigsFound = false, locksFound = false, captureFound = false;
		bool patternFound = false, songFound = false;
		bool patternWriteFound = false, songWriteFound = false;
		for(int i = 0; i < tools.getArraySize(); ++i)
		{
			const auto name = tools.getArrayElement(i).get("name").getString();
			panelFound |= name == "get_machinedrum_panel_state";
			trigsFound |= name == "set_trigs";
			locksFound |= name == "set_parameter_locks";
			captureFound |= name == "start_audio_capture";
			patternFound |= name == "get_pattern";
			songFound |= name == "get_song_rows";
			patternWriteFound |= name == "set_pattern";
			songWriteFound |= name == "set_song_rows";
		}
		require(panelFound && trigsFound && locksFound && captureFound
			&& patternFound && songFound && patternWriteFound && songWriteFound,
			"Machinedrum tools not registered");
		require(request(port, "GET", {}).status == 405, "Stateless MCP GET must return 405");
		const auto notified = request(port, "POST",
			"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
		require(notified.status == 202 && notified.body.empty(),
			"Streamable HTTP notification must return 202 without a body");

		const auto panel = call(port, "get_machinedrum_panel_state", "{}");
		require(panel.hasProperty("selectedTrack") && panel.hasProperty("stepLeds")
			&& panel.hasProperty("lcdText") && panel.hasProperty("transport"),
			"Panel state omits required fields");
		const auto feedDump = [&](const std::vector<uint8_t>& bytes)
		{
			return std::async(std::launch::async, [&, bytes]
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				synthLib::SMidiEvent reply(synthLib::MidiEventSource::Device);
				reply.sysex.assign(bytes.begin(), bytes.end());
				processor.sendToPhysicalOut(reply);
			});
		};
		std::vector<uint8_t> patternDump(0xACB);
		patternDump[0] = 0xf0; patternDump[2] = 0x20; patternDump[3] = 0x3c; patternDump[4] = 0x02;
		patternDump[6] = 0x67; patternDump[7] = 3; patternDump[8] = 1;
		patternDump[9] = 12; patternDump[0x0E] = 0x11;
		patternDump[0xB2] = 16; patternDump[0xB5] = 2; patternDump.back() = 0xf7;
		auto patternFeeder = feedDump(patternDump);
		const auto pattern = call(port, "get_pattern", "{\"pattern\":12}");
		patternFeeder.get();
		require(pattern.get("length").getInt() == 16
			&& pattern.get("tracks").getArrayElement(0).get("trigs").getArraySize() == 2
			&& pattern.get("tracks").getArrayElement(0).get("trigs").getArrayElement(1).getInt() == 5,
			"Pattern dump was not decoded correctly");

		std::vector<uint8_t> songDump(0x1F + 12);
		songDump[0] = 0xf0; songDump[2] = 0x20; songDump[3] = 0x3c; songDump[4] = 0x02;
		songDump[6] = 0x69; songDump[7] = 2; songDump[8] = 2;
		songDump[9] = 1; songDump[0x1A] = 0x40;
		songDump[0x1B] = 0x7f; songDump[0x1C] = 1; songDump[0x1D] = 2;
		songDump[0x21] = 0x0b; songDump[0x23] = 0x40;	// 2880 = 120 BPM * 24
		songDump[0x25] = 15; songDump.back() = 0xf7;
		auto songFeeder = feedDump(songDump);
		const auto song = call(port, "get_song_rows", "{\"song\":1}");
		songFeeder.get();
		require(song.get("rows").getArraySize() == 1
			&& song.get("rows").getArrayElement(0).get("pattern").getInt() == -1
			&& song.get("rows").getArrayElement(0).get("tempo").getDouble() == 120.0,
			"Song dump was not decoded correctly");

		// send_sysex returns what the device answers: only messages matching the prefix, up to maxReplies
		auto replyFeeder = std::async(std::launch::async, [&processor]
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			for(const uint8_t id : {0x72, 0x52, 0x52})
			{
				synthLib::SMidiEvent reply(synthLib::MidiEventSource::Device);
				reply.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, id, 0x05, 0xf7};
				processor.addMidiEvent(reply);
			}
		});
		const auto answered = call(port, "send_sysex", "{\"hex\":\"F0 00 20 3C 02 00 53 05 F7\","
			"\"waitForReplyMs\":3000,\"maxReplies\":2,\"replyPrefix\":\"F0 00 20 3C 02 00 52\"}");
		replyFeeder.get();
		require(answered.get("replies").getArraySize() == 2
			&& answered.get("replies").getArrayElement(0).getString() == "F0 00 20 3C 02 00 52 05 F7",
			"send_sysex did not return the device's replies");
		require(!call(port, "send_sysex", "{\"hex\":\"F0 00 20 3C 02 00 53 05 F7\"}").hasProperty("replies"),
			"send_sysex without a wait should not report replies");

		const auto started = call(port, "start_audio_capture", "{\"seconds\":1}");
		const auto path = started.get("path").getString().toStdString();
		require(!path.empty(), "MCP capture did not return a path");
		std::array<float, 32> left{}, right{};
		left.fill(0.5f);
		right.fill(-0.5f);
		synthLib::TAudioOutputs outputs{};
		outputs[0] = left.data();
		outputs[1] = right.data();
		processor.observeOutput(outputs, left.size(), 48000, false);
		const auto stopped = call(port, "stop_audio_capture", "{}");
		require(stopped.get("path").getString().toStdString() == path,
			"MCP capture returned a different path");
		std::ifstream wav(path, std::ios::binary);
		std::array<char, 44> header{};
		wav.read(header.data(), header.size());
		require(wav.good() && std::string(header.data(), 4) == "RIFF"
			&& std::string(header.data() + 8, 4) == "WAVE"
			&& std::string(header.data() + 36, 4) == "data",
			"MCP capture is not a valid WAV header");
		std::array<unsigned char, 4> firstFrame{};
		wav.read(reinterpret_cast<char*>(firstFrame.data()), firstFrame.size());
		require(wav.good() && firstFrame[0] == 0x00 && firstFrame[1] == 0x40
			&& firstFrame[2] == 0x00 && firstFrame[3] == 0xc0,
			"MCP capture did not contain the synthetic stereo samples");
		wav.close();
		std::remove(path.c_str());
		processor.setMcpServerEnabled(false);
		audioProcessor.releaseResources();
		std::cout << "Machinedrum MCP tools and WAV capture passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
