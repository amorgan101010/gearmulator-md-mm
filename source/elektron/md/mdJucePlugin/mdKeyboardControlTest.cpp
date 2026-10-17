#include "mdKeyboardControl.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	struct Action
	{
		std::string name;
		md::PanelControl control{};
		md::PanelEncoder encoder{};
		float direction = 0.0f;
	};

	class FakeActions final : public mdJucePlugin::PanelKeyboardActions
	{
	public:
		md::MachineModel model = md::MachineModel::Machinedrum;
		md::PanelRowState rows;
		std::vector<md::PanelPacket> packets;

		void keyboardPressControl(md::PanelControl control, bool, bool) override
		{
			actions.push_back({"down", control});
			if(const auto packet = md::panelPacket(model, control))
				packets.push_back(rows.press(*packet));
		}
		void keyboardReleaseControl(md::PanelControl control) override
		{
			actions.push_back({"up", control});
			if(const auto packet = md::panelPacket(model, control))
				packets.push_back(rows.release(*packet));
		}
		bool keyboardEncoderAvailable(md::PanelEncoder) const override { return true; }
		void keyboardSelectEncoder(std::optional<md::PanelEncoder>) override {}
		void keyboardPressEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"encoder-down", {}, encoder});
			if(const auto packet = md::panelEncoderPressPacket(model, encoder))
				packets.push_back(rows.press(*packet));
		}
		void keyboardReleaseEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"encoder-up", {}, encoder});
			if(const auto packet = md::panelEncoderPressPacket(model, encoder))
				packets.push_back(rows.release(*packet));
		}
		void keyboardTurnEncoder(md::PanelEncoder encoder, float direction) override
		{
			actions.push_back({"turn", {}, encoder, direction});
			if(const auto command = md::panelEncoderCommand(model, encoder))
				packets.push_back({*command, static_cast<uint8_t>(direction > 0.0f ? 1 : 0xff)});
		}
		void keyboardClickEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"click", {}, encoder});
			if(const auto packet = md::panelEncoderPressPacket(model, encoder))
			{
				packets.push_back(rows.press(*packet));
				packets.push_back(rows.release(*packet));
			}
		}
		bool keyboardHandlesMachinedrumPageKeys() const override
		{
			return model == md::MachineModel::Machinedrum;
		}
		void keyboardMachinedrumPageKey(int direction, bool down) override
		{
			actions.push_back({down ? "page-down" : "page-up",
				direction > 0 ? md::PanelControl::DataPageForward : md::PanelControl::DataPageBackward});
		}
		void keyboardReleaseAllPanelInputs() override { actions.push_back({"release-all"}); }
		std::vector<Action> actions;
	};

	using Key = Rml::Input::KeyIdentifier;
	using mdJucePlugin::PanelKeyboardControl;

	void testTriggerPressReleaseAndAutoRepeat()
	{
		PanelKeyboardControl keyboard;
		FakeActions sink;
		assert(keyboard.handleKey(Key::KI_F1, true, false, false, 0.0, sink));
		assert(keyboard.handleKey(Key::KI_F1, true, false, false, 1.0, sink));
		assert(keyboard.handleKey(Key::KI_F1, false, false, false, 10.0, sink));
		assert(sink.actions.size() == 1);
		keyboard.serviceReleases(59.0, sink);
		assert(sink.actions.size() == 1);
		keyboard.serviceReleases(61.0, sink);
		assert(sink.actions.size() == 2);
		assert(sink.actions[0].name == "down"
			&& sink.actions[0].control == md::PanelControl::Trigger1);
		assert(sink.actions[1].name == "up"
			&& sink.actions[1].control == md::PanelControl::Trigger1);
	}

	void testFunctionChordFullyReleases()
	{
		PanelKeyboardControl keyboard;
		FakeActions sink;
		assert(keyboard.handleKey(Key::KI_F1, true, false, true, 0.0, sink));
		assert(keyboard.functionHeld());
		assert(keyboard.handleKey(Key::KI_F1, false, false, true, 1.0, sink));
		keyboard.serviceReleases(60.0, sink);
		assert(keyboard.handleKey(Key::KI_UNKNOWN, true, false, false, 61.0, sink) == false);
		assert(!keyboard.functionHeld());
		assert(sink.actions.size() == 4);
		assert(sink.actions[0].name == "down" && sink.actions[0].control == md::PanelControl::Function);
		assert(sink.actions[1].name == "down" && sink.actions[1].control == md::PanelControl::Trigger1);
		assert(sink.actions[2].name == "up" && sink.actions[2].control == md::PanelControl::Trigger1);
		assert(sink.actions[3].name == "up" && sink.actions[3].control == md::PanelControl::Function);
	}

	void testEncoderPressTurnAndRelease()
	{
		PanelKeyboardControl keyboard;
		FakeActions sink;
		assert(keyboard.handleKey(Key::KI_A, true, false, false, 0.0, sink));
		assert(keyboard.handleKey(Key::KI_OEM_6, true, false, false, 1.0, sink));
		assert(keyboard.handleKey(Key::KI_OEM_6, false, false, false, 2.0, sink));
		assert(keyboard.handleKey(Key::KI_A, false, false, false, 3.0, sink));
		keyboard.serviceReleases(100.0, sink);
		assert(sink.actions.size() == 3);
		assert(sink.actions[0].name == "encoder-down"
			&& sink.actions[0].encoder == md::PanelEncoder::DataEntryA);
		assert(sink.actions[1].name == "turn"
			&& sink.actions[1].encoder == md::PanelEncoder::DataEntryA
			&& sink.actions[1].direction == 1.0f);
		assert(sink.actions[2].name == "encoder-up"
			&& sink.actions[2].encoder == md::PanelEncoder::DataEntryA);
	}

	void testMonomachineKeysFunctionPageEncoderAndFocusRelease()
	{
		PanelKeyboardControl keyboard;
		FakeActions sink;
		sink.model = md::MachineModel::Monomachine;
		using Packet = md::PanelPacket;

		assert(keyboard.handleKey(Key::KI_F1, true, false, false, 0.0, sink));
		assert(keyboard.handleKey(Key::KI_F1, false, false, false, 1.0, sink));
		keyboard.serviceReleases(60.0, sink);
		assert((sink.packets[0] == Packet{0x20, 0x01}));
		assert((sink.packets[1] == Packet{0x20, 0x00}));

		assert(keyboard.handleKey(Key::KI_NEXT, true, false, false, 61.0, sink));
		assert(keyboard.handleKey(Key::KI_NEXT, false, false, false, 62.0, sink));
		keyboard.serviceReleases(121.0, sink);
		assert((sink.packets[2] == Packet{0x25, 0x04}));
		assert((sink.packets[3] == Packet{0x25, 0x00}));

		assert(keyboard.handleKey(Key::KI_F1, true, false, true, 122.0, sink));
		assert(keyboard.handleKey(Key::KI_F1, false, false, true, 123.0, sink));
		keyboard.serviceReleases(183.0, sink);
		assert(keyboard.handleKey(Key::KI_UNKNOWN, true, false, false, 184.0, sink) == false);
		assert((sink.packets[4] == Packet{0x23, 0x02}));
		assert((sink.packets[5] == Packet{0x20, 0x01}));
		assert((sink.packets[6] == Packet{0x20, 0x00}));
		assert((sink.packets[7] == Packet{0x23, 0x00}));

		assert(keyboard.handleKey(Key::KI_A, true, false, false, 185.0, sink));
		assert(keyboard.handleKey(Key::KI_OEM_6, true, false, false, 186.0, sink));
		assert(keyboard.handleKey(Key::KI_OEM_6, false, false, false, 187.0, sink));
		assert(keyboard.handleKey(Key::KI_A, false, false, false, 188.0, sink));
		keyboard.serviceReleases(248.0, sink);
		assert((sink.packets[8] == Packet{0x26, 0x01}));
		assert((sink.packets[9] == Packet{0x30, 0x01}));
		assert((sink.packets[10] == Packet{0x26, 0x00}));

		assert(keyboard.handleKey(Key::KI_F2, true, false, false, 249.0, sink));
		assert(keyboard.handleKey(Key::KI_F3, true, false, false, 250.0, sink));
		keyboard.reset(sink); // NativePanelView calls this when it loses global focus.
		assert((sink.packets[11] == Packet{0x20, 0x02}));
		assert((sink.packets[12] == Packet{0x20, 0x06}));
		assert((sink.packets[13] == Packet{0x20, 0x04}));
		assert((sink.packets[14] == Packet{0x20, 0x00}));
		assert(!keyboard.functionHeld() && !keyboard.selectedEncoder());
	}

	void testDisconnectResetReleasesHeldPanelInputs()
	{
		PanelKeyboardControl keyboard;
		FakeActions sink;
		assert(keyboard.handleKey(Key::KI_F1, true, false, false, 0.0, sink));
		assert(keyboard.handleKey(Key::KI_F2, true, false, false, 1.0, sink));
		assert(keyboard.handleKey(Key::KI_A, true, false, false, 2.0, sink));
		assert(keyboard.handleKey(Key::KI_OEM_6, true, false, false, 3.0, sink));
		keyboard.reset(sink); // NativePanelView also resets its local controls when IPC disconnects.
		assert(!keyboard.functionHeld() && !keyboard.selectedEncoder());
		assert(sink.packets.size() == 7);
		assert((sink.packets[0] == md::PanelPacket{0x20, 0x01}));
		assert((sink.packets[1] == md::PanelPacket{0x20, 0x03}));
		assert((sink.packets[2] == md::PanelPacket{0x25, 0x01}));
		assert((sink.packets[3] == md::PanelPacket{0x30, 0x01}));
		assert((sink.packets[4] == md::PanelPacket{0x20, 0x02}));
		assert((sink.packets[5] == md::PanelPacket{0x20, 0x00}));
		assert((sink.packets[6] == md::PanelPacket{0x25, 0x00}));
		assert(sink.actions.back().name == "release-all");
	}
}

int main()
{
	testTriggerPressReleaseAndAutoRepeat();
	testFunctionChordFullyReleases();
	testEncoderPressTurnAndRelease();
	testMonomachineKeysFunctionPageEncoderAndFocusRelease();
	testDisconnectResetReleasesHeldPanelInputs();
	std::cout << "shared MD/MM panel keyboard controls passed\n";
	return 0;
}
