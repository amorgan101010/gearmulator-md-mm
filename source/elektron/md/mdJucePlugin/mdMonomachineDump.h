#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Monomachine kit and pattern SysEx dumps (OS 1.32): the wire encoding and where things sit in the decoded data.
//
// A dump is F0 00 20 3C 03 00 <id> <version> <revision> <position> <data> <checksum hi lo> <length hi lo> F7.
// The data is run-length encoded (a byte with bit 7 set is a count, the next byte the value; a single byte with
// bit 7 clear stands for itself), then packed 7 bytes to 8 with the high bits in a leading byte. The layout of
// the decoded data follows MidiCtrl's MNM library where that matched dumps from the unit; the lock bitmap and
// the seventh trig group did not, and are described below as the unit has them.
namespace mdJucePlugin::mnmDump
{
	constexpr uint8_t g_kitId = 0x52;
	constexpr uint8_t g_kitRequestId = 0x53;
	constexpr uint8_t g_patternId = 0x67;
	constexpr uint8_t g_patternRequestId = 0x68;

	constexpr size_t g_tracks = 6;
	constexpr size_t g_steps = 64;

	// Pattern data, 6520 bytes. Trig groups: 13 groups of 6 tracks, each a big-endian 64-bit word with
	// bit n = step n + 1.
	constexpr size_t g_patternSize = 6520;
	enum TrigGroup : size_t
	{
		AmpTrigs = 0,
		FilterTrigs = 1,
		LfoTrigs = 2,
		NoteOffs = 3,
		LockSteps = 6,		// every step that carries a parameter lock or a note
		Slides = 9,
	};
	constexpr size_t g_lockBitmaps = 628;		// per track 8 bytes: byte = data page, bit = parameter on that page
	constexpr size_t g_notes = 676;				// per track 64 bytes: note number, 0xff = none
	constexpr size_t g_length = 1060;
	constexpr size_t g_tempoMultiplier = 1061;
	constexpr size_t g_kit = 1062;
	constexpr size_t g_unusedFlags = 1358;		// 4 bytes: ff in a never-written pattern, 00 in every used one seen
	constexpr size_t g_lockRowCount = 1366;
	constexpr size_t g_lockRows = 1367;			// 62 rows of 64 step values (0xff = no lock), by track, then parameter
	constexpr size_t g_maxLockRows = 62;
	constexpr size_t g_lockableParameters = 64;

	// Kit data, 698 bytes.
	constexpr size_t g_kitSize = 698;
	constexpr size_t g_kitName = 0;				// 10 characters and a terminator
	constexpr size_t g_kitNameLength = 10;
	constexpr size_t g_kitLevels = 11;
	constexpr size_t g_kitParameters = 17;		// per track 72 bytes: synthesis, amp, filter, effects, LFO 1-3 (8 each), 16 more
	constexpr size_t g_kitParametersPerTrack = 72;
	constexpr size_t g_kitMachines = 449;

	struct Dump
	{
		uint8_t id = 0;
		uint8_t version = 0;
		uint8_t revision = 0;
		uint8_t position = 0;
		std::vector<uint8_t> data;
	};

	inline Dump unpack(const std::vector<uint8_t>& _message)
	{
		if(_message.size() < 16 || _message[0] != 0xf0 || _message[1] != 0x00 || _message[2] != 0x20
			|| _message[3] != 0x3c || _message[4] != 0x03 || _message.back() != 0xf7)
			throw std::runtime_error("Not a Monomachine dump");
		const auto end = _message.size() - 5;
		unsigned checksum = 0;
		for(size_t i = 9; i < end; ++i)
			checksum += _message[i];
		if((checksum & 0x3fff) != static_cast<unsigned>((_message[end] << 7) | _message[end + 1])
			|| _message.size() - 10 != static_cast<size_t>((_message[end + 2] << 7) | _message[end + 3]))
			throw std::runtime_error("Monomachine dump has a wrong checksum or length");

		std::vector<uint8_t> packed;
		for(size_t i = 10; i < end; i += 8)
		{
			const auto highBits = _message[i];
			for(size_t j = 0; j < 7 && i + 1 + j < end; ++j)
				packed.push_back(static_cast<uint8_t>(_message[i + 1 + j] | (((highBits >> (6 - j)) & 1u) << 7)));
		}

		Dump dump{_message[6], _message[7], _message[8], _message[9], {}};
		for(size_t i = 0; i < packed.size(); ++i)
		{
			if(!(packed[i] & 0x80))
			{
				dump.data.push_back(packed[i]);
				continue;
			}
			if(i + 1 >= packed.size())
				throw std::runtime_error("Truncated Monomachine dump");
			dump.data.insert(dump.data.end(), packed[i] & 0x7f, packed[i + 1]);
			++i;
		}
		return dump;
	}

	// Byte for byte what the unit sends for the same data.
	inline std::vector<uint8_t> pack(const Dump& _dump)
	{
		std::vector<uint8_t> packed;
		for(size_t i = 0; i < _dump.data.size();)
		{
			const auto value = _dump.data[i];
			size_t run = 1;
			while(i + run < _dump.data.size() && _dump.data[i + run] == value && run < 127)
				++run;
			if(run == 1 && !(value & 0x80))
				packed.push_back(value);
			else
			{
				packed.push_back(static_cast<uint8_t>(0x80 | run));
				packed.push_back(value);
			}
			i += run;
		}

		std::vector<uint8_t> message{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			_dump.id, _dump.version, _dump.revision, _dump.position};
		for(size_t i = 0; i < packed.size(); i += 7)
		{
			uint8_t highBits = 0;
			for(size_t j = 0; j < 7 && i + j < packed.size(); ++j)
				highBits |= static_cast<uint8_t>((packed[i + j] >> 7) << (6 - j));
			message.push_back(highBits);
			for(size_t j = 0; j < 7 && i + j < packed.size(); ++j)
				message.push_back(packed[i + j] & 0x7f);
		}
		if(packed.size() % 7 == 0)
			message.push_back(0);	// the unit closes a full group with an empty header byte

		unsigned checksum = 0;
		for(size_t i = 9; i < message.size(); ++i)
			checksum += message[i];
		const auto length = message.size() + 5 - 10;
		message.push_back(static_cast<uint8_t>((checksum >> 7) & 0x7f));
		message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		message.push_back(static_cast<uint8_t>((length >> 7) & 0x7f));
		message.push_back(static_cast<uint8_t>(length & 0x7f));
		message.push_back(0xf7);
		return message;
	}

	inline uint64_t trigs(const std::vector<uint8_t>& _data, const size_t _group, const size_t _track)
	{
		uint64_t word = 0;
		for(size_t i = 0; i < 8; ++i)
			word = (word << 8) | _data[_group * 48 + _track * 8 + i];
		return word;
	}

	inline void setTrigs(std::vector<uint8_t>& _data, const size_t _group, const size_t _track, const uint64_t _word)
	{
		for(size_t i = 0; i < 8; ++i)
			_data[_group * 48 + _track * 8 + i] = static_cast<uint8_t>(_word >> (8 * (7 - i)));
	}

	// The lock rows of a pattern as a table: [track][parameter] -> 64 step values, empty when the parameter
	// has no row.
	using LockTable = std::array<std::array<std::vector<uint8_t>, g_lockableParameters>, g_tracks>;

	inline LockTable readLocks(const std::vector<uint8_t>& _data)
	{
		LockTable table;
		size_t row = 0;
		for(size_t track = 0; track < g_tracks; ++track)
		{
			for(size_t parameter = 0; parameter < g_lockableParameters; ++parameter)
			{
				if(!((_data[g_lockBitmaps + track * 8 + parameter / 8] >> (parameter % 8)) & 1u))
					continue;
				if(row >= g_maxLockRows)
					throw std::runtime_error("Monomachine pattern has more lock rows than it can hold");
				const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(g_lockRows + row * g_steps);
				table[track][parameter].assign(begin, begin + g_steps);
				++row;
			}
		}
		return table;
	}

	// Writes the table back: drops rows without any lock, and rebuilds the bitmaps, the row count and the rows.
	inline void writeLocks(std::vector<uint8_t>& _data, const LockTable& _table)
	{
		size_t row = 0;
		std::fill(_data.begin() + g_lockBitmaps, _data.begin() + g_lockBitmaps + g_tracks * 8, uint8_t{0});
		std::fill(_data.begin() + g_lockRows, _data.begin() + g_lockRows + g_maxLockRows * g_steps, uint8_t{0xff});
		for(size_t track = 0; track < g_tracks; ++track)
		{
			for(size_t parameter = 0; parameter < g_lockableParameters; ++parameter)
			{
				const auto& values = _table[track][parameter];
				bool used = false;
				for(const auto value : values)
					used |= value != 0xff;
				if(!used)
					continue;
				if(row >= g_maxLockRows)
					throw std::runtime_error("A Monomachine pattern holds at most 62 locked parameters (track and parameter pairs)");
				_data[g_lockBitmaps + track * 8 + parameter / 8] |= static_cast<uint8_t>(1u << (parameter % 8));
				std::copy(values.begin(), values.end(), _data.begin() + static_cast<std::ptrdiff_t>(g_lockRows + row * g_steps));
				++row;
			}
		}
		_data[g_lockRowCount] = static_cast<uint8_t>(row);
	}
}
