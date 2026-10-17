#include "mdFrontPanelLcd.h"
#include "mdKeyboardControl.h"
#include "mdNativePanelClient.h"
#include "mdPanelAffordances.h"
#include "mdPanelControls.h"

#include "jucePluginData.h"
#include "BinaryData.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Context.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	struct Options
	{
		std::string socketPath;
		std::string runtimeDirectory;
		std::string unit;
		std::string model;
		bool trace = false;
	};

	std::optional<Options> parseOptions(juce::StringArray args, std::string& error)
	{
		Options result;
		for(int i = 0; i < args.size(); ++i)
		{
			const auto flag = args[i].toStdString();
			if(flag == "--trace")
			{
				result.trace = true;
				continue;
			}
			if(flag != "--socket" && flag != "--runtime" && flag != "--unit" && flag != "--model")
			{
				error = "unknown option: " + flag;
				return {};
			}
			if(i + 1 >= args.size())
			{
				error = "missing value for " + flag;
				return {};
			}
			const auto value = args[++i].toStdString();
			if(flag == "--socket") result.socketPath = value;
			else if(flag == "--runtime") result.runtimeDirectory = value;
			else if(flag == "--unit") result.unit = value;
			else result.model = value;
		}
		if(result.socketPath.empty() || result.runtimeDirectory.empty()
			|| result.unit.empty() || result.model.empty())
		{
			error = "required arguments: --model md --unit md --socket PATH --runtime DIR";
			return {};
		}
		if(result.model != "md" || result.unit.empty())
		{
			error = "this vertical slice accepts only --model md and an explicit MD unit";
			return {};
		}
		return result;
	}

	std::string htmlEscape(const std::string& value)
	{
		std::string result;
		for(const char ch : value)
		{
			switch(ch)
			{
			case '&': result += "&amp;"; break;
			case '<': result += "&lt;"; break;
			case '>': result += "&gt;"; break;
		case '"': result += "&quot;"; break;
		default: result += ch; break;
			}
		}
		return result;
	}

	class PanelResources final : public juceRmlUi::DataProvider
	{
	public:
		PanelResources()
		{
			add(BinaryData::namedResourceListSize, BinaryData::originalFilenames,
				BinaryData::namedResourceList, BinaryData::getNamedResource);
			add(jucePluginData::namedResourceListSize, jucePluginData::originalFilenames,
				jucePluginData::namedResourceList, jucePluginData::getNamedResource);
		}

		const char* getResourceByFilename(const std::string& name, uint32_t& size) override
		{
			const auto basename = std::filesystem::path(name).filename().string();
			const auto found = m_resources.find(basename);
			if(found == m_resources.end())
			{
				size = 0;
				return nullptr;
			}
			size = found->second.second;
			return found->second.first;
		}

		std::vector<std::string> getAllFilenames() override
		{
			std::vector<std::string> result;
			result.reserve(m_resources.size());
			for(const auto& [name, _] : m_resources)
				result.push_back(name);
			return result;
		}

	private:
		using Lookup = const char* (*)(const char*, int&);
		void add(int count, const char* const* filenames, const char* const* names, Lookup lookup)
		{
			for(int i = 0; i < count; ++i)
			{
				int size = 0;
				const auto* data = lookup(names[i], size);
				if(data && size >= 0)
					m_resources.emplace(filenames[i], std::make_pair(data, static_cast<uint32_t>(size)));
			}
		}
		std::map<std::string, std::pair<const char*, uint32_t>> m_resources;
	};

	class NativePanelView final : public juce::Component,
		private juce::Timer,
		private mdJucePlugin::PanelKeyboardActions
	{
	public:
		explicit NativePanelView(const Options& options)
			: m_interfaces(m_resources)
			, m_trace(options.trace)
		{
			try
			{
				m_backend = std::make_unique<mdJucePlugin::NativePanelClient>(
					options.socketPath, options.runtimeDirectory, options.unit,
					md::MachineModel::Machinedrum);
				m_connected = true;
			}
			catch(const std::exception& error)
			{
				m_connectionMessage = error.what();
				trace("native-panel connection failed: " + m_connectionMessage);
			}

			juceRmlUi::RmlComponentConfig config;
			config.forceSoftwareRenderer = juceRmlUi::SoftwareRendererMode::ForceOn;
			m_panel = std::make_unique<juceRmlUi::RmlComponent>(m_interfaces, m_resources,
				"mdDefault.rml", 1.0f,
				[](juceRmlUi::RmlComponent&, Rml::Context&) {},
				[](juceRmlUi::RmlComponent&, Rml::Context&) {}, config);
			m_panel->setLookAndFeel(&m_lookAndFeel);
			m_panel->setWantsKeyboardFocus(true);
			addAndMakeVisible(*m_panel);
			wirePanel();
			m_panel->grabKeyboardFocus();
			setSize(1100, 820);
			startTimerHz(30);
		}

		~NativePanelView() override
		{
			stopTimer();
			keyboardReleaseAllPanelInputs();
			m_panel.reset();
		}

		void resized() override
		{
			if(m_panel)
				m_panel->setBounds(getLocalBounds());
		}

		void focusPanelKeyboard()
		{
			if(m_panel)
			{
				m_panel->grabKeyboardFocus();
				trace("native-panel keyboard-focus="
					+ std::to_string(m_panel->hasKeyboardFocus(true)));
			}
		}

	private:
		struct ActiveButton
		{
			md::PanelControl control{};
			md::PanelPacket packet{};
		};

		void trace(const std::string& line) const
		{
			if(m_trace)
				std::cerr << line << '\n';
		}

		Rml::Element* element(const char* id) const
		{
			return m_panel && m_panel->getDocument()
				? m_panel->getDocument()->GetElementById(id) : nullptr;
		}

		void setStatus(const std::string& message, bool error)
		{
			if(m_connectionMessage == message)
				return;
			m_connectionMessage = message;
			if(m_trace && (error || !m_haveStatus))
				trace("native-panel status: " + message);
			if(m_statusElement)
			{
				m_statusElement->SetInnerRML(htmlEscape(message));
				m_statusElement->SetClass("connectionError", error);
			}
			if(m_panel)
				m_panel->enqueueUpdateOnce();
		}

		void addNotice(Rml::ElementDocument* document)
		{
			auto status = document->CreateElement("div");
			m_statusElement = status.get();
			m_statusElement->SetAttribute("id", "nativePanelStatus");
			m_statusElement->SetAttribute("style",
				"position:absolute; left:24dp; top:582dp; width:1052dp; height:26dp;"
				"padding:5dp 9dp; background-color:#171b1d; color:#a9d8ad;"
				"font-size:12dp; line-height:16dp; z-index:3000; white-space:nowrap;");
			document->AppendChild(std::move(status));
			m_statusElement->SetClass("connectionError", !m_connected);
			setStatus(m_connected ? "Connected to hosted MD · waiting for first status snapshot"
				: "Engine unavailable: " + m_connectionMessage, !m_connected);

			if(auto* const rack = document->GetElementById("machineRack"))
			{
				rack->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				rack->SetAttribute("title", "Processor-owned machine rack is unavailable in a hosted panel");
				rack->SetInnerRML("<div style=\"position:absolute; left:8dp; top:12dp; width:1040dp;"
					"height:56dp; padding:12dp 16dp; background-color:#171b1d; color:#d2d7d0;"
					"font-size:13dp; line-height:18dp;\">"
					"Hosted Machinedrum panel. Preset, firmware, storage, SysEx, scene, and machine-rack"
					" operations need processor-owned services and are disabled here.</div>");
			}
			if(auto* const settings = element("btSettings"))
			{
				settings->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				settings->SetAttribute("title", "Processor settings require the plug-in or standalone editor");
			}
			if(auto* const scenes = element("sceneStrip"))
			{
				scenes->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				scenes->SetAttribute("title", "Scene editing requires the processor controller");
			}
			if(auto* const master = element("encMaster"))
			{
				master->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				master->SetAttribute("title", "Master output gain belongs to the processor");
			}
		}

		void wirePanel()
		{
			auto* const document = m_panel->getDocument();
			if(!document)
				throw std::runtime_error("MD panel RML document did not load");
			addNotice(document);

			if(auto* const area = document->GetElementById("lcdArea"))
			{
				m_lcd = juceRmlUi::ElemCanvas::create(area);
				m_lcd->setClearEveryFrame(true);
				m_lcd->setRepaintGraphicsCallback([this](juce::Image& target, juce::Graphics& graphics)
				{
					const auto off = mdJucePlugin::frontPanelLcdOffColor(md::MachineModel::Machinedrum);
					graphics.fillAll(juce::Colour(off));
					if(!m_haveStatus)
						return;
					auto image = mdJucePlugin::makeFrontPanelLcdImage(
						m_snapshot.panel, md::MachineModel::Machinedrum);
					graphics.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
					graphics.drawImageWithin(image, 0, 0, target.getWidth(), target.getHeight(),
						juce::RectanglePlacement::centred);
				});
			}

			for(const auto& binding : mdJucePlugin::g_panelButtons)
			{
				auto* const button = dynamic_cast<juceRmlUi::ElemButton*>(element(binding.id));
				if(!button)
					continue;
				const auto packet = md::panelPacket(md::MachineModel::Machinedrum, binding.control);
				if(!packet)
				{
					button->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
					button->SetAttribute("title", "This control is not available for the hosted MD model");
					continue;
				}
				button->SetAttribute("title", "Send this physical MD panel key to the hosted machine");
				juceRmlUi::EventListener::Add(button, Rml::EventId::Mousedown,
					[this, button, control = binding.control](Rml::Event& event)
					{
						if(juceRmlUi::helper::getMouseButton(event)
							!= juceRmlUi::MouseButton::Left)
							return;
						pressControl(control, event.GetParameter<int>("shift_key", 0) != 0);
						juceRmlUi::ElemButton::setChecked(button, true);
						event.StopPropagation();
					});
				const auto release = [this, button, control = binding.control](Rml::Event&)
				{
					if(!m_shiftLatch.contains(control))
					{
						releaseControlNow(control);
						juceRmlUi::ElemButton::setChecked(button, false);
					}
				};
				juceRmlUi::EventListener::Add(button, Rml::EventId::Mouseup, release);
				juceRmlUi::EventListener::Add(button, Rml::EventId::Mouseout, release);
			}

			for(size_t i = 0; i < 8; ++i)
			{
				const auto encoder = static_cast<md::PanelEncoder>(i);
				const char id[] = { 'e', 'n', 'c', static_cast<char>('A' + i), 0 };
				m_encoderElements[i] = dynamic_cast<juceRmlUi::ElemKnob*>(element(id));
				configureEncoder(m_encoderElements[i], encoder, m_encoderLast[i], m_encoderAccum[i]);
			}
			m_encoderElements[8] = dynamic_cast<juceRmlUi::ElemKnob*>(element("encLevel"));
			configureEncoder(m_encoderElements[8], md::PanelEncoder::Level, m_encoderLast[8], m_encoderAccum[8]);
			m_encoderElements[9] = dynamic_cast<juceRmlUi::ElemKnob*>(element("encSound"));
			configureEncoder(m_encoderElements[9], md::PanelEncoder::SoundSelection, m_encoderLast[9], m_encoderAccum[9]);

			juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown,
				[this](Rml::Event& event) { onPanelKey(event, true); });
			juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup,
				[this](Rml::Event& event) { onPanelKey(event, false); });
			juceRmlUi::EventListener::Add(document, Rml::EventId::Mouseup,
				[this](Rml::Event&)
				{
					for(size_t i = 0; i < m_encoderElements.size(); ++i)
						if(m_encoderElements[i])
							m_encoderElements[i]->SetClass("encoderPressed", false);
					if(m_mouseEncoderPress)
						keyboardReleaseEncoder(*m_mouseEncoderPress);
				});
		}

		void configureEncoder(juceRmlUi::ElemKnob* knob, md::PanelEncoder encoder,
			float& last, float& accumulated)
		{
			if(!knob || !md::panelEncoderCommand(md::MachineModel::Machinedrum, encoder))
				return;
			knob->setMinValue(0.0f);
			knob->setMaxValue(100.0f);
			knob->setEndless(true);
			knob->setValue(50.0f, false);
			last = 50.0f;
			accumulated = 0.0f;
			juceRmlUi::EventListener::Add(knob, Rml::EventId::Change,
				[this, knob, encoder, &last, &accumulated](Rml::Event&)
				{
					const float value = knob->getValue();
					float delta = value - last;
					if(delta > 50.0f) delta -= 100.0f;
					else if(delta < -50.0f) delta += 100.0f;
					last = value;
					accumulated += delta;
					const auto steps = static_cast<int>(accumulated);
					if(steps == 0)
						return;
					accumulated -= static_cast<float>(steps);
					emitEncoder(encoder, steps);
				});
			juceRmlUi::EventListener::Add(knob, Rml::EventId::Mousedown,
				[this, encoder](Rml::Event& event)
				{
					if(juceRmlUi::helper::getMouseButton(event)
						== juceRmlUi::MouseButton::Left
						&& juceRmlUi::helper::getKeyModAlt(event))
					{
						keyboardPressEncoder(encoder);
						m_mouseEncoderPress = encoder;
					}
				});
		}

		void onPanelKey(Rml::Event& event, bool down)
		{
			const auto key = event.GetParameter<int>("key_identifier", 0);
			if(m_trace)
				trace("native-panel key-event identifier=" + std::to_string(key)
					+ " down=" + std::to_string(down)
					+ " shift=" + std::to_string(event.GetParameter<int>("shift_key", 0))
					+ " ctrl=" + std::to_string(event.GetParameter<int>("ctrl_key", 0)));
			const bool handled = m_keyboard.handleKey(key, down,
				event.GetParameter<int>("shift_key", 0) != 0,
				event.GetParameter<int>("ctrl_key", 0) != 0,
				juce::Time::getMillisecondCounterHiRes(), *this);
			if(!handled)
				return;
			event.StopPropagation();
			if(m_trace && key == Rml::Input::KI_F1)
				trace(std::string("native-panel key-consumed F1 ") + (down ? "down" : "up"));
		}

		bool sendPacket(md::PanelPacket packet)
		{
			if(!m_backend)
				return false;
			std::string error;
			const bool accepted = m_backend->sendPanel(packet.row, packet.mask, error);
			if(!accepted)
			{
				m_connected = false;
				setStatus("Engine disconnected: " + error, true);
				trace("native-panel input failed: " + error);
			}
			else if(m_trace)
			{
				char bytes[96];
				std::snprintf(bytes, sizeof(bytes),
					"native-panel input-accepted command=0x%02x argument=0x%02x",
					packet.row, packet.mask);
				trace(bytes);
			}
			return accepted;
		}

		void pressControl(md::PanelControl control, bool shift)
		{
			const auto packet = md::panelPacket(md::MachineModel::Machinedrum, control);
			if(!packet || std::find_if(m_activeButtons.begin(), m_activeButtons.end(),
				[control](const auto& active) { return active.control == control; }) != m_activeButtons.end())
				return;
			const auto action = m_shiftLatch.press(control, shift);
			if(action == mdJucePlugin::panelAffordances::ShiftPanelLatch::PressAction::Ignored)
				return;
			const auto combined = m_rows.press(*packet);
			m_activeButtons.push_back({ control, *packet });
			(void)sendPacket(combined);
		}

		void releaseControlNow(md::PanelControl control)
		{
			const auto it = std::find_if(m_activeButtons.begin(), m_activeButtons.end(),
				[control](const auto& active) { return active.control == control; });
			if(it == m_activeButtons.end())
				return;
			const auto packet = it->packet;
			m_activeButtons.erase(it);
			const auto combined = m_rows.release(packet);
			(void)sendPacket(combined);
			for(const auto& binding : mdJucePlugin::g_panelButtons)
				if(binding.control == control)
					if(auto* const button = dynamic_cast<juceRmlUi::ElemButton*>(element(binding.id)))
						juceRmlUi::ElemButton::setChecked(button, false);
		}

		void releaseLatchedControls()
		{
			m_shiftLatch.releaseAll([this](md::PanelControl control)
			{
				releaseControlNow(control);
			});
		}

		void emitEncoder(md::PanelEncoder encoder, int steps)
		{
			const auto command = md::panelEncoderCommand(md::MachineModel::Machinedrum, encoder);
			if(!command)
				return;
			if(auto* const knob = encoderElement(encoder))
			{
				auto value = std::fmod(knob->getValue() + steps, 100.0f);
				if(value < 0.0f)
					value += 100.0f;
				knob->setValue(value, false);
			}
			const auto argument = static_cast<uint8_t>(steps > 0 ? 0x01 : 0xff);
			for(int i = 0; i < std::min(std::abs(steps), 8); ++i)
				(void)sendPacket({ *command, argument });
		}

		juceRmlUi::ElemKnob* encoderElement(md::PanelEncoder encoder) const
		{
			const auto index = static_cast<size_t>(encoder);
			return index < m_encoderElements.size() ? m_encoderElements[index] : nullptr;
		}

		void updateLeds()
		{
			if(!m_haveStatus)
				return;
			const auto set = [this](const std::string& id, const char* className, bool on)
			{
				if(auto* const item = element(id.c_str()))
					item->SetClass(className, on);
			};
			for(unsigned i = 0; i < 16; ++i)
			{
				set("stepLed" + std::to_string(i), "lit", m_snapshot.panel.getStepLed(i));
				set("drumLed" + std::to_string(i), "lit", m_snapshot.panel.getDrumLed(i));
			}
			const std::pair<const char*, md::FrontPanel::StatusLed> status[] = {
				{ "stPattern", md::FrontPanel::StatusLed::Pattern },
				{ "stSong", md::FrontPanel::StatusLed::Song },
				{ "stSynth", md::FrontPanel::StatusLed::Synthesis },
				{ "stFx", md::FrontPanel::StatusLed::Effects },
				{ "stRoute", md::FrontPanel::StatusLed::Routing },
			};
			for(const auto& led : status)
				set(led.first, "lit", m_snapshot.panel.getStatusLed(led.second));
			const std::pair<const char*, md::FrontPanel::ModeLed> mode[] = {
				{ "ledClassic", md::FrontPanel::ModeLed::Classic },
				{ "ledExtended", md::FrontPanel::ModeLed::Extended },
				{ "ledBankAD", md::FrontPanel::ModeLed::BankGroupAD },
				{ "ledBankEH", md::FrontPanel::ModeLed::BankGroupEH },
				{ "ledRecord", md::FrontPanel::ModeLed::Record },
				{ "ledTempo", md::FrontPanel::ModeLed::Tempo },
			};
			for(const auto& led : mode)
				set(led.first, "lit", m_snapshot.panel.getModeLed(led.second));
			const char* pages[] = {
				"mdPatternPage0", "mdPatternPage1", "mdPatternPage2", "mdPatternPage3",
			};
			constexpr uint8_t pageBanks[] = { 0x22, 0x22, 0x22, 0x23 };
			constexpr uint8_t pageBits[] = { 0, 1, 2, 6 };
			for(size_t i = 0; i < std::size(pages); ++i)
				set(pages[i], "lit", (m_snapshot.panel.getLedBankRaw(pageBanks[i])
					& static_cast<uint8_t>(1u << pageBits[i])) == 0);
		}

		void timerCallback() override
		{
			const auto now = juce::Time::getMillisecondCounterHiRes();
			m_keyboard.serviceReleases(now, *this);
			if(m_shiftLatch.size() != 0
				&& !juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
				releaseLatchedControls();
			if(!m_backend)
				return;
			mdJucePlugin::PanelStatusSnapshot snapshot;
			std::string error;
			if(!m_backend->readStatus(snapshot, error))
			{
				m_connected = false;
				setStatus("Engine disconnected: " + error, true);
				return;
			}
			m_connected = true;
			if(!m_haveStatus || snapshot.generation != m_snapshot.generation)
			{
				m_snapshot = std::move(snapshot);
				m_haveStatus = true;
				if(m_lcd)
					m_lcd->repaint();
				updateLeds();
				if(m_panel)
					m_panel->enqueueUpdateOnce();
				if(m_trace && !m_loggedFirstStatus)
				{
					trace("native-panel status generation=" + std::to_string(m_snapshot.generation)
						+ " lcd-lit=" + std::to_string(m_snapshot.panel.countLitPixels()));
					m_loggedFirstStatus = true;
				}
			}
			setStatus("Connected to hosted MD · status generation "
				+ std::to_string(m_snapshot.generation), false);
		}

		void keyboardPressControl(md::PanelControl control, bool shift, bool) override
		{
			pressControl(control, shift);
		}
		void keyboardReleaseControl(md::PanelControl control) override
		{
			if(!m_shiftLatch.contains(control))
				releaseControlNow(control);
		}
		bool keyboardEncoderAvailable(md::PanelEncoder encoder) const override
		{
			return md::panelEncoderCommand(md::MachineModel::Machinedrum, encoder).has_value();
		}
		void keyboardSelectEncoder(std::optional<md::PanelEncoder> encoder) override
		{
			if(m_selectedEncoder)
				if(auto* const old = encoderElement(*m_selectedEncoder))
					old->SetClass("keyboardEncoderSelected", false);
			m_selectedEncoder = encoder;
			if(m_selectedEncoder)
				if(auto* const selected = encoderElement(*m_selectedEncoder))
					selected->SetClass("keyboardEncoderSelected", true);
		}
		void keyboardPressEncoder(md::PanelEncoder encoder) override
		{
			const auto packet = md::panelEncoderPressPacket(md::MachineModel::Machinedrum, encoder);
			if(!packet || m_encoderPressPacket)
				return;
			m_encoderPressPacket = packet;
			(void)sendPacket(m_rows.press(*packet));
			if(auto* const knob = encoderElement(encoder))
				knob->SetClass("encoderPressed", true);
		}
		void keyboardReleaseEncoder(md::PanelEncoder encoder) override
		{
			if(!m_encoderPressPacket)
				return;
			(void)sendPacket(m_rows.release(*m_encoderPressPacket));
			if(auto* const knob = encoderElement(encoder))
				knob->SetClass("encoderPressed", false);
			m_encoderPressPacket.reset();
			m_mouseEncoderPress.reset();
		}
		void keyboardTurnEncoder(md::PanelEncoder encoder, float direction) override
		{
			emitEncoder(encoder, direction > 0.0f ? 1 : -1);
		}
		void keyboardClickEncoder(md::PanelEncoder encoder) override
		{
			const auto packet = md::panelEncoderPressPacket(md::MachineModel::Machinedrum, encoder);
			if(!packet)
				return;
			(void)sendPacket(m_rows.press(*packet));
			(void)sendPacket(m_rows.release(*packet));
		}
		bool keyboardHandlesMachinedrumPageKeys() const override { return true; }
		void keyboardMachinedrumPageKey(int, bool) override {}
		void keyboardReleaseAllPanelInputs() override
		{
			releaseLatchedControls();
			for(uint8_t row = 0x20; row <= 0x26; ++row)
				if(m_rows.mask(row) != 0)
					(void)sendPacket({ row, 0 });
			m_rows.reset();
			m_activeButtons.clear();
			m_encoderPressPacket.reset();
			m_mouseEncoderPress.reset();
			for(const auto& binding : mdJucePlugin::g_panelButtons)
				if(auto* const button = dynamic_cast<juceRmlUi::ElemButton*>(element(binding.id)))
					juceRmlUi::ElemButton::setChecked(button, false);
		}

		PanelResources m_resources;
		juceRmlUi::RmlInterfaces m_interfaces;
		juceRmlUi::LookAndFeel m_lookAndFeel;
		std::unique_ptr<mdJucePlugin::NativePanelClient> m_backend;
		std::unique_ptr<juceRmlUi::RmlComponent> m_panel;
		juceRmlUi::ElemCanvas* m_lcd = nullptr;
		Rml::Element* m_statusElement = nullptr;
		mdJucePlugin::PanelKeyboardControl m_keyboard;
		md::PanelRowState m_rows;
		mdJucePlugin::panelAffordances::ShiftPanelLatch m_shiftLatch;
		std::vector<ActiveButton> m_activeButtons;
		std::array<juceRmlUi::ElemKnob*, 10> m_encoderElements{};
		std::array<float, 10> m_encoderLast{};
		std::array<float, 10> m_encoderAccum{};
		std::optional<md::PanelEncoder> m_selectedEncoder;
		std::optional<md::PanelEncoder> m_mouseEncoderPress;
		std::optional<md::PanelPacket> m_encoderPressPacket;
		mdJucePlugin::PanelStatusSnapshot m_snapshot;
		std::string m_connectionMessage;
		bool m_connected = false;
		bool m_haveStatus = false;
		bool m_loggedFirstStatus = false;
		bool m_trace = false;
	};

	class ErrorContent final : public juce::Component
	{
	public:
		explicit ErrorContent(std::string text) : m_text(std::move(text))
		{
			setSize(800, 180);
		}
		void paint(juce::Graphics& graphics) override
		{
			graphics.fillAll(juce::Colour(0xff202326));
			graphics.setColour(juce::Colours::white);
			graphics.drawFittedText(m_text, getLocalBounds().reduced(24),
				juce::Justification::centredLeft, 5);
		}
	private:
		std::string m_text;
	};

	class MainWindow final : public juce::DocumentWindow
	{
	public:
		explicit MainWindow(const Options& options)
			: juce::DocumentWindow("Gearmulator MD · Hosted Panel",
				juce::Colour(0xff202326), juce::DocumentWindow::allButtons)
		{
			setUsingNativeTitleBar(true);
			try
			{
				setContentOwned(new NativePanelView(options), true);
				centreWithSize(1100, 820);
			}
			catch(const std::exception& error)
			{
				setContentOwned(new ErrorContent(error.what()), true);
				centreWithSize(800, 180);
			}
			setResizable(true, true);
			setVisible(true);
			toFront(true);
			if(auto* const panel = dynamic_cast<NativePanelView*>(getContentComponent()))
				panel->focusPanelKeyboard();
		}
		void closeButtonPressed() override
		{
			juce::JUCEApplication::getInstance()->systemRequestedQuit();
		}
	};

	class NativePanelApplication final : public juce::JUCEApplication
	{
	public:
		const juce::String getApplicationName() override { return "Gearmulator MD Native Panel"; }
		const juce::String getApplicationVersion() override { return "1.0"; }
		void initialise(const juce::String&) override
		{
			std::string error;
			const auto options = parseOptions(getCommandLineParameterArray(), error);
			if(!options)
			{
				std::cerr << "native-panel startup error: " << error << '\n';
				m_window = std::make_unique<MainWindow>(Options{});
				m_window->setContentOwned(new ErrorContent(error), true);
				m_window->centreWithSize(800, 180);
				return;
			}
			m_window = std::make_unique<MainWindow>(*options);
			std::cout << "NATIVE_PANEL_WINDOW_READY model=md unit=" << options->unit << '\n';
		}
		void shutdown() override { m_window.reset(); }
		void systemRequestedQuit() override { quit(); }
		void anotherInstanceStarted(const juce::String&) override {}
	private:
		std::unique_ptr<MainWindow> m_window;
	};
}

START_JUCE_APPLICATION(NativePanelApplication)
