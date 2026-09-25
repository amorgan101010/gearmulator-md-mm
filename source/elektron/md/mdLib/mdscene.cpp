#include "mdscene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace md::scene
{
	namespace
	{
		constexpr uint8_t g_version = 3;
		constexpr size_t g_headerSize = 2;
		constexpr size_t g_bankHeaderSize = 8;
		constexpr size_t g_lockSize = 5;
		constexpr size_t g_baseValueSize = 4;
		constexpr size_t g_maxBanks = 128;
		constexpr size_t g_maxLocksPerBank = 16 * 16 * 16 * 8;

		void append16(std::vector<uint8_t>& _out, const uint16_t _value)
		{
			_out.push_back(static_cast<uint8_t>(_value >> 8));
			_out.push_back(static_cast<uint8_t>(_value));
		}

		uint16_t read16(const std::vector<uint8_t>& _data, const size_t _offset)
		{
			return static_cast<uint16_t>((static_cast<uint16_t>(_data[_offset]) << 8)
				| _data[_offset + 1]);
		}
	}

	bool Bank::assign(const bool _sideB, const uint8_t _scene)
	{
		if(_scene >= scenes.size())
			return false;
		(_sideB ? sceneB : sceneA) = _scene;
		return true;
	}

	bool Bank::setLock(const uint8_t _scene, const Address _address, const uint8_t _value)
	{
		if(_scene >= scenes.size() || _address.track >= 16 || _address.page >= 16
			|| _address.index >= 8 || _value > 127)
			return false;
		scenes[_scene].locks[_address] = _value;
		return true;
	}

	bool Bank::clearLock(const uint8_t _scene, const Address _address)
	{
		if(_scene >= scenes.size())
			return false;
		return scenes[_scene].locks.erase(_address) != 0;
	}

	bool Bank::clearScene(const uint8_t _scene)
	{
		if(_scene >= scenes.size())
			return false;
		scenes[_scene].locks.clear();
		return true;
	}

	bool Bank::setBase(const Address _address, const uint8_t _value)
	{
		if(_address.track >= 16 || _address.page >= 16 || _address.index >= 8
			|| _value > 127)
			return false;
		baseValues[_address] = _value;
		return true;
	}

	int Bank::getBase(const Address _address) const
	{
		const auto found = baseValues.find(_address);
		return found == baseValues.end() ? -1 : found->second;
	}

	int Bank::getLock(const uint8_t _scene, const Address _address) const
	{
		if(_scene >= scenes.size())
			return -1;
		const auto found = scenes[_scene].locks.find(_address);
		return found == scenes[_scene].locks.end() ? -1 : found->second;
	}

	int Bank::value(const uint8_t _scene, const Address _address,
		const uint8_t _base) const
	{
		const auto locked = getLock(_scene, _address);
		return locked < 0 ? _base : locked;
	}

	int Bank::morphedValue(const Address _address, const uint8_t _base) const
	{
		const auto a = muteA ? _base : value(sceneA, _address, _base);
		const auto b = muteB ? _base : value(sceneB, _address, _base);
		return static_cast<int>(std::lround(a + (b - a) * (fader / 127.0)));
	}

	bool Bank::sceneHasLocks(const uint8_t _scene) const
	{
		return _scene < scenes.size() && !scenes[_scene].locks.empty();
	}

	std::vector<uint8_t> Store::encode() const
	{
		if(kits.size() > g_maxBanks)
			return {};
		std::vector<uint8_t> result;
		result.reserve(g_headerSize + kits.size() * g_bankHeaderSize);
		result.push_back(g_version);
		result.push_back(static_cast<uint8_t>(kits.size()));
		for(const auto& [kit, bank] : kits)
		{
			size_t lockCount = 0;
			for(const auto& scene : bank.scenes)
				lockCount += scene.locks.size();
			if(kit >= g_maxBanks || bank.sceneA >= 16 || bank.sceneB >= 16
				|| bank.fader > 127 || lockCount > g_maxLocksPerBank
				|| lockCount > std::numeric_limits<uint16_t>::max()
				|| bank.baseValues.size() > std::numeric_limits<uint16_t>::max())
				return {};
			result.push_back(kit);
			result.push_back(static_cast<uint8_t>(bank.sceneA | (bank.muteA ? 0x80 : 0)));
			result.push_back(static_cast<uint8_t>(bank.sceneB | (bank.muteB ? 0x80 : 0)));
			result.push_back(bank.fader);
			append16(result, static_cast<uint16_t>(bank.baseValues.size()));
			append16(result, static_cast<uint16_t>(lockCount));
			for(const auto& [address, value] : bank.baseValues)
			{
				if(address.track >= 16 || address.page >= 16 || address.index >= 8
					|| value > 127)
					return {};
				result.push_back(address.track);
				result.push_back(address.page);
				result.push_back(address.index);
				result.push_back(value);
			}
			for(uint8_t sceneIndex = 0; sceneIndex < bank.scenes.size(); ++sceneIndex)
			{
				for(const auto& [address, value] : bank.scenes[sceneIndex].locks)
				{
					if(address.track >= 16 || address.page >= 16 || address.index >= 8
						|| value > 127)
						return {};
					result.push_back(sceneIndex);
					result.push_back(address.track);
					result.push_back(address.page);
					result.push_back(address.index);
					result.push_back(value);
				}
			}
		}
		return result;
	}

	bool Store::decode(const std::vector<uint8_t>& _data, Store& _result)
	{
		if(_data.size() < g_headerSize || (_data[0] < 1 || _data[0] > g_version)
			|| _data[1] > g_maxBanks)
			return false;
		const auto version = _data[0];
		Store candidate;
		size_t offset = g_headerSize;
		for(uint8_t bankIndex = 0; bankIndex < _data[1]; ++bankIndex)
		{
			const auto bankHeaderSize = version == 1 ? size_t{6} : g_bankHeaderSize;
			if(offset + bankHeaderSize > _data.size())
				return false;
			const auto kit = _data[offset++];
			Bank bank;
			const auto sceneA = _data[offset++];
			const auto sceneB = _data[offset++];
			if(version >= 3)
			{
				if((sceneA & 0x70) != 0 || (sceneB & 0x70) != 0)
					return false;
				bank.sceneA = sceneA & 0x0f;
				bank.sceneB = sceneB & 0x0f;
				bank.muteA = (sceneA & 0x80) != 0;
				bank.muteB = (sceneB & 0x80) != 0;
			}
			else
			{
				bank.sceneA = sceneA;
				bank.sceneB = sceneB;
			}
			bank.fader = _data[offset++];
			const auto baseCount = version == 1 ? uint16_t{0} : read16(_data, offset);
			if(version != 1)
				offset += 2;
			const auto lockCount = read16(_data, offset);
			offset += 2;
			if(kit >= g_maxBanks || bank.sceneA >= 16 || bank.sceneB >= 16
				|| bank.fader > 127 || lockCount > g_maxLocksPerBank
				|| offset + static_cast<size_t>(baseCount) * g_baseValueSize
					+ static_cast<size_t>(lockCount) * g_lockSize > _data.size()
				|| candidate.kits.find(kit) != candidate.kits.end())
				return false;
			for(uint16_t i = 0; i < baseCount; ++i)
			{
				const Address address{_data[offset++], _data[offset++], _data[offset++]};
				const auto value = _data[offset++];
				if(address.track >= 16 || address.page >= 16 || address.index >= 8
					|| value > 127 || !bank.baseValues.emplace(address, value).second)
					return false;
			}
			for(uint16_t i = 0; i < lockCount; ++i)
			{
				const auto scene = _data[offset++];
				const Address address{_data[offset++], _data[offset++], _data[offset++]};
				const auto value = _data[offset++];
				if(scene >= 16 || address.track >= 16 || address.page >= 16
					|| address.index >= 8 || value > 127
					|| !bank.scenes[scene].locks.emplace(address, value).second)
					return false;
			}
			candidate.kits.emplace(kit, std::move(bank));
		}
		if(offset != _data.size())
			return false;
		_result = std::move(candidate);
		return true;
	}
}
