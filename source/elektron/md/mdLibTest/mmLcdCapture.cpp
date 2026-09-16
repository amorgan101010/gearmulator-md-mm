// Local exploration tool (not upstream): capture Monomachine LCD frames as PBM images.
//
//   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin mmLcdCapture <output directory>
//
// Writes pages-<n>.pbm for DATA pages 0-6 on the boot kit, then machine-<id>.pbm with
// each machine assigned to track 1 and the SYNTHESIS page shown.

#include "../mdJucePlugin/mdLcdText.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto chunk = std::min<uint32_t>(256, _frames);
			_hardware.advance(chunk);
			_frames -= chunk;
		}
	}

	void tap(md::Hardware& _hardware, const md::PanelControl _control)
	{
		const auto packet = md::panelPacket(md::MachineModel::Monomachine, _control);
		if(!packet)
			return;
		_hardware.trySendPanelEvent(packet->row, packet->mask);
		advance(_hardware, 2048);
		_hardware.trySendPanelEvent(packet->row, 0);
		advance(_hardware, md::g_samplerate / 4);
	}

	void capture(const md::Hardware& _hardware, const std::string& _path)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		auto* const file = std::fopen(_path.c_str(), "w");
		if(!file)
			return;
		std::fprintf(file, "P1\n%u %u\n", md::FrontPanel::g_lcdWidth, md::FrontPanel::g_lcdHeight);
		for(uint32_t y = 0; y < md::FrontPanel::g_lcdHeight; ++y)
		{
			for(uint32_t x = 0; x < md::FrontPanel::g_lcdWidth; ++x)
				std::fputc(panel.getLcdPixel(x, y) ? '1' : '0', file);
			std::fputc('\n', file);
		}
		std::fclose(file);
	}
}

int main(int argc, char** argv)
{
	const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(argc < 2 || !path)
	{
		std::cerr << "usage: GEARMULATOR_MM_FIRMWARE_BIN=... mmLcdCapture <dir>\n";
		return 2;
	}
	const std::string out = argv[1];

	std::vector<uint8_t> rom;
	if(!baseLib::filesystem::readFile(rom, path)
		|| !md::RomLoader::isRomForModel(rom, md::MachineModel::Monomachine))
	{
		std::cerr << "firmware missing or not a Monomachine image\n";
		return 2;
	}

	auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Monomachine);
	advance(*hardware, md::g_samplerate * 20);

	// "turncheck" mode: on AMP and LFO 1, turn each knob and read every field label right
	// after each detent, to see whether turning ever hides labels (which would end an LCD drag).
	if(argc >= 3 && std::string(argv[2]) == "turncheck")
	{
		std::vector<uint64_t> expected(8);
		int hidden = 0, checks = 0;
		for(int page = 1; page <= 4; ++page)
		{
			tap(*hardware, md::PanelControl::DataPageForward);
			if(page != 1 && page != 4)
				continue;
			const auto before = hardware->getFrontPanelSnapshot();
			for(unsigned field = 0; field < 8; ++field)
				expected[field] = mdJucePlugin::lcdText::hash(before, mdJucePlugin::lcdText::fieldLabel(field));
			for(unsigned encoder = 0; encoder < 8; ++encoder)
			{
				const auto command = md::panelEncoderCommand(md::MachineModel::Monomachine,
					static_cast<md::PanelEncoder>(encoder));
				for(int step = 0; step < 6; ++step)
				{
					hardware->trySendPanelEvent(*command, step < 3 ? 0x01 : 0xff);
					advance(*hardware, 2048);
					const auto now = hardware->getFrontPanelSnapshot();
					++checks;
					for(unsigned field = 0; field < 8; ++field)
					{
						if(mdJucePlugin::lcdText::hash(now, mdJucePlugin::lcdText::fieldLabel(field)) != expected[field])
						{
							std::printf("page %d knob %u step %d: field %u label changed\n", page, encoder, step, field);
							++hidden;
							capture(*hardware, out + "/turn-p" + std::to_string(page) + "-k" + std::to_string(encoder)
								+ "-s" + std::to_string(step) + ".pbm");
							break;
						}
					}
				}
			}
		}
		std::printf("turncheck: %d label disruptions in %d checks\n", hidden, checks);
		return 0;
	}

	// "lfo" mode: on LFO 1, step PAGE (knob A) through every value and, for each, DEST (knob B)
	// through every value, saving screens and value-line hashes for transcription.
	if(argc >= 3 && std::string(argv[2]) == "lfo")
	{
		const auto turn = [&](const md::PanelEncoder _encoder, const int _steps)
		{
			const auto command = md::panelEncoderCommand(md::MachineModel::Monomachine, _encoder);
			for(int i = 0; i < std::abs(_steps); ++i)
			{
				hardware->trySendPanelEvent(*command, _steps > 0 ? 0x01 : 0xff);
				advance(*hardware, 1024);
			}
			advance(*hardware, md::g_samplerate / 4);
		};
		const auto valueHash = [&](const unsigned _field)
		{
			auto region = mdJucePlugin::lcdText::fieldLabel(_field);
			region.y += 23;	// the value line sits 23 rows below the label on the LFO pages
			return mdJucePlugin::lcdText::hash(hardware->getFrontPanelSnapshot(), region);
		};

		for(int page = 0; page < 4; ++page)
			tap(*hardware, md::PanelControl::DataPageForward);

		// The firmware can swallow the first detent after a change of direction, so a value is
		// only treated as the last one after several single steps leave it unchanged.
		turn(md::PanelEncoder::DataEntryA, -40);
		std::vector<uint64_t> pages;
		for(int attempt = 0, unchanged = 0; attempt < 64 && unchanged < 4; ++attempt)
		{
			const auto pageHash = valueHash(0);
			if(!pages.empty() && pageHash == pages.back())
			{
				++unchanged;
				turn(md::PanelEncoder::DataEntryA, 1);
				continue;
			}
			unchanged = 0;
			const auto p = static_cast<int>(pages.size());
			pages.push_back(pageHash);
			capture(*hardware, out + "/lfo-p" + std::to_string(p) + ".pbm");
			std::printf("page %d 0x%016llx\n", p, static_cast<unsigned long long>(pageHash));

			turn(md::PanelEncoder::DataEntryB, -40);
			std::vector<uint64_t> dests;
			for(int dAttempt = 0, dUnchanged = 0; dAttempt < 64 && dUnchanged < 4; ++dAttempt)
			{
				const auto destHash = valueHash(1);
				if(!dests.empty() && destHash == dests.back())
				{
					++dUnchanged;
					turn(md::PanelEncoder::DataEntryB, 1);
					continue;
				}
				dUnchanged = 0;
				const auto d = static_cast<int>(dests.size());
				dests.push_back(destHash);
				capture(*hardware, out + "/lfo-p" + std::to_string(p) + "-d" + std::to_string(d) + ".pbm");
				std::printf("  dest %d 0x%016llx\n", d, static_cast<unsigned long long>(destHash));
				turn(md::PanelEncoder::DataEntryB, 1);
			}
			turn(md::PanelEncoder::DataEntryA, 1);
		}
		return 0;
	}

	capture(*hardware, out + "/boot.pbm");
	std::vector<md::FrontPanel> pagePanels;
	for(int page = 1; page <= 6; ++page)
	{
		tap(*hardware, md::PanelControl::DataPageForward);
		capture(*hardware, out + "/page-" + std::to_string(page) + ".pbm");
		pagePanels.push_back(hardware->getFrontPanelSnapshot());
	}
	for(int page = 0; page < 6; ++page)
		tap(*hardware, md::PanelControl::DataPageBackward);
	capture(*hardware, out + "/page-0.pbm");

	// SYNTHESIS page labels transcribed from these captures, checked against Appendix A of
	// the owner's manual. Order is fields A-D then E-H; "" is an empty field.
	struct Machine
	{
		int id;
		const char* labels[8];
	};
	constexpr Machine machines[] =
	{
		{ 0, { "", "", "", "", "", "", "", "" } },
		{ 1, { "", "", "", "", "", "", "", "TUNE" } },
		{ 2, { "ST", "RED", "STON", "", "", "", "", "TUNE" } },
		{ 3, { "PW", "PWAD", "PWRS", "WAVE", "MOD", "MSRC", "MFRQ", "TUNE" } },
		{ 4, { "UNIL", "UNIW", "UNIX", "", "SUBX", "SUB1", "SUB2", "TUNE" } },
		{ 5, { "UNIL", "UNIW", "SUB1", "SUB2", "PW", "PWAD", "PWRS", "TUNE" } },
		{ 6, { "WAVE", "WP", "WPM", "WPRS", "SYNC", "SFRQ", "", "TUNE" } },
		{ 7, { "PTCH", "STRT", "", "", "RTRG", "RTIM", "", "" } },
		{ 8, { "1FRQ", "1FIN", "1ENV", "1FB", "2FRQ", "2VOL", "TONE", "TUNE" } },
		{ 9, { "1FRQ", "1ENV", "2FRQ", "2ENV", "3FRQ", "3ENV", "TONE", "TUNE" } },
		{ 10, { "1FRQ", "1FEN", "1VOL", "1VEN", "2FRQ", "2ENV", "2FB", "TUNE" } },
		{ 11, { "VOC1", "VOC2", "V-SW", "VOIC", "CONS", "CLEN", "CVOL", "TUNE" } },
		{ 12, { "", "", "", "", "", "", "", "INP" } },
		{ 13, { "DEC", "DAMP", "GATE", "MIX", "HP", "LP", "", "INP" } },
		{ 14, { "PCH2", "PCH3", "PCH4", "WAVE", "PW", "CHRL", "CHRW", "TUNE" } },
		{ 15, { "DEL", "DEP", "SPD", "MIX", "FB", "WID", "LP", "INP" } },
		{ 16, { "ATK", "REL", "THRS", "MIX", "RAT", "GAIN", "RMS", "INP" } },
		{ 17, { "WAVE", "EXT", "", "MIX", "", "", "", "INP" } },
		{ 18, { "CNTR", "DEP", "SPD", "MIX", "FB", "WID", "", "INP" } },
		{ 19, { "DEL", "DEP", "SPD", "MIX", "FB", "WID", "", "INP" } },
		{ 32, { "WAV1", "MIX", "WAV2", "TIME", "BR1", "WID", "BR2", "TUNE" } },
		{ 33, { "PCH2", "PCH3", "PCH4", "WAVE", "", "CHRL", "CHRW", "TUNE" } },
	};

	std::vector<std::pair<uint64_t, std::string>> labelHashes;
	std::vector<std::pair<uint64_t, int>> machineHashes;
	int errors = 0;

	const auto verifyLabels = [&](const md::FrontPanel& _panel, const char* const* _labels, const std::string& _context)
	{
		for(unsigned field = 0; field < 8; ++field)
		{
			const auto region = mdJucePlugin::lcdText::fieldLabel(field);
			const std::string label = _labels[field];
			const auto isBlank = mdJucePlugin::lcdText::blank(_panel, region);
			if(label.empty() != isBlank)
			{
				std::cerr << _context << " field " << field << ": transcription '" << label
					<< "' but region is " << (isBlank ? "blank" : "not blank") << '\n';
				++errors;
				continue;
			}
			if(isBlank)
				continue;
			const auto hash = mdJucePlugin::lcdText::hash(_panel, region);
			const auto it = std::find_if(labelHashes.begin(), labelHashes.end(),
				[&](const auto& _e) { return _e.first == hash || _e.second == label; });
			if(it == labelHashes.end())
				labelHashes.emplace_back(hash, label);
			else if(it->first != hash || it->second != label)
			{
				std::cerr << _context << " field " << field << ": '" << label << "' conflicts with '"
					<< it->second << "' (same " << (it->first == hash ? "pixels" : "text, different pixels") << ")\n";
				++errors;
			}
		}
	};

	// Fixed DATA pages 1-6 on the boot kit, transcribed from the captures.
	constexpr const char* pageLabels[6][8] =
	{
		{ "ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT" },
		{ "BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS" },
		{ "EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID" },
		{ "PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH" },
		{ "PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH" },
		{ "PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH" },
	};
	for(size_t page = 0; page < pagePanels.size(); ++page)
		verifyLabels(pagePanels[page], pageLabels[page], "page " + std::to_string(page + 1));

	for(const auto& machine : machines)
	{
		// Manual, MIDI spec: $5b load machine -- track, machine number, 1 = init all data pages.
		synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
		assign.sysex = { 0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x5b, 0x00, static_cast<uint8_t>(machine.id), 0x01, 0xf7 };
		hardware->sendMidi(assign);
		advance(*hardware, md::g_samplerate);
		capture(*hardware, out + "/machine-" + std::to_string(machine.id) + ".pbm");

		const auto panel = hardware->getFrontPanelSnapshot();
		verifyLabels(panel, machine.labels, "machine " + std::to_string(machine.id));

		const auto nameHash = mdJucePlugin::lcdText::hash(panel, mdJucePlugin::lcdText::g_machineName);
		for(const auto& [hash, id] : machineHashes)
		{
			if(hash == nameHash)
			{
				std::cerr << "machine " << machine.id << " name looks identical to machine " << id << '\n';
				++errors;
			}
		}
		machineHashes.emplace_back(nameHash, machine.id);
	}

	std::printf("// Generated by mmLcdCapture from Monomachine OS 1.32b screens.\n");
	for(const auto& [hash, label] : labelHashes)
		std::printf("{ 0x%016llxull, \"%s\" },\n", static_cast<unsigned long long>(hash), label.c_str());
	std::printf("// machine names\n");
	for(const auto& [hash, id] : machineHashes)
		std::printf("{ 0x%016llxull, %d },\n", static_cast<unsigned long long>(hash), id);

	std::cerr << "captured to " << out << ", " << labelHashes.size() << " distinct labels, " << errors << " errors\n";
	return errors == 0 ? 0 : 1;
}
