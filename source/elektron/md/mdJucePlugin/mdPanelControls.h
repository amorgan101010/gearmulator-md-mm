#pragma once

#include "mdLib/mdtypes.h"

namespace mdJucePlugin
{
	struct PanelButtonBinding
	{
		const char* id;
		md::PanelControl control;
	};

	// The physical panel control-to-skin mapping shared by both JUCE frontends.
	inline constexpr PanelButtonBinding g_panelButtons[] =
	{
		{ "trigKey0", md::PanelControl::Trigger1 }, { "trigKey1", md::PanelControl::Trigger2 },
		{ "trigKey2", md::PanelControl::Trigger3 }, { "trigKey3", md::PanelControl::Trigger4 },
		{ "trigKey4", md::PanelControl::Trigger5 }, { "trigKey5", md::PanelControl::Trigger6 },
		{ "trigKey6", md::PanelControl::Trigger7 }, { "trigKey7", md::PanelControl::Trigger8 },
		{ "trigKey8", md::PanelControl::Trigger9 }, { "trigKey9", md::PanelControl::Trigger10 },
		{ "trigKey10", md::PanelControl::Trigger11 }, { "trigKey11", md::PanelControl::Trigger12 },
		{ "trigKey12", md::PanelControl::Trigger13 }, { "trigKey13", md::PanelControl::Trigger14 },
		{ "trigKey14", md::PanelControl::Trigger15 }, { "trigKey15", md::PanelControl::Trigger16 },
		{ "btTempo", md::PanelControl::Tempo },
		{ "btRec", md::PanelControl::Record },
		{ "btPlay", md::PanelControl::Play },
		{ "btStop", md::PanelControl::Stop },
		{ "btSynth", md::PanelControl::SynthesisEffectsRouting },
		{ "btPattern", md::PanelControl::PatternSong },
		{ "btKit", md::PanelControl::Kit },
		{ "btScale", md::PanelControl::Scale },
		{ "btExit", md::PanelControl::Exit },
		{ "btLeft", md::PanelControl::Left },
		{ "btDown", md::PanelControl::Down },
		{ "btRight", md::PanelControl::Right },
		{ "btClassic", md::PanelControl::ClassicExtended },
		{ "btFunction", md::PanelControl::Function },
		{ "btBankGrp", md::PanelControl::BankGroup },
		{ "btEnter", md::PanelControl::Enter },
		{ "btUp", md::PanelControl::Up },
		{ "btTrigSelect", md::PanelControl::TrigSelect },
		{ "btSongEnable", md::PanelControl::SongEnable },
		{ "btDataNext", md::PanelControl::DataPageForward },
		{ "btDataPrev", md::PanelControl::DataPageBackward },
		{ "btBankA", md::PanelControl::BankA },
		{ "btBankB", md::PanelControl::BankB },
		{ "btBankC", md::PanelControl::BankC },
		{ "btBankD", md::PanelControl::BankD },
		{ "btTrack1", md::PanelControl::Track1 },
		{ "btTrack2", md::PanelControl::Track2 },
		{ "btTrack3", md::PanelControl::Track3 },
		{ "btTrack4", md::PanelControl::Track4 },
		{ "btTrack5", md::PanelControl::Track5 },
		{ "btTrack6", md::PanelControl::Track6 },
	};
}
