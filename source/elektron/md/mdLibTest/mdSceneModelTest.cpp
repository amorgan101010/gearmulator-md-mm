#include "mdLib/mdscene.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	void require(const bool _condition, const char* _message)
	{
		if(_condition)
			return;
		std::cerr << _message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

int main()
{
	using md::scene::Address;
	using md::scene::Bank;
	using md::scene::Store;
	const Address address{2, 1, 3};
	Bank bank;
	require(bank.sceneA == 0 && bank.sceneB == 15,
		"new scene banks must default to A 01 and B 16");
	require(bank.assign(true, 1), "scene B could not be assigned to scene 02");
	require(bank.setBase(address, 40), "valid base value rejected");
	require(bank.morphedValue(address, 40) == 40,
		"empty assigned scenes must preserve the base value");
	require(bank.setLock(0, address, 100), "valid scene lock rejected");
	require(bank.setLock(1, address, 0), "zero must be a valid scene lock");
	require(bank.morphedValue(address, 40) == 100,
		"fader A endpoint did not select scene A");
	bank.fader = 64;
	require(bank.morphedValue(address, 40) == 50,
		"midpoint interpolation did not round the expected value");
	bank.fader = 127;
	require(bank.morphedValue(address, 40) == 0,
		"fader B endpoint did not select the zero-valued lock");
	bank.muteA = true;
	require(bank.morphedValue(address, 40) == 0,
		"muted A side did not use the clean base value");
	bank.fader = 0;
	require(bank.morphedValue(address, 40) == 40,
		"muted A endpoint did not return to the clean base value");
	bank.muteB = true;
	require(bank.morphedValue(address, 40) == 40,
		"muting both scene sides did not return to the clean base value");
	bank.muteA = false;
	bank.muteB = false;
	bank.fader = 64;
	require(bank.clearLock(1, address), "scene lock could not be cleared");
	require(bank.setLock(1, address, 0), "scene lock setup for full clear failed");
	require(bank.setLock(1, Address{2, 3, 4}, 99), "second scene lock setup failed");
	require(bank.clearScene(1) && !bank.sceneHasLocks(1)
		&& bank.sceneB == 1,
		"clearing a scene did not remove all its locks and preserve its assignment");
	require(bank.morphedValue(address, 40) == 70,
		"an unlocked side did not fall back to the base value");

	Store store;
	store.kits.emplace(3, bank);
	auto mutedBank = bank;
	mutedBank.muteA = true;
	mutedBank.muteB = true;
	store.kits.emplace(127, mutedBank);
	const auto encoded = store.encode();
	Store decoded;
	require(!encoded.empty() && Store::decode(encoded, decoded),
		"version 3 scene snapshot failed to round-trip");
	require(decoded.kits.count(127) == 1,
		"scene snapshot did not preserve the last Monomachine Kit bank");
	require(decoded.kits.at(127).muteA && decoded.kits.at(127).muteB,
		"scene snapshot did not preserve A/B mute state");
	const auto& restored = decoded.kits.at(3);
	require(restored.getBase(address) == 40 && restored.sceneA == 0
		&& restored.sceneB == 1 && restored.fader == 64
		&& !restored.muteA && !restored.muteB,
		"scene base or assignment state changed during round-trip");
	require(restored.getLock(0, address) == 100
		&& restored.getLock(1, address) == -1,
		"scene locks changed during round-trip");

	const std::vector<uint8_t> version2{2, 1, 3, 2, 5, 64, 0, 0, 0, 0};
	Store migratedV2;
	require(Store::decode(version2, migratedV2)
		&& !migratedV2.kits.at(3).muteA && !migratedV2.kits.at(3).muteB,
		"version 2 scenes did not migrate with both sides unmuted");

	// Version 1 stored only assignments, fader, and scene locks.
	const std::vector<uint8_t> version1{
		1, 1, 3, 2, 5, 64, 0, 1, 2, 2, 1, 3, 45};
	Store migrated;
	require(Store::decode(version1, migrated), "version 1 snapshot was rejected");
	require(migrated.kits.at(3).getBase(address) == -1
		&& migrated.kits.at(3).getLock(2, address) == 45,
		"version 1 snapshot did not migrate as sparse locks without a base");

	auto malformed = encoded;
	malformed.push_back(0);
	Store untouched;
	require(!Store::decode(malformed, untouched),
		"trailing bytes in a scene snapshot were accepted");
	return EXIT_SUCCESS;
}
