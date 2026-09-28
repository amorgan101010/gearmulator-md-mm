#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace md::scene
{
	struct Address
	{
		uint8_t track = 0;
		uint8_t page = 0;
		uint8_t index = 0;

		constexpr bool operator<(const Address& _other) const
		{
			if(track != _other.track) return track < _other.track;
			if(page != _other.page) return page < _other.page;
			return index < _other.index;
		}
	};

	struct Scene
	{
		std::map<Address, uint8_t> locks;
	};

	struct Bank
	{
		std::array<Scene, 16> scenes;
		// Last clean Kit values, captured from a coherent Kit dump and explicit
		// editor/DAW writes. Scene MIDI output never changes this snapshot.
		std::map<Address, uint8_t> baseValues;
		uint8_t sceneA = 0;
		uint8_t sceneB = 15;
		uint8_t fader = 0;
		bool muteA = false;
		bool muteB = false;

		bool assign(bool _sideB, uint8_t _scene);
		bool setLock(uint8_t _scene, Address _address, uint8_t _value);
		bool clearLock(uint8_t _scene, Address _address);
		bool clearScene(uint8_t _scene);
		bool setBase(Address _address, uint8_t _value);
		int getBase(Address _address) const;
		int getLock(uint8_t _scene, Address _address) const;
		int value(uint8_t _scene, Address _address, uint8_t _base) const;
		int morphedValue(Address _address, uint8_t _base) const;
		bool sceneHasLocks(uint8_t _scene) const;
	};

	struct Store
	{
		std::map<uint8_t, Bank> kits;

		std::vector<uint8_t> encode() const;
		static bool decode(const std::vector<uint8_t>& _data, Store& _result);
	};
}
