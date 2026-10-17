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
		void keyboardPressControl(md::PanelControl control, bool, bool) override
		{
			actions.push_back({"down", control});
		}
		void keyboardReleaseControl(md::PanelControl control) override
		{
			actions.push_back({"up", control});
		}
		bool keyboardEncoderAvailable(md::PanelEncoder) const override { return true; }
	void keyboardSelectEncoder(std::optional<md::PanelEncoder>) override {}
	void keyboardPressEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"encoder-down", {}, encoder});
		}
		void keyboardReleaseEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"encoder-up", {}, encoder});
		}
		void keyboardTurnEncoder(md::PanelEncoder encoder, float direction) override
		{
			actions.push_back({"turn", {}, encoder, direction});
		}
		void keyboardClickEncoder(md::PanelEncoder encoder) override
		{
			actions.push_back({"click", {}, encoder});
		}
		bool keyboardHandlesMachinedrumPageKeys() const override { return true; }
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
}

int main()
{
	testTriggerPressReleaseAndAutoRepeat();
	testFunctionChordFullyReleases();
	testEncoderPressTurnAndRelease();
	std::cout << "shared MD/MM panel keyboard controls passed\n";
	return 0;
}
