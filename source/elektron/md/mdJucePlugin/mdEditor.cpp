#include "mdEditor.h"

#include "mdController.h"
#include "mdPanelAffordances.h"
#include "mdLcdText.h"
#include "mdMonomachineHelp.h"
#include "mdMachinedrumHelp.h"
#include "mdParameterHelp.h"
#include "mdPluginProcessor.h"
#include "mdSettingsAudioInput.h"
#include "mdSettingsPanelFeel.h"
#include "mdSampleDropTarget.h"
#include "mdMachineRack.h"
#include "mdPixelPerfectPanel.h"
#include "mdLcdViewport.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginEditorLib/fileChooserFlow.h"

#include "juceUiLib/messageBox.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdstate.h"

#include "synthLib/plugin.h"

#include "baseLib/filesystem.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlMenu.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace mdJucePlugin
{
	namespace
	{
		// The two machines use the same framebuffer geometry but different physical
		// displays, both positive: lit red/orange backlight with dark pixels on the
		// Machinedrum, pale green-grey with dark pixels on the Monomachine. "Off" is
		// therefore the bright backlight and "on" the dark set pixel.
		constexpr uint32_t g_mdLcdOff = 0xffe0472b;
		constexpr uint32_t g_mdLcdOn  = 0xff38100a;
		constexpr uint32_t g_mmLcdOff = 0xffb9c8b2;
		constexpr uint32_t g_mmLcdOn  = 0xff1a2b1e;

		// A skin button bound to a logical control. The packet is selected using the
		// actual device model when the editor is created.
		struct PanelButton
		{
			const char* id;
			md::PanelControl control;
		};

		bool lcdChanged(const md::FrontPanel& _a, const md::FrontPanel& _b)
		{
			for(uint32_t half = 0; half < 2; ++half)
			{
				for(uint32_t page = 0; page < 8; ++page)
				{
					for(uint32_t column = 0; column < 64; ++column)
					{
						if(_a.getLcdVram(half, page, column) != _b.getLcdVram(half, page, column))
							return true;
					}
				}
			}
			return false;
		}

		constexpr bool isTrigger(const md::PanelControl _control)
		{
			return _control >= md::PanelControl::Trigger1 && _control <= md::PanelControl::Trigger16;
		}

		// Arbitrary endless-knob value range; only per-move deltas are used.
		constexpr float g_encoderRange = 100.0f;
		constexpr int g_encoderBurstCap = 8;	// max ±1 events emitted per Change
		constexpr int g_presentationTimerId = 1;
		constexpr int g_panelTimerId = 2;
		constexpr int g_presentationTimerIntervalMilliseconds = 16;
		constexpr int g_panelTimerIntervalMilliseconds = 33;
		constexpr size_t g_ledTransitionBatchSize = 256;

		constexpr PanelButton g_panelButtons[] =
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

	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
		, m_model(dynamic_cast<const AudioPluginAudioProcessor&>(_processor).getModel())
	{
		juce::Desktop::getInstance().addFocusChangeListener(this);
	}

	Editor::~Editor()
	{
		juce::Desktop::getInstance().removeFocusChangeListener(this);
		m_panelSteps.clear();
		cancelPanelInputGestures();
		stopTimer(g_presentationTimerId);
		stopTimer(g_panelTimerId);
	}

	std::shared_ptr<md::FrontPanelPublisher> Editor::getFrontPanelPublisher() const
	{
		return getProcessor().getPlugin().withDeviceLocked(
			[](synthLib::Device* const _device)
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				return device ? device->getFrontPanelPublisher()
					: std::shared_ptr<md::FrontPanelPublisher>{};
			});
	}

	bool Editor::sendPanelEvent(const uint8_t _command, const uint8_t _argument) const
	{
		auto& plugin = getProcessor().getPlugin();
		auto& diagnostics = plugin.getRealtimeInstrumentation();
		const auto model = static_cast<uint32_t>(getModel());
		const auto token = diagnostics.beginPanelInput(model, _command, _argument);
		const auto accepted = plugin.withDeviceLocked(
			[&](synthLib::Device* const _device)
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				if(!device)
					return false;
				return device->sendPanelEvent(_command, _argument);
			});
		diagnostics.endPanelInput(token, model, _command, _argument, accepted);
		return accepted;
	}

	bool Editor::refreshFrontPanelState(const double _nowMilliseconds)
	{
		// Fetching the publisher takes the device lock, which the audio thread holds for its whole block. Doing
		// that every frame made the UI, LEDs included, wait out most of a block whenever the machine ran close
		// to realtime. The publisher lasts as long as its device, so fetch it again only when it is retired.
		if(!m_frontPanelPublisher || m_frontPanelPublisher->isRetired())
			m_frontPanelPublisher = getFrontPanelPublisher();
		const auto& publisher = m_frontPanelPublisher;
		if(!publisher)
			return false;
		const auto presentationBeforeDrain = m_ledPresentation;
		const bool ledsChangedBeforeDrain = m_ledsChanged;

		std::array<md::FrontPanelLedTransition, g_ledTransitionBatchSize> transitions;
		const auto drainTransitions = [&](const uint64_t _afterSequence = 0)
		{
			constexpr size_t maxBatches =
				(md::FrontPanelPublisher::g_ledTransitionCapacity
					+ g_ledTransitionBatchSize - 1) / g_ledTransitionBatchSize;
			for(size_t batch = 0; batch < maxBatches; ++batch)
			{
				const auto count = publisher->drainLedTransitions(
					transitions.data(), transitions.size());
				for(size_t i = 0; i < count; ++i)
					if(transitions[i].sequence > _afterSequence)
						m_ledPresentation.apply(transitions[i], _nowMilliseconds);
				if(count < transitions.size())
					break;
			}
		};

		auto status = publisher->getLedTransitionStatus();
		if(!m_ledTransitionStatusValid
			|| status.epoch != m_ledTransitionStatus.epoch
			|| status.dropped != m_ledTransitionStatus.dropped)
		{
			m_ledResyncPending = true;
			m_ledResyncSequence = std::max(
				m_ledResyncSequence, status.producedSequence);
		}

		auto published = publisher->readPublishedState();
		m_lcdChanged = !m_frontPanelSnapshotValid
			|| lcdChanged(m_frontPanelSnapshot, published.panel);
		m_lcdInteractionInputChanged = !m_frontPanelSnapshotValid || m_lcdChanged
			|| lcdInteraction::classificationLedsChanged(
				m_frontPanelSnapshot, published.panel, getModel());
		m_frontPanelSnapshot = std::move(published.panel);

		if(m_ledResyncPending && published.ledSequence >= m_ledResyncSequence)
		{
			m_ledPresentation.reset(m_frontPanelSnapshot);
			m_ledsChanged = true;
			m_ledResyncPending = false;
			drainTransitions(published.ledSequence);
		}
		else if(!m_ledResyncPending)
		{
			drainTransitions();
		}

		const auto finalStatus = publisher->getLedTransitionStatus();
		if(finalStatus.epoch != status.epoch
			|| finalStatus.dropped != status.dropped)
		{
			m_ledPresentation = presentationBeforeDrain;
			m_ledsChanged = ledsChangedBeforeDrain;
			m_ledResyncPending = true;
			m_ledResyncSequence = std::max(
				m_ledResyncSequence, finalStatus.producedSequence);
		}
		m_ledTransitionStatus = finalStatus;
		m_ledTransitionStatusValid = true;
		m_ledsChanged = m_ledPresentation.advance(_nowMilliseconds)
			|| m_ledsChanged;
		return true;
	}

	md::MachineModel Editor::getModel() const
	{
		return m_model;
	}

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		if(auto* romSelector = findChild<juceRmlUi::ElemComboBox>("RomSelector", false))
		{
			const auto rom = md::RomLoader::findROM(getModel());

			if(rom.isValid())
				romSelector->addOption(baseLib::filesystem::getFilenameWithoutPath(rom.getFilename()));
			else
				romSelector->addOption("<No ROM found>");

			romSelector->setValue(0);
			romSelector->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		}

		createLcd();
		bindSettingsButton();
		if(auto* const document = getRmlComponent() ? getRmlComponent()->getDocument() : nullptr)
			m_sampleDropTarget = std::make_unique<SampleDropTarget>(document, *this);
		if(auto* const rack = findChild("machineRack", false))
		{
			if(auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor()))
			{
				// the rack is an add-on; a failure there must not take the editor down
				try
				{
					m_machineRack = std::make_unique<MachineRack>(*this, *processor, rack);
				}
				catch(const std::exception& e)
				{
					std::fprintf(stderr, "[MD] machine rack disabled: %s\n", e.what());
					m_machineRack.reset();
				}
			}
		}
		else
			std::fprintf(stderr, "[MD] skin has no machineRack element\n");
		createButtons();
		createEncoders();
		createMasterVolume();
		applyPanelSpeeds();
		applyTooltipSettings();
		createLeds();
		createPanelAffordances();
		applyPixelPerfectPanel();
		createGamepad();
		createKeyboardControl();
		createParameterTooltip();

		// A transfer belongs to the emulated machine, not the lifetime of one
		// editor window. Reattach progress monitoring after a reopen, or reclaim a
		// file buffer whose terminal transition happened while no editor existed.
		const auto progress = getUserSysexProgress();
		if(progress && (progress->state == md::MidiSysexTransferState::Queued
			|| progress->state == md::MidiSysexTransferState::NegotiatingTurbo
			|| progress->state == md::MidiSysexTransferState::WaitingForDevice
			|| progress->state == md::MidiSysexTransferState::Retrying
			|| progress->state == md::MidiSysexTransferState::WaitingForReceiveMode
			|| progress->state == md::MidiSysexTransferState::Sending
			|| progress->state == md::MidiSysexTransferState::Cancelling))
		{
			m_sysexTransferWasActive = true;
			m_sysexMonitoredTicket = progress->ticket;
			m_sysexLastState = progress->state;
			m_sysexLastSent = progress->sent;
			m_sysexLastAdvanceMilliseconds = juce::Time::getMillisecondCounterHiRes();
		}
		else if(progress && (progress->state == md::MidiSysexTransferState::Complete
			|| progress->state == md::MidiSysexTransferState::Cancelled
			|| progress->state == md::MidiSysexTransferState::Failed))
		{
			std::vector<uint8_t> retiredPayload;
			(void)getProcessor().getPlugin().withDeviceLocked(
				[&](synthLib::Device* const _device)
				{
					auto* const device = dynamic_cast<md::Device*>(_device);
					return device && device->retireUserSysexImport(progress->ticket, retiredPayload);
				});
		}
	}

	void Editor::bindSettingsButton()
	{
		// gear icon in the skin: opens the settings panel (firmware, panel feel, audio)
		if(auto* const gear = findChild("btSettings", false))
			juceRmlUi::EventListener::AddClick(gear, [this] { toggleSettings(); });
	}

	void Editor::createLcd()
	{
		auto* lcdArea = findChild("lcdArea", false);

		if(!lcdArea)
			return;

		m_lcdCanvas = juceRmlUi::ElemCanvas::create(lcdArea);
		m_lcdCanvas->setClearEveryFrame(true);
		m_lcdCanvas->SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::Drag);
		m_lcdCanvas->setRepaintGraphicsCallback([this](const juce::Image& _image, juce::Graphics& _g)
		{
			paintLcd(_image, _g);
		});
		juceRmlUi::EventListener::Add(m_lcdCanvas, Rml::EventId::Mousemove,
			[this](Rml::Event& _event) { updateLcdHover(_event); });
		juceRmlUi::EventListener::Add(m_lcdCanvas, Rml::EventId::Mouseout,
			[this](Rml::Event&) { clearLcdHover(); });
		juceRmlUi::EventListener::Add(m_lcdCanvas, Rml::EventId::Mousedown,
			[this](Rml::Event& _event)
			{
				if(juceRmlUi::helper::getMouseButton(_event) != juceRmlUi::MouseButton::Left
					|| juceRmlUi::helper::isContextMenu(_event) || !m_lcdInteractionState)
					return;
				const auto target = lcdTargetAt(_event);
				if(!target)
					return;
				const auto mouse = juceRmlUi::helper::getMousePos(_event);
				(void)m_lcdDragGesture.begin(*m_lcdInteractionState, *target,
					mouse.x, mouse.y);
				// RmlUi arms its drag source only after mousedown propagation completes.
				// Stopping this event prevents every subsequent Drag event.
			});
		juceRmlUi::EventListener::Add(m_lcdCanvas, Rml::EventId::Drag,
			[this](Rml::Event& _event)
			{
				if(!m_lcdDragGesture.active() || !m_lcdInteractionState)
				{
					cancelLcdGesture();
					return;
				}
				const auto mouse = juceRmlUi::helper::getMousePos(_event);
				const auto percent = getProcessor().getConfig().getIntValue(
					"panelEncoderSpeedPercent", 100);
				const auto base = getModel() == md::MachineModel::Monomachine ? 150.0 : 120.0;
				const auto modifier = juceRmlUi::helper::getKeyModCommand(_event)
					? lcdInteraction::commandFineScale : 1.0;
				const auto steps = m_lcdDragGesture.drag(*m_lcdInteractionState,
					mouse.x, mouse.y,
					std::max(1, percent) / base * modifier, g_encoderBurstCap);
				if(const auto encoder = m_lcdDragGesture.encoder(); encoder && steps != 0)
					emitEncoderSteps(static_cast<md::PanelEncoder>(
						static_cast<unsigned>(md::PanelEncoder::DataEntryA) + *encoder), steps);
				_event.StopPropagation();
			});
		juceRmlUi::EventListener::Add(m_lcdCanvas, Rml::EventId::Mousescroll,
			[this](Rml::Event& _event)
			{
				const auto target = lcdTargetAt(_event);
				if(!target)
					return;
				if(m_lcdWheelEncoder != target)
				{
					m_lcdWheelEncoder = target;
					m_lcdWheelAccumulator.reset();
				}
				const auto steps = m_lcdWheelAccumulator.add(
					juceRmlUi::ElemKnob::mouseWheelValueDelta(
						g_encoderRange, _event),
					g_encoderBurstCap);
				if(steps != 0)
					emitEncoderSteps(static_cast<md::PanelEncoder>(
						static_cast<unsigned>(md::PanelEncoder::DataEntryA) + *target), steps);
				_event.StopPropagation();
			});
		m_lcdCanvas->repaint();

		// LED/LCD presentation follows the renderer at roughly 60 Hz. Firmware-facing
		// panel edges retain their established 33 ms cadence on a separate timer.
		startTimer(g_presentationTimerId,
			g_presentationTimerIntervalMilliseconds);
		startTimer(g_panelTimerId, g_panelTimerIntervalMilliseconds);
	}

	void Editor::updateLcdInteractionState()
	{
		const auto enabled = getProcessor().getConfig().getBoolValue(
			lcdInteraction::configKey, lcdInteraction::defaultEnabled);
		const auto oldState = m_lcdInteractionState;
		m_lcdInteractionState = enabled && m_frontPanelSnapshotValid
			? lcdInteraction::classify(m_frontPanelSnapshot, getModel(), m_encoderPress.active())
			: std::nullopt;
		m_lcdInteractionInputChanged = false;
		const auto identityChanged = oldState.has_value() != m_lcdInteractionState.has_value()
			|| (oldState && m_lcdInteractionState
				&& (oldState->identityToken != m_lcdInteractionState->identityToken
					|| oldState->layout != m_lcdInteractionState->layout
					|| oldState->activeEncoderMask != m_lcdInteractionState->activeEncoderMask));
		if(m_lcdDragGesture.active()
			&& (!m_lcdInteractionState || !m_lcdDragGesture.validFor(*m_lcdInteractionState)))
			cancelLcdGesture();
		if(identityChanged)
			clearLcdHover();
		else if(m_lcdHoverEncoder && (!m_lcdInteractionState
			|| (m_lcdInteractionState->activeEncoderMask & (1u << *m_lcdHoverEncoder)) == 0))
			clearLcdHover();
	}

	std::optional<unsigned> Editor::lcdTargetAt(const Rml::Event& _event) const
	{
		if(!m_lcdCanvas || !m_lcdInteractionState)
			return std::nullopt;
		const auto point = lcdNativePointAt(_event);
		if(!point)
			return std::nullopt;
		return lcdInteraction::hitTest(*m_lcdInteractionState, point->first, point->second);
	}

	std::optional<std::pair<int, int>> Editor::lcdNativePointAt(const Rml::Event& _event) const
	{
		if(!m_lcdCanvas)
			return std::nullopt;
		const auto mouse = juceRmlUi::helper::getMousePos(_event);
		auto offset = m_lcdCanvas->GetAbsoluteOffset(Rml::BoxArea::Content);
		auto display = m_lcdCanvas->GetBox().GetSize(Rml::BoxArea::Content);
		const auto pixelAligned = m_pixelPerfectPanel && m_pixelPerfectPanel->isEnabled();
		// Pixel-aligned canvases draw a snapped quad, which need not coincide with
		// the unsnapped layout box. Use the quad actually rendered for input too.
		if(pixelAligned)
			if(const auto rendered = m_lcdCanvas->getRenderedRect())
			{
				offset = rendered->origin;
				display = rendered->size;
			}
		auto paint = m_lcdCanvas->getPaintSize();
		// A pointer can arrive before the canvas has completed its first Render and
		// allocated a texture. In that brief interval the box is already laid out,
		// so its content size is the correct unpadded fallback paint size.
		if(paint.x <= 0 || paint.y <= 0)
			paint = {static_cast<int>(display.x), static_cast<int>(display.y)};
		const auto viewport = lcdInteraction::Viewport::create(display.x, display.y,
			paint.x, paint.y, pixelAligned);
		const auto point = viewport.displayToNative(mouse.x - offset.x, mouse.y - offset.y);
		if(!point)
			return std::nullopt;
		return std::pair<int, int>{ static_cast<int>(std::floor(point->x)), static_cast<int>(std::floor(point->y)) };
	}

	void Editor::updateLcdHover(const Rml::Event& _event)
	{
		const auto point = lcdNativePointAt(_event);
		const auto target = point && m_lcdInteractionState
			? lcdInteraction::hitTest(*m_lcdInteractionState, point->first, point->second) : std::nullopt;
		// The machine name is a tooltip source too. Allow a pixel of slack above and below the 7 px text.
		const auto& name = getModel() == md::MachineModel::Machinedrum ? lcdText::g_mdMachineName : lcdText::g_machineName;
		const lcdInteraction::NativeRect nameArea{ static_cast<int>(name.x), static_cast<int>(name.y) - 1,
			static_cast<int>(name.width), static_cast<int>(name.height) + 2 };
		m_tooltipLcdMachineName = point && nameArea.contains(point->first, point->second);
		if(target == m_lcdHoverEncoder)
			return;
		m_lcdHoverEncoder = target;
		m_lcdWheelEncoder.reset();
		m_lcdWheelAccumulator.reset();
		if(m_lcdCanvas)
		{
			if(target)
				m_lcdCanvas->SetProperty("cursor", "ns-resize");
			else
				m_lcdCanvas->RemoveProperty(Rml::PropertyId::Cursor);
		}
	}

	void Editor::clearLcdHover()
	{
		m_tooltipLcdMachineName = false;
		if(!m_lcdHoverEncoder && !m_lcdWheelEncoder)
			return;
		m_lcdHoverEncoder.reset();
		m_lcdWheelEncoder.reset();
		m_lcdWheelAccumulator.reset();
		if(m_lcdCanvas)
			m_lcdCanvas->RemoveProperty(Rml::PropertyId::Cursor);
	}

	void Editor::cancelLcdGesture()
	{
		m_lcdDragGesture.cancel();
	}

	void Editor::applyPixelPerfectPanel()
	{
		if (auto* component = getRmlComponent())
		{
			if (!m_pixelPerfectPanel)
				m_pixelPerfectPanel = std::make_unique<PixelPerfectPanel>();
			m_pixelPerfectPanel->apply(*component, m_lcdCanvas,
				getProcessor().getConfig().getBoolValue(PixelPerfectPanel::configKey, PixelPerfectPanel::defaultEnabled));
		}
	}

	void Editor::applyLcdInteraction()
	{
		m_lcdInteractionInputChanged = true;
		updateLcdInteractionState();
	}

	void Editor::createButtons()
	{
		cancelPanelInputGestures();
		const auto model = getModel();

		for (const auto& pb : g_panelButtons)
		{
			auto* b = findChild<juceRmlUi::ElemButton>(pb.id, false);
			if(!b)
				continue;

			const auto packet = md::panelPacket(model, pb.control);
			if(!packet)
			{
				b->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				continue;
			}

			// On the hardware, A/E through D/H are held while a trig key chooses the
			// pattern number. A normal MM bank click therefore keeps its existing latch.
			// When another Shift-held control is active, the bank acts as an ordinary
			// momentary target so chords such as FUNCTION + BANK remain exact.
			if(model == md::MachineModel::Monomachine
				&& panelAffordances::isPatternBank(pb.control))
			{
				b->SetAttribute("title",
					"Click to hold this bank until a trig; Shift uses the same bank latch");
				juceRmlUi::EventListener::Add(b, Rml::EventId::Mousedown,
					[this, b, packet, control = pb.control](Rml::Event& _event)
				{
					const bool shiftDown = _event.GetParameter<int>("shift_key", 0) != 0;
					if(!shiftDown && !m_shiftPanelLatch.empty())
						releasePanelButtonGestures();
					if(panelAffordances::usesPersistentPatternBankLatch(getModel(),
						control, !m_shiftPanelLatch.empty()))
						togglePatternBankLatch(b, *packet);
					else
						pressPanelButton(b, control, *packet, shiftDown);
				});
				const auto release = [this, b, packet, control = pb.control](Rml::Event&)
				{
					releasePanelButton(b, control, *packet);
				};
				juceRmlUi::EventListener::Add(b, Rml::EventId::Mouseup, release);
				juceRmlUi::EventListener::Add(b, Rml::EventId::Mouseout, release);
				continue;
			}

			if(isTrigger(pb.control))
				b->SetAttribute("title",
					"Shift-click to hold this trig; release Shift to let go");
			else
				b->SetAttribute("title",
					"Shift-click to hold; use another control; release Shift to let go");

			juceRmlUi::EventListener::Add(b, Rml::EventId::Mousedown,
				[this, b, packet, control = pb.control](Rml::Event& _event)
			{
				pressPanelButton(b, control, *packet,
					_event.GetParameter<int>("shift_key", 0) != 0);
			});

			// Mouseout releases too, otherwise dragging off a button leaves it held.
			const auto release = [this, b, packet, control = pb.control](Rml::Event&)
			{
				releasePanelButton(b, control, *packet);
			};
			juceRmlUi::EventListener::Add(b, Rml::EventId::Mouseup, release);
			juceRmlUi::EventListener::Add(b, Rml::EventId::Mouseout, release);
		}

		if(auto* const document = getDocument())
		{
			juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup,
				[this](const Rml::Event& _event)
				{
					if(!juceRmlUi::helper::getKeyModAlt(_event))
						releaseEncoderPress();
					if(_event.GetParameter<int>("shift_key", 0) == 0
						&& !m_shiftPanelLatch.empty())
						releasePanelButtonGestures();
				});
			juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown,
				[this](Rml::Event& _event)
				{
					if(juceRmlUi::helper::getKeyIdentifier(_event) != Rml::Input::KI_ESCAPE
						|| (m_shiftPanelLatch.empty() && m_activePanelButtons.empty()
							&& m_panelGesturePackets.empty() && !m_patternBankPacket
							&& !m_encoderPress.active() && !m_lcdDragGesture.active()))
						return;
					_event.StopPropagation();
					cancelPanelInputGestures();
				});
			juceRmlUi::EventListener::Add(document, Rml::EventId::Mouseup,
				[this](Rml::Event& _event)
				{
					if(juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Left)
					{
						releaseEncoderPress();
						cancelLcdGesture();
					}
				});
			juceRmlUi::EventListener::Add(document, Rml::EventId::Dragend,
				[this](Rml::Event&)
				{
					releaseEncoderPress();
					cancelLcdGesture();
				});
		}
	}

	void Editor::pressPanelButton(juceRmlUi::ElemButton* const _button,
		const md::PanelControl _control, const md::PanelPacket& _packet,
		const bool _shiftDown)
	{
		if(!_button || _button->isChecked())
			return;

		// A missing native key-up must never let an earlier hold leak into a new,
		// unmodified click before the timer fail-safe gets its next turn.
		if(!_shiftDown && !m_shiftPanelLatch.empty())
			releasePanelButtonGestures();

		const auto action = m_shiftPanelLatch.press(_control, _shiftDown);
		if(action == panelAffordances::ShiftPanelLatch::PressAction::Ignored)
			return;

		if(getModel() == md::MachineModel::Monomachine && !isTrigger(_control))
			releasePatternBankLatch();

		juceRmlUi::ElemButton::setChecked(_button, true);
		if(action == panelAffordances::ShiftPanelLatch::PressAction::Momentary)
			m_activePanelButtons.push_back({ _button, _packet });

		const auto combined = m_panelRows.press(_packet);
		(void)sendPanelEvent(combined.row, combined.mask);
	}

	void Editor::releasePanelButton(juceRmlUi::ElemButton* const _button,
		const md::PanelControl _control, const md::PanelPacket& _packet)
	{
		if(m_shiftPanelLatch.contains(_control))
			return;

		const auto it = std::find_if(m_activePanelButtons.begin(), m_activePanelButtons.end(),
			[_button](const ActivePanelButton& _active) { return _active.button == _button; });
		if(it == m_activePanelButtons.end())
			return;

		m_activePanelButtons.erase(it);
		juceRmlUi::ElemButton::setChecked(_button, false);
		const auto combined = m_panelRows.release(_packet);
		(void)sendPanelEvent(combined.row, combined.mask);
		if(getModel() == md::MachineModel::Monomachine && isTrigger(_control))
			releasePatternBankLatch();
	}

	void Editor::releaseActivePanelButtons()
	{
		while(!m_activePanelButtons.empty())
		{
			const auto active = m_activePanelButtons.back();
			m_activePanelButtons.pop_back();
			if(active.button)
				juceRmlUi::ElemButton::setChecked(active.button, false);
			const auto combined = m_panelRows.release(active.packet);
			(void)sendPanelEvent(combined.row, combined.mask);
		}
	}

	void Editor::createPanelAffordances()
	{
		const auto bindChordList = [this](const auto& _shortcuts)
		{
			for(const auto& shortcut : _shortcuts)
				bindPanelChord(shortcut.id, shortcut.control);
		};

		const auto bindPage = [this](const char* const _id, const auto _select)
		{
			auto* element = findChild(_id, false);
			if(!element)
				return;
			element->SetClass(panelAffordances::g_affordanceClass, true);
			juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [this, _select](Rml::Event&)
			{
				releasePanelButtonGestures();
				_select();
			});
		};

		if(getModel() == md::MachineModel::Machinedrum)
		{
			for(int track = 0; track < 16; ++track)
			{
				const auto id = std::to_string(track);
				bindPage((panelAffordances::g_drumLedPrefix + id).c_str(),
					[this, track] { selectMachinedrumTrack(track); });
				bindPage((panelAffordances::g_trackLabelPrefix + id).c_str(),
					[this, track] { selectMachinedrumTrack(track); });
			}

			bindChordList(panelAffordances::g_machinedrumShortcuts);

			for(size_t page = 0; page < panelAffordances::g_machinedrumDataPages.size(); ++page)
				bindPage(panelAffordances::g_machinedrumDataPages[page],
					[this, page] { selectMachinedrumDataPage(static_cast<int>(page)); });
			return;
		}

		for(int track = 0; track < 6; ++track)
		{
			const auto control = static_cast<md::PanelControl>(
				static_cast<int>(md::PanelControl::Track1) + track);
			const auto id = std::to_string(track);
			bindPanelTarget((panelAffordances::g_drumLedPrefix + id).c_str(), control);
			bindPanelTarget((panelAffordances::g_trackLabelPrefix + id).c_str(), control);
			bindPanelChord((panelAffordances::g_trackMutePrefix + id).c_str(), control);
		}

		bindChordList(panelAffordances::g_monomachineShortcuts);

		for(size_t page = 0; page < panelAffordances::g_monomachineDataPages.size(); ++page)
			bindPage(panelAffordances::g_monomachineDataPages[page],
				[this, page] { selectMonomachineDataPage(static_cast<int>(page)); });

		for(size_t mode = 0; mode < panelAffordances::g_monomachineTrigModes.size(); ++mode)
			bindPage(panelAffordances::g_monomachineTrigModes[mode],
				[this, mode] { selectMonomachineTrigMode(static_cast<int>(mode)); });
	}

	void Editor::bindPanelTarget(const char* const _id, const md::PanelControl _control)
	{
		auto* element = findChild(_id, false);
		if(!element)
			return;

		element->SetClass(panelAffordances::g_affordanceClass, true);
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown,
			[this, element, _control](Rml::Event&)
		{
			beginPanelGesture(element, { _control });
		});

		const auto release = [this](Rml::Event&) { endPanelGesture(); };
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mouseup, release);
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mouseout, release);
	}

	void Editor::bindPanelChord(const char* const _id, const md::PanelControl _control)
	{
		auto* element = findChild(_id, false);
		if(!element)
			return;

		element->SetClass(panelAffordances::g_affordanceClass, true);
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown,
			[this, element, _control](Rml::Event&)
		{
			beginPanelGesture(element, { md::PanelControl::Function, _control });
		});

		const auto release = [this](Rml::Event&) { endPanelGesture(); };
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mouseup, release);
		juceRmlUi::EventListener::Add(element, Rml::EventId::Mouseout, release);
	}

	void Editor::beginPanelGesture(Rml::Element* const _element,
		const std::initializer_list<md::PanelControl> _controls)
	{
		// Direct labels own their complete gesture. Ending an existing Shift hold
		// avoids duplicate row bits and accidental three-control chords.
		releasePanelButtonGestures();
		endPanelGesture();

		m_panelGestureElement = _element;
		m_panelGestureElement->SetClass("active", true);

		for(const auto control : _controls)
		{
			const auto packet = md::panelPacket(getModel(), control);
			if(!packet)
				continue;

			m_panelGesturePackets.push_back(*packet);
			const auto combined = m_panelRows.press(*packet);
			(void)sendPanelEvent(combined.row, combined.mask);
		}
	}

	void Editor::endPanelGesture()
	{
		if(m_panelGestureElement)
			m_panelGestureElement->SetClass("active", false);

		// Release in reverse order so a chord lets go of the target before FUNCTION.
		for(auto it = m_panelGesturePackets.rbegin(); it != m_panelGesturePackets.rend(); ++it)
		{
			const auto combined = m_panelRows.release(*it);
			(void)sendPanelEvent(combined.row, combined.mask);
		}

		m_panelGesturePackets.clear();
		m_panelGestureElement = nullptr;
	}

	void Editor::releasePanelButtonGestures()
	{
		releaseEncoderPress();
		// Finish every momentary target before its Shift-held modifier. This also
		// makes a later mouse-up harmless when key-up or focus loss ends the gesture.
		releaseActivePanelButtons();

		m_shiftPanelLatch.releaseAll([this](const md::PanelControl _control)
		{
			for(const auto& panelButton : g_panelButtons)
			{
				if(panelButton.control != _control)
					continue;
				if(auto* const button = findChild<juceRmlUi::ElemButton>(panelButton.id, false))
					juceRmlUi::ElemButton::setChecked(button, false);
				break;
			}

			if(const auto packet = md::panelPacket(getModel(), _control))
			{
				const auto combined = m_panelRows.release(*packet);
				(void)sendPanelEvent(combined.row, combined.mask);
			}
		});

		// A pattern bank acts as the modifier in the MM bank + trig chord. Let go
		// of every target trig before releasing that modifier.
		releasePatternBankLatch();
	}

	void Editor::cancelPanelInputGestures()
	{
		cancelLcdGesture();
		endPanelGesture();
		releasePanelButtonGestures();
		releaseAllPanelInputs();
	}

	void Editor::releaseEncoderPress()
	{
		const auto wasActive = m_encoderPress.active();
		if(const auto packet = m_encoderPress.release())
		{
			const auto combined = m_panelRows.release(*packet);
			(void)sendPanelEvent(combined.row, combined.mask);
		}
		if(m_pressedEncoder)
			m_pressedEncoder->SetClass("encoderPressed", false);
		m_pressedEncoder = nullptr;
		if(wasActive)
		{
			// The held-switch state is a classifier input. Restore hit targets
			// immediately even if a short press never produced an LCD redraw.
			m_lcdInteractionInputChanged = true;
			updateLcdInteractionState();
		}
	}

	void Editor::globalFocusChanged(juce::Component* const _focusedComponent)
	{
		auto* const panel = getRmlComponent();
		if(panel && _focusedComponent
			&& (_focusedComponent == panel || panel->isParentOf(_focusedComponent)))
			return;

		resetKeyboardControl();
		cancelPanelInputGestures();
	}

	void Editor::releaseAllPanelInputs()
	{
		for(uint8_t row = 0x20; row <= 0x26; ++row)
			if(m_panelRows.mask(row) != 0)
				(void)sendPanelEvent(row, 0);
		m_panelRows.reset();
	}

	void Editor::queuePanelPulse(const md::PanelControl _control, const int _count)
	{
		if(_count <= 0)
			return;

		const auto packet = md::panelPacket(getModel(), _control);
		if(!packet)
			return;

		releasePatternBankLatch();

		for(int i = 0; i < _count; ++i)
		{
			m_panelSteps.push_back({ *packet, true });
			m_panelSteps.push_back({ *packet, false });
		}
	}

	// One step per timer tick, so the firmware sees distinct press and release edges.
	void Editor::servicePanelQueue()
	{
		if(m_panelSteps.empty())
		{
			servicePanelNavigation();
			return;
		}

		const auto step = m_panelSteps.front();

		const auto combined = step.press ? m_panelRows.press(step.packet) : m_panelRows.release(step.packet);
		// Navigation pulses are retryable: do not advance to the matching release
		// until this row state actually entered the bounded FIFO.
		if(!sendPanelEvent(combined.row, combined.mask))
			return;
		m_panelSteps.pop_front();

		// Give the firmware a complete timer interval to update its LED readback
		// before deciding whether the pending direct-selection target needs another
		// pulse. This also makes a rapid replacement request use observed state.
		if(m_panelSteps.empty() && !step.press)
			m_panelSettleTicks = 1;
	}

	void Editor::servicePanelNavigation()
	{
		if(!m_panelSteps.empty())
			return;

		if(m_panelSettleTicks > 0)
		{
			--m_panelSettleTicks;
			return;
		}

		if(!m_frontPanelSnapshotValid)
			return;
		const auto& frontPanel = m_frontPanelSnapshot;

		if(getModel() == md::MachineModel::Machinedrum)
		{
			const auto target = m_machinedrumDataPageTarget.target();
			if(!target)
				return;

			const auto current = currentMachinedrumDataPage();
			if(!current || m_machinedrumDataPageTarget.completeIfAt(*current))
				return;

			const auto plan = panelAffordances::machinedrumDataPagePlan(*current, *target);
			if(plan && m_machinedrumDataPageTarget.beginAttempt())
				queuePanelPulse(plan->control);
			return;
		}

		const auto dataTarget = m_monomachineDataPageTarget.target();
		if(dataTarget)
		{
			const auto current = currentMonomachineDataPage();
			if(current && !m_monomachineDataPageTarget.completeIfAt(*current))
			{
				const auto plan = panelAffordances::monomachineDataPagePlan(*current, *dataTarget);
				if(plan && m_monomachineDataPageTarget.beginAttempt())
				{
					queuePanelPulse(plan->control);
					return;
				}
			}
		}

		const auto modeTarget = m_monomachineTrigModeTarget.target();
		if(!modeTarget)
			return;

		const auto raw = frontPanel.getLedBankRaw(0x27);
		const bool amp = (raw & (1u << 1)) == 0;
		const bool filter = (raw & (1u << 2)) == 0;
		const bool lfo = (raw & (1u << 3)) == 0;
		const std::array<bool, panelAffordances::g_monomachineTrigModes.size()> active
		{{
			amp && !filter && !lfo,
			!amp && filter && !lfo,
			!amp && !filter && lfo,
			amp && filter && lfo,
		}};

		const auto current = panelAffordances::singleActiveIndex(active);
		if(!current || m_monomachineTrigModeTarget.completeIfAt(*current))
			return;

		const auto plan = panelAffordances::monomachineTrigModePlan(*current, *modeTarget);
		if(plan && m_monomachineTrigModeTarget.beginAttempt())
			queuePanelPulse(plan->control);
	}

	void Editor::selectMachinedrumTrack(const int _track)
	{
		const auto body = md::midiProtocol::selectTrack(_track);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.reserve(body.size() + 2);
		event.sysex.push_back(0xf0);
		event.sysex.insert(event.sysex.end(), body.begin(), body.end());
		event.sysex.push_back(0xf7);
		getProcessor().addMidiEvent(event);
	}

	void Editor::selectMachinedrumDataPage(const int _page)
	{
		if(m_machinedrumDataPageTarget.request(_page))
			servicePanelNavigation();
	}

	void Editor::selectMonomachineDataPage(const int _page)
	{
		if(m_monomachineDataPageTarget.request(_page))
			servicePanelNavigation();
	}

	void Editor::selectMonomachineTrigMode(const int _mode)
	{
		if(m_monomachineTrigModeTarget.request(_mode))
			servicePanelNavigation();
	}

	void Editor::togglePatternBankLatch(juceRmlUi::ElemButton* const _button, const md::PanelPacket& _packet)
	{
		const auto wasLatched = m_patternBankButton == _button;
		releasePatternBankLatch();
		if(wasLatched)
			return;

		m_patternBankButton = _button;
		m_patternBankPacket = _packet;
		juceRmlUi::ElemButton::setChecked(_button, true);

		const auto combined = m_panelRows.press(_packet);
		(void)sendPanelEvent(combined.row, combined.mask);
	}

	void Editor::releasePatternBankLatch()
	{
		if(!m_patternBankPacket)
			return;

		if(m_patternBankButton)
			juceRmlUi::ElemButton::setChecked(m_patternBankButton, false);

		const auto combined = m_panelRows.release(*m_patternBankPacket);
		(void)sendPanelEvent(combined.row, combined.mask);

		m_patternBankButton = nullptr;
		m_patternBankPacket.reset();
	}

	void Editor::createEncoders()
	{
		static const char* const ids[8] = { "encA","encB","encC","encD","encE","encF","encG","encH" };

		for(uint32_t i=0; i<8; ++i)
		{
			auto* k = findChild<juceRmlUi::ElemKnob>(ids[i], false);
			m_encoders[i] = k;
			configureEncoder(k, static_cast<md::PanelEncoder>(i), m_encLast[i], m_encAccum[i]);
		}

		m_levelEncoder = findChild<juceRmlUi::ElemKnob>("encLevel", false);
		configureEncoder(m_levelEncoder, md::PanelEncoder::Level, m_levelLast, m_levelAccum);

		m_soundEncoder = findChild<juceRmlUi::ElemKnob>("encSound", false);
		configureEncoder(m_soundEncoder, md::PanelEncoder::SoundSelection,
			m_soundLast, m_soundAccum);
	}

	std::string Editor::getSettingsTemplateSuffix() const
	{
		return getModel() == md::MachineModel::Monomachine ? "Monomachine" : "Machinedrum";
	}

	std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> Editor::createDeviceSpecificSettings(
		const std::string& _templateName, Rml::Element* _root)
	{
		if (_templateName == "tus_settings_gui_Machinedrum" || _templateName == "tus_settings_gui_Monomachine")
			return std::make_unique<SettingsPanelFeel>(*this, _root);
		if (_templateName == "tus_settings_dspaudio_Machinedrum" || _templateName == "tus_settings_dspaudio_Monomachine")
			return std::make_unique<SettingsAudioInput>(getProcessor(), _root);
		return jucePluginEditorLib::Editor::createDeviceSpecificSettings(_templateName, _root);
	}

	void Editor::applyTooltipSettings()
	{
		auto& config = getProcessor().getConfig();
		m_tooltipsEnabled = config.getBoolValue(g_tooltipsEnabledKey, true);
		m_tooltipDelayMs = std::max(0, config.getIntValue(g_tooltipDelayKey, g_defaultTooltipDelayMs));
		updateParameterTooltip();
	}

	void Editor::applyPanelSpeeds()
	{
		auto& config = getProcessor().getConfig();
		const auto wheelPercent = config.getIntValue("panelWheelSpeedPercent", 100);
		const auto encoderPercent = config.getIntValue("panelEncoderSpeedPercent", 100);

		// The knob "speed" property is the mouse distance for a full sweep, so a
		// higher user-facing percentage means a smaller property value. Base values
		// mirror the skins' RCSS defaults.
		const auto isMonomachine = getModel() == md::MachineModel::Monomachine;

		// juceRmlUi::Element::getProperty() reads the attribute before the RCSS
		// property, so setting the attribute both overrides the skin default and
		// raises the change notification that refreshes the knob's cached speed.
		const auto apply = [](juceRmlUi::ElemKnob* const _knob, const float _baseSpeed, const int _percent)
		{
			if (!_knob || _percent <= 0)
				return;
			_knob->SetAttribute("speed", _baseSpeed * 100.0f / static_cast<float>(_percent));
		};

		const float encoderBase = isMonomachine ? 150.0f : 120.0f;
		for (auto* knob : m_encoders)
			apply(knob, encoderBase, encoderPercent);
		apply(m_levelEncoder, isMonomachine ? 150.0f : 100.0f, encoderPercent);
		apply(m_soundEncoder, 1360.0f, wheelPercent);
	}

	void Editor::loadInstalledFactoryStorage()
	{
		if(m_model != md::MachineModel::Monomachine)
			return;

		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor)
			return;

		const auto isExactStorage = [](const juce::File& _file)
		{
			return _file.existsAsFile()
				&& _file.getSize() == static_cast<juce::int64>(md::g_patchRamStateSize);
		};

		const auto configuredPath = getProcessor().getConfig().getValue(
			"mmFactoryStoragePath");
		const auto configured = configuredPath.isNotEmpty()
			? juce::File(configuredPath) : juce::File{};
		if(isExactStorage(configured))
		{
			confirmStorageImage(configured, StorageImageBookmark::Factory);
			return;
		}

		const auto conventional = processor->getInstalledFactoryStorageImage();
		if(isExactStorage(conventional))
		{
			confirmStorageImage(conventional, StorageImageBookmark::Factory);
			return;
		}

		// Factory content is user-supplied and is never embedded in the product.
		// The first successful selection becomes a convenient remembered slot.
		chooseStorageImage(StorageImageBookmark::Factory);
	}

	void Editor::chooseStorageImage()
	{
		chooseStorageImage(StorageImageBookmark::Other);
	}

	void Editor::chooseStorageImage(const StorageImageBookmark _bookmark)
	{
		if(m_model != md::MachineModel::Monomachine)
			return;
		if(!jucePluginEditorLib::fileChooserFlow::tryBegin(m_storageImageFlow,
			StorageImageFlow::None, StorageImageFlow::Choosing))
		{
			showStorageOperationResult(false,
				"Finish the open storage image dialog first.");
			return;
		}

		auto& config = getProcessor().getConfig();
		const auto factorySelection = _bookmark == StorageImageBookmark::Factory;
		const auto lastPath = config.getValue(factorySelection
			? "mmFactoryStoragePath" : "mmStorageImageLastPath");
		const auto lastDirectory = factorySelection ? juce::String{}
			: config.getValue("mmStorageImageLastDirectory");
		juce::File initial;
		if(lastPath.isNotEmpty())
		{
			const juce::File remembered(lastPath);
			initial = remembered.existsAsFile()
				? remembered : remembered.getParentDirectory();
		}
		if(!initial.existsAsFile() && lastDirectory.isNotEmpty())
			initial = juce::File(lastDirectory);
		if(initial == juce::File())
		{
			if(auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor()))
				initial = processor->getInstalledFactoryStorageImage().getParentDirectory();
		}
		if(!initial.exists())
			initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

		m_storageFileChooser = std::make_unique<juce::FileChooser>(
			factorySelection
				? "Choose an exact 1 MiB factory storage image"
				: "Choose an exact 1 MiB storage image",
			initial, "*.bin", true);
		const auto safeRoot =
			juce::Component::SafePointer<juceRmlUi::RmlComponent>(getRmlComponent());
		const std::function<void(const juce::FileChooser&)> completion =
			jucePluginEditorLib::fileChooserFlow::makeGuardedCompletion(
				safeRoot, &m_storageImageFlow, StorageImageFlow::Choosing,
				StorageImageFlow::None,
				[this, _bookmark](const juce::FileChooser& _chooser)
				{
					const auto file = _chooser.getResult();
					if(!file.existsAsFile())
						return;
					confirmStorageImage(file, _bookmark);
				});
		m_storageFileChooser->launchAsync(
			juce::FileBrowserComponent::openMode
				| juce::FileBrowserComponent::canSelectFiles,
			completion);
	}

	void Editor::restorePreviousStorage()
	{
		if(m_model != md::MachineModel::Monomachine)
			return;
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor)
			return;
		const auto recovery = processor->getStorageRecoveryImage();
		if(!recovery.existsAsFile()
			|| recovery.getSize() != static_cast<juce::int64>(md::g_patchRamStateSize))
		{
			showStorageOperationResult(false,
				"No complete 1 MiB recovery image is available yet.\n\nExpected at:\n"
				+ recovery.getFullPathName());
			return;
		}
		confirmStorageImage(recovery, StorageImageBookmark::None);
	}

	bool Editor::hasStorageRecoveryImage() const
	{
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor || m_model != md::MachineModel::Monomachine)
			return false;
		const auto recovery = processor->getStorageRecoveryImage();
		return recovery.existsAsFile()
			&& recovery.getSize() == static_cast<juce::int64>(md::g_patchRamStateSize);
	}

	void Editor::confirmStorageImage(const juce::File& _file,
		const StorageImageBookmark _bookmark)
	{
		if(!jucePluginEditorLib::fileChooserFlow::tryBegin(m_storageImageFlow,
			StorageImageFlow::None, StorageImageFlow::AwaitingConfirmation))
		{
			showStorageOperationResult(false,
				"Finish the open storage image dialog first.");
			return;
		}

		if(!_file.existsAsFile()
			|| _file.getSize() != static_cast<juce::int64>(md::g_patchRamStateSize))
		{
			m_storageImageFlow = StorageImageFlow::None;
			showStorageOperationResult(false,
				"Storage was not changed. The selected image must be exactly 1 MiB.");
			return;
		}
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor)
		{
			m_storageImageFlow = StorageImageFlow::None;
			return;
		}
		const auto recoveryPath = processor->getStorageRecoveryImage().getFullPathName();

		const auto safeRoot =
			juce::Component::SafePointer<juceRmlUi::RmlComponent>(getRmlComponent());
		const genericUI::MessageBox::Callback completion =
			jucePluginEditorLib::fileChooserFlow::makeGuardedCompletion(
				safeRoot, &m_storageImageFlow,
				StorageImageFlow::AwaitingConfirmation, StorageImageFlow::None,
				[this, file = _file, _bookmark](
					const genericUI::MessageBox::Result _answer)
				{
					if(_answer != genericUI::MessageBox::Result::Yes)
						return;

					auto* const processor =
						dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
					if(!processor)
						return;
					juce::String result;
					const bool loaded = processor->loadStorageImage(file, result);
					if(loaded && _bookmark != StorageImageBookmark::None)
					{
						auto& config = getProcessor().getConfig();
						if(_bookmark == StorageImageBookmark::Factory)
							config.setValue("mmFactoryStoragePath",
								file.getFullPathName());
						else
						{
							config.setValue("mmStorageImageLastPath",
								file.getFullPathName());
							config.setValue("mmStorageImageLastDirectory",
								file.getParentDirectory().getFullPathName());
						}
						config.saveIfNeeded();
					}
					showStorageOperationResult(loaded, result);
				});

		const auto message = "Load '" + _file.getFileName()
			+ "'?\n\nThis replaces every kit, pattern, song, and global in "
				"machine storage, then reboots the machine.\n\n"
				"A recovery copy of the current 1 MiB storage will be written first. "
				"If that backup cannot be saved, nothing will be changed.\n\nRecovery file:\n"
			+ recoveryPath;
		genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Warning,
			"Replace machine storage?", message.toStdString(), completion);
	}

	void Editor::chooseFirmwareImage()
	{
		if(m_firmwareDialogOpen)
			return;
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor)
			return;
		auto& config = getProcessor().getConfig();
		juce::File initial;
		const auto current = processor->getFirmwareImagePath();
		if(!current.empty())
			initial = juce::File(current).getParentDirectory();
		if(!initial.exists())
		{
			const auto lastDirectory = config.getValue("firmwareImageLastDirectory");
			if(lastDirectory.isNotEmpty())
				initial = juce::File(lastDirectory);
		}
		if(!initial.exists())
			initial = juce::File(getProcessor().getPublicRomFolder());
		if(!initial.exists())
			initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

		m_firmwareDialogOpen = true;
		m_firmwareFileChooser = std::make_unique<juce::FileChooser>(
			std::string("Choose an 8 MiB ") + getSettingsTemplateSuffix() + " firmware image",
			initial, "*.bin", true);
		const auto safeRoot =
			juce::Component::SafePointer<juceRmlUi::RmlComponent>(getRmlComponent());
		m_firmwareFileChooser->launchAsync(
			juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this, safeRoot](const juce::FileChooser& _chooser)
			{
				if(!safeRoot)
					return;
				m_firmwareDialogOpen = false;
				const auto file = _chooser.getResult();
				if(!file.existsAsFile())
					return;
				getProcessor().getConfig().setValue("firmwareImageLastDirectory",
					file.getParentDirectory().getFullPathName());
				confirmFirmwareImage(file.getFullPathName().toStdString());
			});
	}

	void Editor::useStockFirmware()
	{
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!processor || processor->getFirmwareImagePath().empty())
			return;
		confirmFirmwareImage({});
	}

	void Editor::confirmFirmwareImage(const std::string& _path)
	{
		const auto safeRoot =
			juce::Component::SafePointer<juceRmlUi::RmlComponent>(getRmlComponent());
		const auto name = _path.empty() ? std::string("the stock OS")
			: baseLib::filesystem::getFilenameWithoutPath(_path);
		genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question,
			std::string("Switch ") + getSettingsTemplateSuffix() + " firmware",
			"Restart the machine with " + name + "?\n\n"
			"The machine boots from a fresh factory state on the new OS. Kits, patterns and "
			"songs of the running machine are not carried over, save them via SysEx first if "
			"you need them. The image becomes the default for new instances and is stored with "
			"the project.",
			[this, safeRoot, _path](const genericUI::MessageBox::Result _answer)
			{
				if(!safeRoot || _answer != genericUI::MessageBox::Result::Yes)
					return;
				auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
				if(!processor)
					return;
				std::string error;
				if(processor->setFirmwareImage(_path, error))
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Firmware switched",
						"The machine is now running " + processor->getFirmwareDescription(), getRmlComponent());
				else
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Firmware unchanged",
						"The image was not loaded: " + error, getRmlComponent());
			});
	}

	std::string Editor::getFirmwareDescription() const
	{
		auto* const processor = dynamic_cast<const AudioPluginAudioProcessor*>(&getProcessor());
		return processor ? processor->getFirmwareDescription() : std::string{};
	}

	void Editor::showStorageOperationResult(const bool _success,
		const juce::String& _message)
	{
		genericUI::MessageBox::showOk(_success
				? genericUI::MessageBox::Icon::Info
				: genericUI::MessageBox::Icon::Warning,
			_success ? "Machine storage loaded" : "Machine storage unchanged",
			_message.toStdString(), getRmlComponent());
	}

	std::optional<md::SysexImportProgress> Editor::getUserSysexProgress() const
	{
		return getProcessor().getPlugin().withDeviceLocked(
			[](synthLib::Device* const _device)
				-> std::optional<md::SysexImportProgress>
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				if(!device)
					return std::nullopt;
				return device->userSysexImportProgress();
			});
	}

	bool Editor::isUserSysexTransferActive() const
	{
		const auto progress = getUserSysexProgress();
		if(!progress)
			return false;
		return progress->state == md::MidiSysexTransferState::Queued
			|| progress->state == md::MidiSysexTransferState::NegotiatingTurbo
			|| progress->state == md::MidiSysexTransferState::WaitingForDevice
			|| progress->state == md::MidiSysexTransferState::Retrying
			|| progress->state == md::MidiSysexTransferState::WaitingForReceiveMode
			|| progress->state == md::MidiSysexTransferState::Sending
			|| progress->state == md::MidiSysexTransferState::Cancelling;
	}

	bool Editor::canCancelUserSysexTransfer() const
	{
		const auto progress = getUserSysexProgress();
		if(!progress)
			return false;
		return progress->state == md::MidiSysexTransferState::Queued
			|| progress->state == md::MidiSysexTransferState::NegotiatingTurbo
			|| progress->state == md::MidiSysexTransferState::WaitingForDevice
			|| progress->state == md::MidiSysexTransferState::Retrying
			|| progress->state == md::MidiSysexTransferState::WaitingForReceiveMode
			|| progress->state == md::MidiSysexTransferState::Sending;
	}

	std::string Editor::getUserSysexMenuText() const
	{
		const auto progress = getUserSysexProgress();
		if(!progress)
			return "Send SysEx File...";
		if(progress->state == md::MidiSysexTransferState::Cancelling)
			return "Cancelling SysEx Transfer...";
		if(progress->state == md::MidiSysexTransferState::Queued
			|| progress->state == md::MidiSysexTransferState::NegotiatingTurbo)
			return "Cancel SysEx Transfer - negotiating TurboMIDI...";
		if(progress->state == md::MidiSysexTransferState::WaitingForReceiveMode)
			return "Cancel SysEx Transfer - waiting for machine readiness...";
		if(progress->state == md::MidiSysexTransferState::WaitingForDevice)
			return "Cancel SysEx Transfer - waiting for sample acknowledgement...";
		if(progress->state == md::MidiSysexTransferState::Retrying)
			return "Cancel SysEx Transfer - retrying sample packet...";
		if(progress->state == md::MidiSysexTransferState::Sending)
		{
			const auto percent = progress->total == 0 ? size_t{0}
				: std::min<size_t>(100, (progress->sent * 100) / progress->total);
			return "Cancel SysEx Transfer... " + std::to_string(percent) + "%";
		}
		return "Send SysEx File...";
	}

	void Editor::cancelUserSysexTransfer()
	{
		const auto progress = getUserSysexProgress();
		if(!progress) return;
		std::vector<uint8_t> retiredPayload;
		const bool cancelled = getProcessor().getPlugin().withDeviceLocked(
			[&](synthLib::Device* const _device)
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				return device && device->cancelUserSysexImport(progress->ticket, retiredPayload);
			});
		// retiredPayload is intentionally destroyed here, after withDeviceLocked()
		// has returned, so cancellation never frees file-sized storage on audio time.
		if(!cancelled)
			showUserSysexError("The transfer was no longer active.");
	}

	bool Editor::canResumeUserSysexTransfer() const
	{
		const auto progress = getUserSysexProgress();
		return progress && progress->state == md::MidiSysexTransferState::WaitingForReceiveMode;
	}

	void Editor::resumeUserSysexTransfer()
	{
		const auto progress = getUserSysexProgress();
		if(!progress) return;
		getProcessor().getPlugin().withDeviceLocked([&](synthLib::Device* base)
		{
			auto* device = dynamic_cast<md::Device*>(base);
			if(device) device->resumeUserSysexImport(progress->ticket, progress->transferId, progress->receiveStep, true);
		});
	}

	void Editor::chooseUserSysexFile()
	{
		if(m_sysexChooserOpen)
		{
			showUserSysexError("Finish the open SysEx file dialog first.");
			return;
		}
		if(isUserSysexTransferActive())
		{
			showUserSysexError("A SysEx file is already being sent.");
			return;
		}

		const auto ticket = beginUserSysexTicket();
		if(!ticket)
			return;
		m_sysexChooserOpen = true;
		launchUserSysexFileChooser(*ticket);
	}

	std::optional<md::SysexImportTicket> Editor::beginUserSysexTicket()
	{
		const auto ticket = getProcessor().getPlugin().withDeviceLocked(
			[](synthLib::Device* base) -> std::optional<md::SysexImportTicket>
			{
				auto* device = dynamic_cast<md::Device*>(base);
				return device ? device->beginUserSysexImport() : std::nullopt;
			});
		if(!ticket)
			showUserSysexError("The machine is unavailable, restoring state, or already receiving a file. Try again when it is ready.");
		return ticket;
	}

	void Editor::chooseSampleFiles()
	{
		if(m_model != md::MachineModel::Machinedrum)
			return;
		if(m_sysexChooserOpen)
		{
			showSampleError("Finish the open file dialog first.");
			return;
		}
		if(isUserSysexTransferActive())
		{
			showSampleError("A transfer is already running. Wait for it to finish or cancel it.");
			return;
		}
		const auto ticket = beginUserSysexTicket();
		if(!ticket)
			return;

		auto& config = getProcessor().getConfig();
		juce::File initial(config.getValue("mdSampleLastDirectory"));
		if(!initial.isDirectory())
			initial = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
		m_sysexChooserOpen = true;
		m_sampleFileChooser = std::make_unique<juce::FileChooser>(
			"Load samples into the Machinedrum", initial,
			"*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF;*.mp3;*.m4a;*.caf", true);
		const std::weak_ptr<void> lifetime = m_lifetimeToken;
		const auto t = *ticket;
		m_sampleFileChooser->launchAsync(
			juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
				| juce::FileBrowserComponent::canSelectMultipleItems,
			[lifetime, this, t](const juce::FileChooser& _chooser)
			{
				if(lifetime.expired())
					return;
				m_sysexChooserOpen = false;
				std::vector<juce::File> files;
				for(const auto& file : _chooser.getResults())
					if(file.existsAsFile())
						files.push_back(file);
				if(!files.empty())
					importSampleFiles(files, t);
			});
	}

	void Editor::importDroppedFiles(const std::vector<std::string>& _files)
	{
		if(_files.empty())
			return;
		if(m_sysexChooserOpen)
		{
			showSampleError("Finish the open file dialog first.");
			return;
		}
		if(isUserSysexTransferActive())
		{
			showSampleError("A transfer is already running. Wait for it to finish or cancel it.");
			return;
		}
		std::vector<juce::File> files;
		for(const auto& path : _files)
			files.emplace_back(juce::String(path));

		if(files.size() == 1 && sampleImport::isSysexFile(files.front()))
		{
			const auto ticket = beginUserSysexTicket();
			if(ticket)
				sendUserSysexFile(files.front(), *ticket);
			return;
		}
		if(m_model != md::MachineModel::Machinedrum)
		{
			showSampleError("Audio samples can only be loaded into the Machinedrum UW. The Monomachine takes DigiPRO waveforms as SysEx files.");
			return;
		}
		const auto ticket = beginUserSysexTicket();
		if(ticket)
			importSampleFiles(files, *ticket);
	}

	void Editor::importSampleFiles(const std::vector<juce::File>& _files, const md::SysexImportTicket& _ticket)
	{
		auto samples = std::make_shared<std::vector<sampleImport::DecodedSample>>();
		juce::String problems;
		for(const auto& file : _files)
		{
			std::string error;
			auto decoded = sampleImport::decode(file, error);
			if(!decoded)
			{
				problems += file.getFileName() + ": " + juce::String(error) + "\n";
				continue;
			}
			samples->push_back(std::move(*decoded));
		}
		if(samples->empty())
		{
			showSampleError("No sample could be loaded.\n\n" + problems);
			return;
		}
		if(samples->size() > sampleImport::g_slotCount)
		{
			showSampleError("At most 48 samples can be loaded at once.");
			return;
		}

		auto& config = getProcessor().getConfig();
		config.setValue("mdSampleLastDirectory", _files.front().getParentDirectory().getFullPathName());
		config.saveIfNeeded();

		if(problems.isNotEmpty())
		{
			const std::weak_ptr<void> lifetime = m_lifetimeToken;
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Some files were skipped",
				problems.toStdString(), getRmlComponent(), [lifetime, this, samples, _ticket]
				{
					if(!lifetime.expired())
						openSampleSlotMenu(samples, _ticket);
				});
			return;
		}
		openSampleSlotMenu(samples, _ticket);
	}

	namespace
	{
		juce::String rateLabel(const uint32_t _rate)
		{
			static constexpr uint32_t standard[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
			auto rate = _rate;
			for(const auto r : standard)
				if(std::abs(static_cast<int>(_rate) - static_cast<int>(r)) <= static_cast<int>(r / 500))
					rate = r;
			return rate % 1000 == 0 ? juce::String(rate / 1000) + "k" : juce::String(rate / 1000.0, 1) + "k";
		}

		juce::String secondsLabel(const double _seconds)
		{
			return _seconds < 10.0 ? juce::String(_seconds, 2) + " s" : juce::String(_seconds, 1) + " s";
		}
	}

	void Editor::openSampleSlotMenu(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, const md::SysexImportTicket& _ticket)
	{
		auto* const component = getRmlComponent();
		auto* const document = component ? component->getDocument() : nullptr;
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		if(!document || !processor)
			return;

		const auto directory = processor->readSampleDirectory();
		const auto ledger = sampleImport::loadLedger(getProcessor().getConfig());
		const auto count = static_cast<uint32_t>(_samples->size());
		const auto isUsed = [&](const uint32_t _slot)
		{
			return directory ? directory->slots[_slot].used : ledger.find(_slot) != ledger.end();
		};

		// suggest the first run of empty slots that fits all samples, else the first empty slot
		uint32_t suggested = sampleImport::g_slotCount;
		for(uint32_t first = 0; first + count <= sampleImport::g_slotCount && suggested == sampleImport::g_slotCount; ++first)
		{
			bool free = true;
			for(uint32_t i = 0; i < count && free; ++i)
				free = !isUsed(first + i);
			if(free)
				suggested = first;
		}
		for(uint32_t slot = 0; slot < sampleImport::g_slotCount && suggested == sampleImport::g_slotCount; ++slot)
			if(!isUsed(slot) && slot + count <= sampleImport::g_slotCount)
				suggested = slot;
		if(suggested == sampleImport::g_slotCount)
			suggested = 0;

		juce::String title = count == 1
			? juce::String("Load \"") + juce::String((*_samples)[0].name).trimEnd() + "\" into:"
			: "Load " + juce::String(count) + " samples starting at:";
		if(directory)
		{
			const auto freeSeconds = static_cast<double>(directory->freeSectors) * md::sampleDirectory::g_continuationSectorFrames / 44100.0;
			title += "   (" + juce::String(freeSeconds, 1) + " s free at 44.1k)";
		}

		juceRmlUi::Menu menu;
		menu.addEntry(title.toStdString(), false, false, {});
		const std::weak_ptr<void> lifetime = m_lifetimeToken;
		for(uint32_t slot = 0; slot < sampleImport::g_slotCount; ++slot)
		{
			juce::String label = sampleImport::slotLabel(slot);
			if(directory)
			{
				const auto& s = directory->slots[slot];
				if(s.used)
					label += "  " + juce::String(s.name).trimEnd().paddedRight(' ', 4) + "  " + secondsLabel(s.seconds()) + "  " + rateLabel(s.sampleRate);
				else
					label += "  - empty -";
			}
			else if(const auto it = ledger.find(slot); it != ledger.end())
				label += "  " + juce::String(it->second.name);
			const bool fits = slot + count <= sampleImport::g_slotCount;
			menu.addEntry(label.toStdString(), fits, slot == suggested, [lifetime, this, _samples, slot, _ticket]
			{
				// run after the menu has closed
				juce::MessageManager::callAsync([lifetime, this, _samples, slot, _ticket]
				{
					if(!lifetime.expired())
						confirmSampleSlots(_samples, slot, _ticket);
				});
			});
		}
		const auto size = component->getDocumentSize();
		menu.runModal(document, Rml::Vector2f(static_cast<float>(size.x) * 0.08f, static_cast<float>(size.y) * 0.05f), 13);
	}

	void Editor::confirmSampleSlots(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, const uint32_t _firstSlot, const md::SysexImportTicket& _ticket)
	{
		auto* const processor = dynamic_cast<AudioPluginAudioProcessor*>(&getProcessor());
		const auto directory = processor ? processor->readSampleDirectory() : std::nullopt;

		if(directory)
		{
			uint32_t needed = 0;
			for(const auto& sample : *_samples)
				needed += md::sampleDirectory::sectorsForFrames(static_cast<uint32_t>(sample.samples.size()));
			uint32_t available = directory->freeSectors;
			juce::String replaced;
			for(size_t i = 0; i < _samples->size(); ++i)
			{
				const auto& s = directory->slots[_firstSlot + i];
				if(!s.used)
					continue;
				available += s.sectors;
				replaced += juce::String(sampleImport::slotLabel(_firstSlot + static_cast<uint32_t>(i))) + "  " + juce::String(s.name).trimEnd() + "\n";
			}
			if(needed > available)
			{
				const auto toSeconds = [](const uint32_t _sectors) { return juce::String(_sectors * static_cast<double>(md::sampleDirectory::g_continuationSectorFrames) / 44100.0, 1); };
				showSampleError("Not enough sample memory. The selection needs about " + toSeconds(needed)
					+ " s at 44.1 kHz, the machine has " + toSeconds(available) + " s available for these slots.");
				return;
			}
			if(replaced.isNotEmpty())
			{
				const std::weak_ptr<void> lifetime = m_lifetimeToken;
				genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, "Replace samples?",
					("These slots already hold samples that will be replaced:\n\n" + replaced + "\nContinue?").toStdString(),
					[lifetime, this, _samples, _firstSlot, _ticket](const genericUI::MessageBox::Result _result)
					{
						if(!lifetime.expired() && _result == genericUI::MessageBox::Result::Yes)
							sendSamples(_samples, _firstSlot, _ticket);
					});
				return;
			}
		}
		sendSamples(_samples, _firstSlot, _ticket);
	}

	void Editor::sendSamples(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, const uint32_t _firstSlot, const md::SysexImportTicket& _ticket)
	{
		auto stream = sampleImport::encode(*_samples, _firstSlot);
		if(stream.empty())
		{
			showSampleError("The samples could not be encoded.");
			return;
		}

		PendingSampleSlots pending;
		pending.ticket = _ticket;
		for(size_t i = 0; i < _samples->size(); ++i)
			pending.slots[_firstSlot + static_cast<uint32_t>(i)] = sampleImport::SlotInfo{(*_samples)[i].name, (*_samples)[i].samples.size()};
		m_pendingSampleSlots = std::move(pending);

		sendUserSysexBytes(std::move(stream), juce::File((*_samples)[0].sourcePath), _ticket);
	}

	void Editor::showSampleError(const juce::String& _message)
	{
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"Samples not loaded", _message.toStdString(), getRmlComponent());
	}

	void Editor::launchUserSysexFileChooser(const md::SysexImportTicket& ticket)
	{

		auto& config = getProcessor().getConfig();
		juce::File initial(config.getValue("mdMmSysexLastDirectory"));
		if(!initial.isDirectory())
			initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

		m_sysexFileChooser = std::make_unique<juce::FileChooser>(
			"Send SysEx file to the emulated machine", initial,
			"*.syx;*.SYX", true);
		const std::weak_ptr<void> lifetime = m_lifetimeToken;
		m_sysexFileChooser->launchAsync(
			juce::FileBrowserComponent::openMode
				| juce::FileBrowserComponent::canSelectFiles,
			[lifetime, this, ticket](const juce::FileChooser& _chooser)
			{
				if(lifetime.expired())
					return;
				m_sysexChooserOpen = false;
				const auto file = _chooser.getResult();
				if(file.existsAsFile())
					sendUserSysexFile(file, ticket);
			});
	}

	void Editor::sendUserSysexFile(const juce::File& _file, const md::SysexImportTicket& ticket)
	{
		const auto fileSize = _file.getSize();
		if(fileSize <= 0)
		{
			showUserSysexError("The selected file is empty.");
			return;
		}
		if(fileSize > static_cast<juce::int64>(md::g_midiSysexTransferMaxBytes))
		{
			showUserSysexError("The selected file is larger than the 8 MiB safety limit.");
			return;
		}

		juce::MemoryBlock fileData;
		if(!_file.loadFileAsData(fileData)
			|| fileData.getSize() != static_cast<size_t>(fileSize))
		{
			showUserSysexError("The selected file could not be read completely.");
			return;
		}

		const auto* const begin = static_cast<const uint8_t*>(fileData.getData());
		std::vector<uint8_t> bytes(begin, begin + fileData.getSize());
		sendUserSysexBytes(std::move(bytes), _file, ticket);
	}

	void Editor::sendUserSysexBytes(std::vector<uint8_t>&& bytes, const juce::File& _file, const md::SysexImportTicket& ticket)
	{
		md::MidiSysexStreamValidation validation{};
		auto prepared = md::prepareMidiSysexTransfer(std::move(bytes), m_model, &validation);
		if(!prepared)
		{
			showUserSysexError(md::midiSysexValidationMessage(validation));
			return;
		}
		auto transfer = std::make_shared<md::PreparedMidiSysexTransfer>(std::move(*prepared));
		if(m_model == md::MachineModel::Monomachine
			|| transfer->contains(md::MidiSysexMessageKind::SdsHeader))
		{
			const bool digiPro = transfer->firstKind() == md::MidiSysexMessageKind::DigiPro;
			const bool samples = m_model == md::MachineModel::Machinedrum;
			const std::weak_ptr<void> lifetime = m_lifetimeToken;
			m_sysexChooserOpen = true;
			genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Info,
				samples ? "Is Machinedrum ready to receive samples?" : "Is Monomachine ready to receive?",
				samples
					? "Sample transfers can overwrite existing ROM sample slots and stop the sequencer. Wait until booting and any CLEANING/LOADING display have finished.\n\nIs the machine ready now?"
					: digiPro
					? "This file contains DigiPRO waveforms. Open GLOBAL > FILE > DIGIPRO MGR > RECEIVE.\n\nDoes the display say WAITING?"
					: "This file contains kits, patterns, songs, or globals. Open GLOBAL > FILE > SYSEX RECV.\n\nDoes the display say WAITING?",
				[lifetime, this, transfer, ticket, file = _file](const genericUI::MessageBox::Result answer)
				{
					if(lifetime.expired()) return;
					m_sysexChooserOpen = false;
					if(answer == genericUI::MessageBox::Result::Yes)
						startUserSysexTransfer(transfer, file, ticket, true);
					else
					{
						std::vector<uint8_t> retired;
						getProcessor().getPlugin().withDeviceLocked([&](synthLib::Device* base)
						{
							if(auto* device = dynamic_cast<md::Device*>(base))
								device->cancelUserSysexImport(ticket, retired);
						});
					}
				});
			return;
		}
		startUserSysexTransfer(transfer, _file, ticket, false);
	}

	void Editor::startUserSysexTransfer(const std::shared_ptr<md::PreparedMidiSysexTransfer>& prepared,
		const juce::File& _file, const md::SysexImportTicket& ticket, bool receiveModeConfirmed)
	{

		using StartResult = md::SysexImportStartResult;
		const auto result = getProcessor().getPlugin().withDeviceLocked(
			[&](synthLib::Device* const _device) -> std::optional<StartResult>
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				if(!device)
					return std::nullopt;
				return device->startUserSysexImport(ticket, *prepared, receiveModeConfirmed);
			});

		if(result != StartResult::Started)
		{
			if(result == StartResult::StaleRequest)
				showUserSysexError("The machine, project state, or import request changed while the dialog was open. Choose the file again.");
			else if(result == StartResult::Restoring)
				showUserSysexError("Wait for project-state restoration to finish, then try again.");
			else if(result == StartResult::NotReady)
				showUserSysexError("Wait for the emulated machine to finish booting, then try again.");
			else if(result == StartResult::Initializing)
				showUserSysexError("Wait for first-run storage initialization and the automatic reboot to finish, then try again.");
			else if(result == StartResult::ConfirmationRequired)
				showUserSysexError("Confirm that the machine has finished booting and is in the required receive mode before sending this file.");
			else if(result == StartResult::WrongModel)
				showUserSysexError("This file was prepared for a different machine model.");
			else if(result == StartResult::Busy)
				showUserSysexError("A SysEx file is already being sent.");
			else
				showUserSysexError("The local emulated machine is not available.");
			return;
		}

		m_sysexTransferWasActive = true;
		m_sysexMonitoredTicket = ticket;
		m_sysexLastState = md::MidiSysexTransferState::Queued;
		m_sysexLastSent = 0;
		m_sysexLastServiceSerial = 0;
		m_sysexLastAdvanceMilliseconds = juce::Time::getMillisecondCounterHiRes();
		m_sysexStallWarningShown = false;
		auto& config = getProcessor().getConfig();
		config.setValue("mdMmSysexLastDirectory",
			_file.getParentDirectory().getFullPathName());
		config.saveIfNeeded();
	}

	void Editor::showUserSysexError(const juce::String& _message)
	{
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"SysEx file not sent", _message.toStdString(), getRmlComponent());
	}

	void Editor::serviceUserSysexProgress()
	{
		if(!m_sysexTransferWasActive)
			return;
		const auto progress = getUserSysexProgress();
		if(!progress || progress->ticket != m_sysexMonitoredTicket
			|| progress->stage == md::SysexImportStage::Invalidated)
		{
			m_sysexTransferWasActive = false;
			showUserSysexError("The machine, project state, or import request changed before this transfer completed. Previously imported data is not rolled back. Choose the file again if needed.");
			return;
		}
		if(progress && progress->state == md::MidiSysexTransferState::WaitingForReceiveMode
			&& (m_sysexReceivePromptId != progress->transferId || m_sysexReceivePromptStep != progress->receiveStep))
		{
			m_sysexReceivePromptId = progress->transferId;
			m_sysexReceivePromptStep = progress->receiveStep;
			const juce::String screen = progress->receiveKind == md::MidiSysexMessageKind::DigiPro
				? "GLOBAL > FILE > DIGIPRO MGR > RECEIVE" : "GLOBAL > FILE > SYSEX RECV";
			const juce::String message = m_model == md::MachineModel::Machinedrum
				? "The samples have been acknowledged, but Machinedrum may still be CLEANING/LOADING. Close this message and wait for that display to finish. Then right-click and choose Resume SysEx Transfer to send the rest of this file, or cancel the remaining transfer."
				: "The next part of this file needs " + screen
					+ ". Close this message, leave the previous receive screen, and open that screen. "
					"When the display says WAITING, right-click and choose Resume SysEx Transfer. "
					"You can also cancel the remaining transfer.";
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info,
				"SysEx transfer paused", message.toStdString(), getRmlComponent());
		}
		if(progress && (progress->state == md::MidiSysexTransferState::Queued
			|| progress->state == md::MidiSysexTransferState::NegotiatingTurbo
			|| progress->state == md::MidiSysexTransferState::WaitingForDevice
			|| progress->state == md::MidiSysexTransferState::Retrying
			|| progress->state == md::MidiSysexTransferState::WaitingForReceiveMode
			|| progress->state == md::MidiSysexTransferState::Sending
			|| progress->state == md::MidiSysexTransferState::Cancelling))
		{
			const auto now = juce::Time::getMillisecondCounterHiRes();
			if(progress->serviceSerial != m_sysexLastServiceSerial
				|| progress->state != m_sysexLastState || progress->sent != m_sysexLastSent)
			{
				m_sysexLastServiceSerial = progress->serviceSerial;
				m_sysexLastState = progress->state;
				m_sysexLastSent = progress->sent;
				m_sysexLastAdvanceMilliseconds = now;
				m_sysexStallWarningShown = false;
			}
			else if(!m_sysexStallWarningShown
				&& now - m_sysexLastAdvanceMilliseconds >= 5000.0)
			{
				m_sysexStallWarningShown = true;
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
					"SysEx transfer paused",
					"The host has not advanced the emulated MIDI port for five seconds. "
					"Resume audio processing and disable plug-in bypass/suspension, or "
					"right-click the instrument to cancel the transfer.",
					getRmlComponent());
			}
			return;
		}

		m_sysexTransferWasActive = false;
		std::vector<uint8_t> retiredPayload;
		(void)getProcessor().getPlugin().withDeviceLocked(
			[&](synthLib::Device* const _device)
			{
				auto* const device = dynamic_cast<md::Device*>(_device);
				return device && device->retireUserSysexImport(progress->ticket, retiredPayload);
			});
		// Destruction remains outside the device lock and therefore outside any
		// interval in which it can block the real-time process callback.
		auto pendingSamples = std::move(m_pendingSampleSlots);
		m_pendingSampleSlots.reset();
		if(pendingSamples && progress && progress->ticket != pendingSamples->ticket)
			pendingSamples.reset();

		if(progress && progress->state == md::MidiSysexTransferState::Complete && pendingSamples)
		{
			auto& config = getProcessor().getConfig();
			auto ledger = sampleImport::loadLedger(config);
			juce::String loaded;
			for(const auto& [slot, info] : pendingSamples->slots)
			{
				ledger[slot] = info;
				loaded += juce::String(sampleImport::slotLabel(slot)) + "  " + juce::String(info.name).trimEnd() + "\n";
			}
			sampleImport::saveLedger(config, ledger);
			const auto firstSlot = pendingSamples->slots.begin()->first;
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Samples loaded",
				(loaded + "\nPlay a slot with the ROM machine of the same number, e.g. ROM-"
					+ juce::String(firstSlot + 1).paddedLeft('0', 2) + " for " + juce::String(sampleImport::slotLabel(firstSlot))
					+ ". Wait for any CLEANING/LOADING display to finish first.").toStdString(), getRmlComponent());
		}
		else if(progress && progress->state == md::MidiSysexTransferState::Complete)
		{
			juce::String message = "Every byte reached the emulated MIDI input. "
				"Check the machine display for the firmware's import result.";
			if(progress->acknowledgedSamples != 0)
				message += "\n\nThe device acknowledged " + juce::String(progress->acknowledgedSamples)
					+ " complete sample(s). Wait for any CLEANING/LOADING display to finish.";
			if(progress->fallbackCount != 0)
				message += "\n\nTurboMIDI was unavailable, so the transfer completed at standard MIDI speed.";
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info,
				"SysEx delivery complete", message.toStdString(), getRmlComponent());
		}
		else if(progress && progress->state == md::MidiSysexTransferState::Failed)
		{
			const auto error = progress->error;
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				"SysEx transfer stopped", error == md::MidiSysexTransferError::DeviceCancelled
				? "The machine cancelled the sample transfer. Check its display and available sample memory. Data already imported is not rolled back."
				: error == md::MidiSysexTransferError::ReplyTimedOut
					? "The machine did not finish waiting for the sample transfer. The transfer was stopped; previously imported data is not rolled back."
					: "The sample transfer could not obtain a reliable acknowledgement. It was stopped. Previously imported data is not rolled back.", getRmlComponent());
		}
		else if(progress && progress->state == md::MidiSysexTransferState::Cancelled)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info,
				"SysEx transfer cancelled",
				"The sender stopped the transfer and terminated any partial message. Previously imported data is not rolled back.",
				getRmlComponent());
		}
		else
		{
			showUserSysexError(
				"The emulated machine changed before the transfer completed. Please try again.");
		}
	}

	namespace
	{
		// The master knob spans the same range as the gain slider on the settings page, so the
		// extra headroom is reachable without opening a menu. Fully down is silence; unity sits
		// where the firmware's own output level lands, and the rest of the travel is boost.
		constexpr float g_masterVolumeMinDb = -40.0f;
		constexpr float g_masterVolumeMaxDb = 24.0f;
		float masterVolumePositionToGain(const float _position)
		{
			if(_position <= 0.0f)
				return 0.0f;
			const auto db = g_masterVolumeMinDb + _position * (g_masterVolumeMaxDb - g_masterVolumeMinDb);
			return std::pow(10.0f, db / 20.0f);
		}
		float masterVolumeGainToPosition(const float _gain)
		{
			if(_gain <= 0.0f)
				return 0.0f;
			const auto db = 20.0f * std::log10(_gain);
			return std::clamp((db - g_masterVolumeMinDb) / (g_masterVolumeMaxDb - g_masterVolumeMinDb), 0.0f, 1.0f);
		}
	}
	void Editor::createMasterVolume()
	{
		m_masterVolume = findChild<juceRmlUi::ElemKnob>("encMaster", false);
		if(!m_masterVolume)
			return;
		m_masterVolume->setMinValue(0.0f);
		m_masterVolume->setMaxValue(1.0f);
		m_masterVolume->setEndless(false);
		m_masterVolumeGain = getProcessor().getOutputGain();
		m_masterVolume->setValue(masterVolumeGainToPosition(m_masterVolumeGain), false);
		juceRmlUi::EventListener::Add(m_masterVolume, Rml::EventId::Change,
			[this](Rml::Event&)
			{
				m_masterVolumeGain = masterVolumePositionToGain(std::clamp(
					juceRmlUi::ElemValue::getValue(m_masterVolume), 0.0f, 1.0f));
				getProcessor().setOutputGain(m_masterVolumeGain);
			});
	}
	void Editor::syncMasterVolume()
	{
		// The settings page writes the same gain; follow it so both controls agree.
		if(!m_masterVolume)
			return;
		const auto gain = getProcessor().getOutputGain();
		if(gain == m_masterVolumeGain)
			return;
		m_masterVolumeGain = gain;
		m_masterVolume->setValue(masterVolumeGainToPosition(gain), false);
	}
	void Editor::configureEncoder(juceRmlUi::ElemKnob* const _knob,
		const md::PanelEncoder _encoder, float& _last, float& _accum)
	{
		if(!_knob)
			return;

		// Shift belongs to the MD/MM panel-hold gesture. Keep normal drag speed
		// while it is down; Command/Ctrl remains the fine-adjustment modifier.
		_knob->SetAttribute("speedScaleShift", 1.0f);
		if(const auto packet = md::panelEncoderPressPacket(getModel(), _encoder))
		{
			_knob->SetAttribute("speedScaleAlt", 1.0f);
			_knob->SetAttribute("title", "Drag to turn; Alt/Option-click to press; Alt/Option-drag to press and turn");
			juceRmlUi::EventListener::Add(_knob, Rml::EventId::Mousedown,
				[this, _knob, packet](Rml::Event& _event)
				{
					releaseEncoderPress();
					if(m_encoderPress.begin(packet,
						juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Left
							&& !juceRmlUi::helper::isContextMenu(_event),
						juceRmlUi::helper::getKeyModAlt(_event)))
					{
						// Suppress LCD hit targets immediately, before firmware has time
						// to draw the held-value overlay on the next presentation tick.
						m_lcdInteractionInputChanged = true;
						updateLcdInteractionState();
						m_pressedEncoder = _knob;
						_knob->SetClass("encoderPressed", true);
						const auto combined = m_panelRows.press(*packet);
						(void)sendPanelEvent(combined.row, combined.mask);
					}
				});
		}
		_knob->setMinValue(0.0f);
		_knob->setMaxValue(g_encoderRange);
		_knob->setEndless(true);
		_knob->setValue(g_encoderRange * 0.5f, false);	// no spurious Change at init
		_last = g_encoderRange * 0.5f;
		_accum = 0.0f;

		juceRmlUi::EventListener::Add(_knob, Rml::EventId::Change,
			[this, _knob, _encoder, &_last, &_accum](Rml::Event&)
			{
				onEncoderChanged(_knob, _encoder, _last, _accum);
			});
	}

	void Editor::onEncoderChanged(juceRmlUi::ElemKnob* const _knob,
		const md::PanelEncoder _encoder, float& _last, float& _accum)
	{
		if(!_knob)
			return;

		const float v = juceRmlUi::ElemValue::getValue(_knob);

		float delta = v - _last;

		// unwrap across the endless range boundary
		if(delta > g_encoderRange * 0.5f)
			delta -= g_encoderRange;
		else if(delta < -g_encoderRange * 0.5f)
			delta += g_encoderRange;

		_last = v;

		// accumulate fractional movement into whole detents
		_accum += delta;
		const int steps = static_cast<int>(_accum);

		if(steps == 0)
			return;

		_accum -= static_cast<float>(steps);

		emitEncoderSteps(_encoder, steps);
	}

	void Editor::emitEncoderSteps(const md::PanelEncoder _encoder, const int _steps) const
	{
		const auto command = md::panelEncoderCommand(getModel(), _encoder);
		if(!command || _steps == 0)
			return;
		const auto argument = static_cast<uint8_t>(_steps > 0 ? 0x01 : 0xff);
		const auto count = std::min(std::abs(_steps), g_encoderBurstCap);
		// The whole burst goes in under one device lock. Taking the lock waits for the audio thread to finish its
		// block, so a lock per step made a burst wait for several blocks when the machine runs close to realtime.
		auto& plugin = getProcessor().getPlugin();
		auto& diagnostics = plugin.getRealtimeInstrumentation();
		const auto model = static_cast<uint32_t>(getModel());
		plugin.withDeviceLocked([&](synthLib::Device* const _device)
		{
			auto* const device = dynamic_cast<md::Device*>(_device);
			for(int step = 0; step < count; ++step)
			{
				const auto token = diagnostics.beginPanelInput(model, *command, argument);
				const bool accepted = device && device->sendPanelEvent(*command, argument);
				diagnostics.endPanelInput(token, model, *command, argument, accepted);
			}
		});
	}

	void Editor::createLeds()
	{
		for(uint32_t i=0; i<16; ++i)
		{
			m_stepLeds[i] = findChild("stepLed" + std::to_string(i), false);
			m_drumLeds[i] = findChild("drumLed" + std::to_string(i), false);
		}

		if(getModel() == md::MachineModel::Monomachine)
		{
			// Monomachine uses a separate active-low LED-bank layout.
			const struct { const char* id; uint8_t bank; uint8_t bit; } leds[] =
			{
				{ "mmPageLed0", 0x25, 4 }, { "mmPageLed1", 0x25, 5 },
				{ "mmPageLed2", 0x25, 6 }, { "mmPageLed3", 0x25, 7 },
				{ "mmPageLed4", 0x26, 0 }, { "mmPageLed5", 0x26, 1 },
				{ "mmPageLed6", 0x26, 2 },
				{ "mmBankGroupAD", 0x26, 3 }, { "mmBankGroupEH", 0x26, 4 },
				{ "stPattern", 0x26, 5 }, { "stSong", 0x26, 6 },
				{ "mmTempoLed", 0x26, 7 },
				{ "mmRecordLed", 0x27, 0 },
				{ "mmTrigAmp", 0x27, 1 }, { "mmTrigFilter", 0x27, 2 },
				{ "mmTrigLfo", 0x27, 3 },
				{ "mmTrackPage0", 0x27, 4 }, { "mmTrackPage1", 0x27, 5 },
				{ "mmTrackPage2", 0x27, 6 }, { "mmTrackPage3", 0x27, 7 },
			};
			static_assert(std::size(leds) == 20);
			for(size_t i = 0; i < std::size(leds); ++i)
				m_mmPanelLeds[i] = { findChild(leds[i].id, false), leds[i].bank, leds[i].bit };
			return;
		}

		const std::pair<const char*, md::FrontPanel::StatusLed> status[] =
		{
			{ "stPattern", md::FrontPanel::StatusLed::Pattern   },
			{ "stSong",    md::FrontPanel::StatusLed::Song      },
			{ "stSynth",   md::FrontPanel::StatusLed::Synthesis },
			{ "stFx",      md::FrontPanel::StatusLed::Effects   },
			{ "stRoute",   md::FrontPanel::StatusLed::Routing   },
		};
		static_assert(std::size(status) == std::tuple_size_v<decltype(m_statusLeds)>);

		for(size_t i=0; i<m_statusLeds.size(); ++i)
			m_statusLeds[i] = { findChild(status[i].first, false), static_cast<uint8_t>(status[i].second) };

		const std::pair<const char*, md::FrontPanel::ModeLed> mode[] =
		{
			{ "ledClassic",  md::FrontPanel::ModeLed::Classic     },
			{ "ledExtended", md::FrontPanel::ModeLed::Extended    },
			{ "ledBankAD",   md::FrontPanel::ModeLed::BankGroupAD },
			{ "ledBankEH",   md::FrontPanel::ModeLed::BankGroupEH },
			{ "ledRecord",   md::FrontPanel::ModeLed::Record      },
			{ "ledTempo",    md::FrontPanel::ModeLed::Tempo       },
		};
		static_assert(std::size(mode) == std::tuple_size_v<decltype(m_mdModeLeds)>);

		for(size_t i=0; i<m_mdModeLeds.size(); ++i)
			m_mdModeLeds[i] = { findChild(mode[i].first, false), static_cast<uint8_t>(mode[i].second) };

		const RawLedElem pages[] =
		{
			{ findChild("mdPatternPage0", false), 0x22, 0 },
			{ findChild("mdPatternPage1", false), 0x22, 1 },
			{ findChild("mdPatternPage2", false), 0x22, 2 },
			{ findChild("mdPatternPage3", false), 0x23, 6 },
		};
		static_assert(std::size(pages) == std::tuple_size_v<decltype(m_mdPageLeds)>);
		std::copy(std::begin(pages), std::end(pages), m_mdPageLeds.begin());
	}

	bool Editor::updateLeds()
	{
		if(!m_frontPanelSnapshotValid || !m_ledPresentation.valid()
			|| !m_ledsChanged)
			return false;

		const auto isMonomachine = getModel() == md::MachineModel::Monomachine;
		const auto lit = [this](const uint8_t _bank, const uint8_t _bit)
		{
			return m_ledPresentation.isLit(_bank, _bit);
		};

		for(uint32_t i=0; i<16; ++i)
		{
			if(!m_stepLeds[i])
				continue;
			if(isMonomachine)
			{
				const auto bank = static_cast<uint8_t>(
					md::FrontPanel::g_firstLedBank + (i >> 2));
				const auto color = md::FrontPanel::decodeMonomachineStepLedColor(
					m_ledPresentation.getLedBankRaw(bank), i & 3);
				m_stepLeds[i]->SetClass("green", color == md::FrontPanel::LedColor::Green);
				m_stepLeds[i]->SetClass("red", color == md::FrontPanel::LedColor::Red);
				m_stepLeds[i]->SetClass("yellow", color == md::FrontPanel::LedColor::Yellow);
			}
			else
				m_stepLeds[i]->SetClass("lit", lit(
					static_cast<uint8_t>(0x20 + (i >> 3)),
					static_cast<uint8_t>(i & 7)));
		}

		if(isMonomachine)
		{
			const struct { uint8_t greenBank, greenBit, redBank, redBit; } tracks[] =
			{
				{ 0x25, 0, 0x25, 1 }, { 0x25, 2, 0x25, 3 },
				{ 0x24, 0, 0x24, 1 }, { 0x24, 2, 0x24, 3 },
				{ 0x24, 4, 0x24, 5 }, { 0x24, 6, 0x24, 7 },
			};
			for(size_t i = 0; i < std::size(tracks); ++i)
			{
				if(!m_drumLeds[i])
					continue;
				const bool green = lit(tracks[i].greenBank, tracks[i].greenBit);
				const bool red = lit(tracks[i].redBank, tracks[i].redBit);
				m_drumLeds[i]->SetClass("green", green && !red);
				m_drumLeds[i]->SetClass("red", red && !green);
				m_drumLeds[i]->SetClass("yellow", green && red);
			}
			for(const auto& led : m_mmPanelLeds)
			{
				if(led.elem)
					led.elem->SetClass("lit", lit(led.bank, led.bit));
			}
			m_ledsChanged = false;
			return true;
		}

		for(uint32_t i=0; i<16; ++i)
		{
			if(m_drumLeds[i])
				m_drumLeds[i]->SetClass("lit", lit(
					static_cast<uint8_t>(0x24 + (i >> 3)),
					static_cast<uint8_t>(i & 7)));
		}

		for(const auto& s : m_statusLeds)
		{
			if(s.elem)
				s.elem->SetClass("lit", lit(0x22, s.bit));
		}

		for(const auto& m : m_mdModeLeds)
		{
			if(m.elem)
				m.elem->SetClass("lit", lit(0x23, m.bit));
		}

		for(const auto& page : m_mdPageLeds)
		{
			if(page.elem)
				page.elem->SetClass("lit", lit(page.bank, page.bit));
		}
		m_ledsChanged = false;
		return true;
	}

	void Editor::paintLcd(const juce::Image& _target, juce::Graphics& _g) const
	{
		const auto isMonomachine = getModel() == md::MachineModel::Monomachine;
		const auto lcdOff = isMonomachine ? g_mmLcdOff : g_mdLcdOff;
		const auto lcdOn = isMonomachine ? g_mmLcdOn : g_mdLcdOn;

		// The skin's display surround need not have the framebuffer's 2:1 aspect.
		// Keep spare space the LCD background colour instead of stretching pixels.
		_g.fillAll(juce::Colour(lcdOff));
		if(!m_frontPanelSnapshotValid)
			return;

		const auto& fp = m_frontPanelSnapshot;

		juce::Image lcd(juce::Image::ARGB, md::FrontPanel::g_lcdWidth, md::FrontPanel::g_lcdHeight, false);

		{
			const juce::Image::BitmapData bd(lcd, juce::Image::BitmapData::writeOnly);

			for(uint32_t y=0; y<md::FrontPanel::g_lcdHeight; ++y)
			{
				for(uint32_t x=0; x<md::FrontPanel::g_lcdWidth; ++x)
					bd.setPixelColour(x, y, juce::Colour(fp.getLcdPixel(x, y) ? lcdOn : lcdOff));
			}
		}

		_g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
		if(m_pixelPerfectPanel && m_pixelPerfectPanel->paintLcd(lcd, _g))
			return;
		auto paintSize = m_lcdCanvas ? m_lcdCanvas->getPaintSize()
			: Rml::Vector2i(_target.getWidth(), _target.getHeight());
		if(paintSize.x <= 0 || paintSize.y <= 0)
			paintSize = {_target.getWidth(), _target.getHeight()};
		const auto viewport = lcdInteraction::Viewport::create(
			paintSize.x, paintSize.y, paintSize.x, paintSize.y,
			false);
		const auto content = viewport.contentInPaintSpace();
		_g.drawImage(lcd, juce::Rectangle<float>(static_cast<float>(content.x),
			static_cast<float>(content.y), static_cast<float>(content.width),
			static_cast<float>(content.height)));
	}

	void Editor::timerCallback(const int _timerId)
	{
		if(_timerId == g_panelTimerId)
		{
			servicePanelQueue();
			return;
		}
		if(_timerId != g_presentationTimerId)
			return;

		const auto nowMilliseconds = juce::Time::getMillisecondCounterHiRes();
		serviceGamepad(nowMilliseconds);
		serviceKeyboardReleases(nowMilliseconds);
		const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
		if(m_encoderPress.active() && (!modifiers.isAltDown() || !modifiers.isLeftButtonDown()))
			releaseEncoderPress();
		// Some plugin hosts can lose the modifier key-up when focus changes. Poll
		// native state as a fail-safe so no panel row remains held indefinitely.
		// A held gamepad latch button stands in for Shift.
		if(!m_shiftPanelLatch.empty() && !m_gamepadLatchHeld
			&& !juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
			releasePanelButtonGestures();
		// Same fail-safe for keyboard FUNCTION: never leave it held if the Ctrl key-up was lost.
		if(m_keyboardFunctionHeld && !modifiers.isCtrlDown())
		{
			m_keyboardFunctionHeld = false;
			releaseHeldControl(md::PanelControl::Function);
		}

		const auto hadFrontPanelSnapshot = m_frontPanelSnapshotValid;
		m_frontPanelSnapshotValid = refreshFrontPanelState(nowMilliseconds);
		if(hadFrontPanelSnapshot && !m_frontPanelSnapshotValid)
			m_lcdInteractionInputChanged = true;
		serviceUserSysexProgress();
		if(m_lcdInteractionInputChanged)
			updateLcdInteractionState();
		// Follows page changes and keyboard/gamepad focus; cached, so unchanged content is cheap.
		updateParameterTooltip();
		syncMasterVolume();

		if(m_lcdCanvas && m_lcdChanged)
			m_lcdCanvas->repaint();

		// SetClass mutates the Rml DOM but does not wake its renderer. Without this,
		// LED state is correct in the DOM while the pixels on screen can remain stale
		// until an unrelated repaint (normally up to 500 ms later).
		if(updateLeds())
			if(auto* rml = getRmlComponent())
				// These class changes are resolved in the next RmlUi update. Avoid
				// asking the software fallback to rasterize three unchanged frames.
				rml->enqueueUpdateOnce();
	}

	namespace
	{
		constexpr double g_gamepadRepeatDelayMilliseconds = 350.0;
		constexpr double g_gamepadRepeatIntervalMilliseconds = 110.0;
		// A knob push shorter than this is stretched to it, so a quick tap still reaches the firmware as a click.
		constexpr double g_gamepadMinimumPushMilliseconds = 50.0;
		constexpr double g_gamepadHighlightTimeoutMilliseconds = 120000.0;	// hide focus after 2 minutes idle
		constexpr double g_keyReleaseGraceMilliseconds = 50.0;	// see Editor::onPanelKey
		constexpr float g_gamepadTriggerThreshold = 0.5f;
		constexpr float g_gamepadStickDeadzone = 0.2f;
		constexpr float g_gamepadCursorSpeed = 0.6f;	// panel widths per second, left stick fully pushed
		constexpr double g_gamepadCursorLingerMilliseconds = 1000.0;	// cursor stays this long after the stick is let go
		constexpr float g_gamepadStickDetentsPerSecond = 30.0f;
		constexpr float g_gamepadTouchDetentsPerWidth = 48.0f;	// knob steps for a finger across the whole touchpad
		constexpr float g_gamepadTouchpadAspect = 0.5f;	// height / width, so a step is the same distance both ways
		constexpr float g_gamepadGyroDetentsPerRadian = 40.0f;	// a 45 degree tilt is about 30 knob steps
		constexpr float g_gamepadGyroDeadzone = 0.03f;	// rad/s: slower rotation is sensor drift, not a tilt
		constexpr float g_gamepadTiltRangeRadians = 0.785398f;	// absolute tilt: +-45 degrees covers the knob at 100%
		constexpr int g_gamepadAxisStepsPerPoll = 8;	// absolute axes glide to their target at most this fast

		struct GamepadDirectButton
		{
			Gamepad::Button button;
			md::PanelControl control;
		};

		// Pad buttons bound to a fixed panel key, independent of the focus.
		constexpr GamepadDirectButton g_gamepadDirectButtons[] =
		{
			{ Gamepad::Button::RightShoulder, md::PanelControl::Function },
			{ Gamepad::Button::East, md::PanelControl::Exit },
			{ Gamepad::Button::North, md::PanelControl::Enter },
			{ Gamepad::Button::West, md::PanelControl::Record },
			{ Gamepad::Button::Start, md::PanelControl::Play },
			{ Gamepad::Button::Back, md::PanelControl::Stop },
		};

		md::PanelControl arrowControl(const int _direction)
		{
			static constexpr md::PanelControl g_arrows[] =
				{ md::PanelControl::Up, md::PanelControl::Down, md::PanelControl::Left, md::PanelControl::Right };
			return g_arrows[_direction];
		}

		Rml::Vector2f elementCentre(Rml::Element* const _element)
		{
			return _element->GetAbsoluteOffset(Rml::BoxArea::Border)
				+ _element->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
		}
	}

	void Editor::createGamepad()
	{
		if(getProcessor().wrapperType != juce::AudioProcessor::wrapperType_Standalone)
			return;

		m_gamepadTargets.clear();
		for(const auto& pb : g_panelButtons)
		{
			auto* const button = findChild<juceRmlUi::ElemButton>(pb.id, false);
			if(!button || !md::panelPacket(getModel(), pb.control))
				continue;
			GamepadTarget target;
			target.element = button;
			target.button = button;
			target.control = pb.control;
			m_gamepadTargets.push_back(target);
		}

		const auto addKnob = [this](juceRmlUi::ElemKnob* const _knob, const md::PanelEncoder _encoder)
		{
			if(!_knob || !md::panelEncoderCommand(getModel(), _encoder))
				return;
			GamepadTarget target;
			target.element = _knob;
			target.knob = _knob;
			target.encoder = _encoder;
			m_gamepadTargets.push_back(target);
		};
		for(size_t i = 0; i < m_encoders.size(); ++i)
			addKnob(m_encoders[i], static_cast<md::PanelEncoder>(i));
		addKnob(m_levelEncoder, md::PanelEncoder::Level);
		addKnob(m_soundEncoder, md::PanelEncoder::SoundSelection);

		auto* const document = getDocument();
		if(m_gamepadTargets.empty() || !document)
			return;

		// The ring is reparented into whichever control has focus, so it follows that
		// control through panel scaling without any coordinate conversion.
		auto ring = document->CreateElement("div");
		ring->SetAttribute("style",
			"position: absolute; left: -4dp; top: -4dp; right: -4dp; bottom: -4dp;"
			" border: 3dp #ffc83a; pointer-events: none; z-index: 1000; display: none;");
		m_gamepadFocusRing = ring.get();
		m_gamepadTargets.front().element->AppendChild(std::move(ring));

		// The left stick's free cursor. Whatever control it overlaps takes the focus.
		auto cursor = document->CreateElement("div");
		cursor->SetAttribute("style",
			"position: absolute; left: 0px; top: 0px; width: 26dp; height: 26dp; margin-left: -13dp; margin-top: -13dp;"
			" border: 2dp #ffc83a; border-radius: 13dp; background-color: #ffc83a40;"
			" pointer-events: none; z-index: 1001; display: none;");
		m_gamepadCursor = cursor.get();
		document->AppendChild(std::move(cursor));
		// The cursor moves once per drawn frame, by the time since the previous frame. Moving it from the
		// poll timer instead steps unevenly, because that timer and the frame timer drift against each other.
		if(auto* const rml = getRmlComponent())
		{
			m_gamepadCursorFrame.set(rml->evPreUpdate, [this](juceRmlUi::RmlComponent*) { stepGamepadCursor(); });
			m_gamepadCursorFrameDone.set(rml->evPostUpdate, [this](juceRmlUi::RmlComponent*)
			{
				if(m_gamepadCursorLastFrameMilliseconds > 0.0 && m_gamepadCursor)
					m_gamepadCursor->GetContext()->RequestNextUpdate(0.0f);
			});
		}

		applyGamepadSettings();
		m_gamepad = std::make_unique<Gamepad>();
		m_gamepadFocus = 0;
	}

	std::optional<size_t> Editor::findGamepadTarget(const md::PanelControl _control) const
	{
		for(size_t i = 0; i < m_gamepadTargets.size(); ++i)
			if(m_gamepadTargets[i].button && m_gamepadTargets[i].control == _control)
				return i;
		return {};
	}

	std::optional<size_t> Editor::findGamepadTarget(const juceRmlUi::ElemKnob* const _knob) const
	{
		for(size_t i = 0; i < m_gamepadTargets.size(); ++i)
			if(_knob && m_gamepadTargets[i].knob == _knob)
				return i;
		return {};
	}

	void Editor::setGamepadFocus(const size_t _index)
	{
		if(_index >= m_gamepadTargets.size())
			return;
		m_gamepadFocus = _index;

		auto* const element = m_gamepadTargets[_index].element;
		auto* const parent = m_gamepadFocusRing ? m_gamepadFocusRing->GetParentNode() : nullptr;
		if(parent && parent != element)
		{
			// Anchor the absolutely positioned ring to the control without moving it.
			if(element->GetComputedValues().position() == Rml::Style::Position::Static)
				element->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Relative);
			if(auto ring = parent->RemoveChild(m_gamepadFocusRing))
				element->AppendChild(std::move(ring));

			// Panel keys clip their children, which would hide a ring drawn outside the
			// key. Draw it just inside those, and outside controls that do not clip.
			const bool clips = element->GetComputedValues().overflow_x() != Rml::Style::Overflow::Visible;
			const Rml::Property offset(clips ? 0.0f : -4.0f, Rml::Unit::DP);
			for(const auto id : { Rml::PropertyId::Left, Rml::PropertyId::Top, Rml::PropertyId::Right, Rml::PropertyId::Bottom })
				m_gamepadFocusRing->SetProperty(id, offset);
		}

		if(auto* const rml = getRmlComponent())
			rml->enqueueUpdateOnce();
	}

	void Editor::moveGamepadFocus(const GamepadDirection _direction)
	{
		if(m_gamepadTargets.empty())
			return;

		// Nearest control in the pressed direction, preferring ones that line up.
		const auto from = elementCentre(m_gamepadTargets[m_gamepadFocus].element);
		std::optional<size_t> best;
		float bestScore = std::numeric_limits<float>::max();
		for(size_t i = 0; i < m_gamepadTargets.size(); ++i)
		{
			if(i == m_gamepadFocus)
				continue;
			const auto delta = elementCentre(m_gamepadTargets[i].element) - from;
			float along = 0.0f, across = 0.0f;
			switch(_direction)
			{
			case GamepadDirection::Up:    along = -delta.y; across = delta.x; break;
			case GamepadDirection::Down:  along =  delta.y; across = delta.x; break;
			case GamepadDirection::Left:  along = -delta.x; across = delta.y; break;
			case GamepadDirection::Right: along =  delta.x; across = delta.y; break;
			}
			if(along <= 1.0f)
				continue;
			const auto score = along + 2.0f * std::abs(across);
			if(score < bestScore)
			{
				bestScore = score;
				best = i;
			}
		}
		if(best)
		{
			setGamepadFocus(*best);
			centreGamepadCursor();
		}
	}

	void Editor::placeGamepadCursor(const bool _requestUpdate)
	{
		auto* const document = getDocument();
		if(!m_gamepadCursor || !document)
			return;
		const auto size = document->GetBox().GetSize(Rml::BoxArea::Padding);
		m_gamepadCursor->SetProperty(Rml::PropertyId::Left, Rml::Property(m_gamepadCursorPosition.x * size.x, Rml::Unit::PX));
		m_gamepadCursor->SetProperty(Rml::PropertyId::Top, Rml::Property(m_gamepadCursorPosition.y * size.y, Rml::Unit::PX));
		if(_requestUpdate)
			if(auto* const rml = getRmlComponent())
				rml->enqueueUpdateOnce();
	}

	void Editor::stepGamepadCursor()
	{
		if(m_gamepadCursorLastFrameMilliseconds <= 0.0)
			return;
		const auto now = juce::Time::getMillisecondCounterHiRes();
		const auto elapsed = std::min(100.0, now - m_gamepadCursorLastFrameMilliseconds);
		m_gamepadCursorLastFrameMilliseconds = now;
		moveGamepadCursor(m_gamepadCursorStick.x, m_gamepadCursorStick.y, elapsed);
	}

	void Editor::centreGamepadCursor()
	{
		auto* const document = getDocument();
		if(!document || m_gamepadFocus >= m_gamepadTargets.size())
			return;
		const auto size = document->GetBox().GetSize(Rml::BoxArea::Padding);
		if(size.x <= 0.0f || size.y <= 0.0f)
			return;
		const auto centre = elementCentre(m_gamepadTargets[m_gamepadFocus].element)
			- document->GetAbsoluteOffset(Rml::BoxArea::Padding);
		m_gamepadCursorPosition = { centre.x / size.x, centre.y / size.y };
		placeGamepadCursor();
	}

	void Editor::moveGamepadCursor(const float _x, const float _y, const double _elapsedMilliseconds)
	{
		auto* const document = getDocument();
		const auto magnitude = std::min(1.0f, std::hypot(_x, _y));
		if(!m_gamepadCursor || !document || magnitude <= g_gamepadStickDeadzone)
			return;
		const auto size = document->GetBox().GetSize(Rml::BoxArea::Padding);
		if(size.x <= 0.0f || size.y <= 0.0f)
			return;

		// Squared response: fine placement near the centre of the stick's travel, fast at the edge. The
		// speed is in panel widths on both axes so a diagonal moves diagonally.
		const auto push = (magnitude - g_gamepadStickDeadzone) / (1.0f - g_gamepadStickDeadzone);
		const auto pixels = push * push * g_gamepadCursorSpeed * size.x * static_cast<float>(_elapsedMilliseconds / 1000.0);
		m_gamepadCursorPosition.x = std::clamp(m_gamepadCursorPosition.x + _x / magnitude * pixels / size.x, 0.0f, 1.0f);
		m_gamepadCursorPosition.y = std::clamp(m_gamepadCursorPosition.y + _y / magnitude * pixels / size.y, 0.0f, 1.0f);
		placeGamepadCursor(false);

		// Focus the control the circle overlaps, the one nearest its centre if it overlaps several. Over
		// empty panel the focus stays where it was.
		const auto point = document->GetAbsoluteOffset(Rml::BoxArea::Padding)
			+ Rml::Vector2f(m_gamepadCursorPosition.x * size.x, m_gamepadCursorPosition.y * size.y);
		const auto radius = m_gamepadCursor->GetBox().GetSize(Rml::BoxArea::Border).x * 0.5f;
		std::optional<size_t> best;
		float bestDistance = std::numeric_limits<float>::max();
		for(size_t i = 0; i < m_gamepadTargets.size(); ++i)
		{
			auto* const element = m_gamepadTargets[i].element;
			const auto origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
			const auto extent = element->GetBox().GetSize(Rml::BoxArea::Border);
			const auto dx = std::max({ origin.x - point.x, 0.0f, point.x - (origin.x + extent.x) });
			const auto dy = std::max({ origin.y - point.y, 0.0f, point.y - (origin.y + extent.y) });
			if(std::hypot(dx, dy) > radius)
				continue;
			const auto toCentre = elementCentre(element) - point;
			const auto distance = std::hypot(toCentre.x, toCentre.y);
			if(distance < bestDistance)
			{
				bestDistance = distance;
				best = i;
			}
		}
		if(best && *best != m_gamepadFocus)
			setGamepadFocus(*best);
	}

	void Editor::pressHeldControl(const md::PanelControl _control, const bool _latch, const bool _momentaryBanks)
	{
		const auto index = findGamepadTarget(_control);
		const auto packet = md::panelPacket(getModel(), _control);
		if(!index || !packet)
			return;
		auto* const button = m_gamepadTargets[*index].button;
		if(button->isChecked())
			return;

		// Mirror the mouse handler: an MM pattern bank normally toggles its own latch.
		if(!_momentaryBanks && getModel() == md::MachineModel::Monomachine && panelAffordances::isPatternBank(_control))
		{
			if(!_latch && !m_shiftPanelLatch.empty())
				releasePanelButtonGestures();
			if(panelAffordances::usesPersistentPatternBankLatch(getModel(), _control, !m_shiftPanelLatch.empty()))
			{
				togglePatternBankLatch(button, *packet);
				return;
			}
		}

		pressPanelButton(button, _control, *packet, _latch);
		m_gamepadHeldControls.push_back(_control);

		if(_control >= md::PanelControl::Track1 && _control <= md::PanelControl::Track6)
			m_gamepadTrack = static_cast<int>(_control) - static_cast<int>(md::PanelControl::Track1);
	}

	void Editor::releaseHeldControl(const md::PanelControl _control)
	{
		const auto it = std::find(m_gamepadHeldControls.begin(), m_gamepadHeldControls.end(), _control);
		if(it == m_gamepadHeldControls.end())
			return;
		m_gamepadHeldControls.erase(it);

		const auto index = findGamepadTarget(_control);
		const auto packet = md::panelPacket(getModel(), _control);
		if(index && packet)
			releasePanelButton(m_gamepadTargets[*index].button, _control, *packet);
	}

	void Editor::turnGamepadKnob(juceRmlUi::ElemKnob* const _knob, const float _detents)
	{
		if(!_knob || _detents == 0.0f)
			return;
		// Moving the on-screen knob reuses onEncoderChanged: the same detent
		// accumulator as a mouse drag, and the knob visibly turns.
		auto value = std::fmod(juceRmlUi::ElemValue::getValue(_knob) + _detents, g_encoderRange);
		if(value < 0.0f)
			value += g_encoderRange;
		_knob->setValue(value, true);
	}

	void Editor::pushGamepadKnob(const GamepadTarget& _target, const double _nowMilliseconds)
	{
		releaseGamepadKnob();
		const auto packet = md::panelEncoderPressPacket(getModel(), _target.encoder);
		if(!packet)
			return;
		const auto combined = m_panelRows.press(*packet);
		(void)sendPanelEvent(combined.row, combined.mask);
		m_gamepadPushedKnobPacket = packet;
		m_gamepadPushedKnob = _target.knob;
		m_gamepadKnobPushMilliseconds = _nowMilliseconds;
		m_gamepadKnobReleaseMilliseconds = std::numeric_limits<double>::max();
		_target.knob->SetClass("encoderPressed", true);
	}

	void Editor::releaseGamepadKnob()
	{
		if(!m_gamepadPushedKnobPacket)
			return;
		const auto combined = m_panelRows.release(*m_gamepadPushedKnobPacket);
		(void)sendPanelEvent(combined.row, combined.mask);
		if(m_gamepadPushedKnob)
			m_gamepadPushedKnob->SetClass("encoderPressed", false);
		m_gamepadPushedKnobPacket.reset();
		m_gamepadPushedKnob = nullptr;
	}

	void Editor::applyGamepadSettings()
	{
		auto& config = getProcessor().getConfig();
		for(const auto& info : gamepadAxes::g_axes)
			m_gamepadAxes[static_cast<size_t>(info.axis)] = gamepadAxes::read(config, info, getModel());
	}

	juceRmlUi::ElemKnob* Editor::gamepadAxisKnob(const gamepadAxes::Axis _axis) const
	{
		const auto& settings = m_gamepadAxes[static_cast<size_t>(_axis)];
		const auto target = settings.target;
		if(target >= gamepadAxes::g_targetKnobA && target < gamepadAxes::g_targetKnobA + static_cast<int>(m_encoders.size()))
			return m_encoders[static_cast<size_t>(target - gamepadAxes::g_targetKnobA)];
		if(target == gamepadAxes::g_targetLevel)
			return m_levelEncoder;
		if(target != gamepadAxes::g_targetFocusedTop && target != gamepadAxes::g_targetFocusedBottom)
			return nullptr;

		// The focused column: only while focus is on one of the eight data entry knobs.
		if(m_gamepadFocus >= m_gamepadTargets.size())
			return nullptr;
		const auto& focused = m_gamepadTargets[m_gamepadFocus];
		const auto encoder = static_cast<size_t>(focused.encoder);
		if(!focused.knob || encoder >= m_encoders.size() || focused.knob != m_encoders[encoder])
			return nullptr;
		return m_encoders[encoder % 4 + (target == gamepadAxes::g_targetFocusedBottom ? 4 : 0)];
	}

	void Editor::driveGamepadAxis(const gamepadAxes::Axis _axis, const bool _engaged, const float _position,
		const float _detents)
	{
		auto& run = m_gamepadAxisRuns[static_cast<size_t>(_axis)];
		const auto& settings = m_gamepadAxes[static_cast<size_t>(_axis)];
		auto* const knob = _engaged ? gamepadAxisKnob(_axis) : nullptr;
		if(!knob)
		{
			run = {};
			return;
		}

		// Engaging, or the axis now points at another knob: start from that knob's current value, if known.
		if(knob != run.knob)
		{
			run.knob = knob;
			run.value = settings.absolute && _position >= 0.0f ? currentKnobValue(knob) : -1;
		}

		if(run.value < 0)
		{
			// Relative, or the value is unknown (LEVEL, an LFO window): movement nudges the knob.
			const auto scale = static_cast<float>(settings.speedPercent) / 100.0f * (settings.invert ? -1.0f : 1.0f);
			turnGamepadKnob(knob, _detents * scale);
			return;
		}

		// Absolute: step towards the value for this position, a few steps per poll so a jump glides.
		const auto position = settings.invert ? 1.0f - _position : _position;
		const auto target = static_cast<int>(std::lround(std::clamp(position, 0.0f, 1.0f) * 127.0f));
		const auto steps = std::clamp(target - run.value, -g_gamepadAxisStepsPerPoll, g_gamepadAxisStepsPerPoll);
		if(steps == 0)
			return;
		emitEncoderSteps(knobEncoder(knob), steps);
		run.value = std::clamp(run.value + steps, 0, 127);
	}

	md::PanelEncoder Editor::knobEncoder(const juceRmlUi::ElemKnob* const _knob) const
	{
		for(size_t i = 0; i < m_encoders.size(); ++i)
			if(m_encoders[i] == _knob)
				return static_cast<md::PanelEncoder>(i);
		return md::PanelEncoder::Level;
	}

	int Editor::currentKnobValue(const juceRmlUi::ElemKnob* const _knob) const
	{
		// Known for the eight data entry knobs on a data page, from the controller's copy of the kit.
		const auto encoder = static_cast<size_t>(knobEncoder(_knob));
		if(encoder >= m_encoders.size() || m_encoders[encoder] != _knob)
			return -1;
		std::optional<int> page;
		if(getModel() == md::MachineModel::Monomachine)
			page = currentMonomachineDataPage();
		else if(!m_lcdInteractionState || m_lcdInteractionState->layout == lcdInteraction::LayoutKind::Standard)
			page = currentMachinedrumDataPage();
		const auto track = m_controller.getCurrentTrack();
		if(!page || track < 0)
			return -1;
		return m_controller.getTrackParameterValue(static_cast<uint8_t>(track), static_cast<uint8_t>(*page),
			static_cast<uint8_t>(encoder));
	}

	void Editor::setGamepadAxisFunction(const bool _held)
	{
		if(_held == m_gamepadAxisFunctionHeld)
			return;
		if(!_held)
		{
			releaseHeldControl(md::PanelControl::Function);
			m_gamepadAxisFunctionHeld = false;
			return;
		}
		// If R1 already holds FUNCTION, leave it to R1; this is tried again every poll.
		const auto index = findGamepadTarget(md::PanelControl::Function);
		if(!index || m_gamepadTargets[*index].button->isChecked())
			return;
		pressHeldControl(md::PanelControl::Function, false);
		m_gamepadAxisFunctionHeld = true;
	}

	void Editor::releaseGamepadInputs()
	{
		const auto held = m_gamepadHeldControls;
		for(auto it = held.rbegin(); it != held.rend(); ++it)
			releaseHeldControl(*it);
		m_gamepadHeldControls.clear();
		releaseGamepadKnob();
		m_gamepadActHeld = false;
		m_gamepadRepeatDirection.reset();
		m_gamepadAxisFunctionHeld = false;
		m_gamepadLatchHeld = false;
	}

	void Editor::serviceGamepad(const double _nowMilliseconds)
	{
		if(!m_gamepad)
			return;

		using Button = Gamepad::Button;
		const auto state = m_gamepad->poll();
		const auto previous = m_gamepadPrevious;
		m_gamepadPrevious = state;
		const auto elapsedMilliseconds = m_gamepadLastPollMilliseconds > 0.0
			? std::min(100.0, _nowMilliseconds - m_gamepadLastPollMilliseconds) : 0.0;
		m_gamepadLastPollMilliseconds = _nowMilliseconds;

		if(!state.connected)
		{
			if(previous.connected)
			{
				// An unplug mid-hold must never leave keys down in the emulated machine.
				releaseGamepadInputs();
				cancelPanelInputGestures();
				if(m_gamepadFocusRing)
					m_gamepadFocusRing->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
				if(m_gamepadCursor)
					m_gamepadCursor->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
				m_gamepadCursorVisible = false;
				m_gamepadCursorLastMoveMilliseconds = 0.0;
				m_gamepadCursorLastFrameMilliseconds = 0.0;
				m_gamepadCursorStick = {};
				m_gamepadHighlightVisible = false;
				m_gamepadLastActivityMilliseconds = 0.0;
				if(auto* const rml = getRmlComponent())
					rml->enqueueUpdateOnce();
			}
			return;
		}

		// Show the focus highlight only while the controller is in use.
		const auto active = state.buttons != previous.buttons || state.touching
			|| std::abs(state.leftX) > g_gamepadStickDeadzone || std::abs(state.leftY) > g_gamepadStickDeadzone
			|| std::abs(state.rightX) > g_gamepadStickDeadzone || std::abs(state.rightY) > g_gamepadStickDeadzone
			|| state.leftTrigger > g_gamepadTriggerThreshold || state.rightTrigger > g_gamepadTriggerThreshold;
		if(active)
			m_gamepadLastActivityMilliseconds = _nowMilliseconds;
		const auto highlight = m_gamepadLastActivityMilliseconds > 0.0
			&& _nowMilliseconds - m_gamepadLastActivityMilliseconds < g_gamepadHighlightTimeoutMilliseconds;
		if(highlight != m_gamepadHighlightVisible && m_gamepadFocusRing)
		{
			m_gamepadHighlightVisible = highlight;
			m_gamepadFocusRing->SetProperty(Rml::PropertyId::Display,
				highlight ? Rml::Style::Display::Block : Rml::Style::Display::None);
			if(highlight)
			{
				setGamepadFocus(m_gamepadFocus);
				centreGamepadCursor();
			}
			else if(auto* const rml = getRmlComponent())
				rml->enqueueUpdateOnce();
		}

		const auto pressedNow = [&](const Button _b) { return state.pressed(_b) && !previous.pressed(_b); };
		const auto releasedNow = [&](const Button _b) { return !state.pressed(_b) && previous.pressed(_b); };
		const auto actKnob = [&]() -> juceRmlUi::ElemKnob*
		{
			return m_gamepadActHeld && m_gamepadActTarget < m_gamepadTargets.size()
				? m_gamepadTargets[m_gamepadActTarget].knob : nullptr;
		};
		const auto focusKnob = [&]() -> juceRmlUi::ElemKnob*
		{
			return m_gamepadFocus < m_gamepadTargets.size() ? m_gamepadTargets[m_gamepadFocus].knob : nullptr;
		};
		const auto startRepeat = [&](const GamepadDirection _direction)
		{
			m_gamepadRepeatDirection = _direction;
			m_gamepadRepeatNextMilliseconds = _nowMilliseconds + g_gamepadRepeatDelayMilliseconds;
		};
		// With Cross holding a knob its switch is pushed, so these are press-turns: the firmware picks the step size.
		const auto turnHeldKnob = [&](const GamepadDirection _direction)
		{
			const bool up = _direction == GamepadDirection::Right || _direction == GamepadDirection::Up;
			turnGamepadKnob(actKnob(), up ? 1.0f : -1.0f);
		};

		// L1 holds panel keys like Shift-click; letting go releases everything it held.
		m_gamepadLatchHeld = state.pressed(Button::LeftShoulder);
		if(releasedNow(Button::LeftShoulder) && !m_shiftPanelLatch.empty())
			releasePanelButtonGestures();

		for(const auto& direct : g_gamepadDirectButtons)
		{
			if(pressedNow(direct.button))
				pressHeldControl(direct.control, m_gamepadLatchHeld);
			else if(releasedNow(direct.button))
				releaseHeldControl(direct.control);
		}

		const bool arrowLayer = state.rightTrigger > g_gamepadTriggerThreshold;	// R2: machine arrow keys
		const bool pageLayer = state.leftTrigger > g_gamepadTriggerThreshold;	// L2: data pages / tracks

		static constexpr std::pair<Button, GamepadDirection> g_dpad[] =
		{
			{ Button::DpadUp, GamepadDirection::Up }, { Button::DpadDown, GamepadDirection::Down },
			{ Button::DpadLeft, GamepadDirection::Left }, { Button::DpadRight, GamepadDirection::Right },
		};
		for(const auto& [button, direction] : g_dpad)
		{
			const auto arrow = arrowControl(static_cast<int>(direction));
			if(pressedNow(button))
			{
				if(arrowLayer)
					pressHeldControl(arrow, m_gamepadLatchHeld);
				else if(pageLayer && getModel() == md::MachineModel::Machinedrum)
				{
					// The Machinedrum has no page or track keys; use its direct selection helpers.
					if(direction == GamepadDirection::Left || direction == GamepadDirection::Right)
					{
						const auto pages = static_cast<int>(panelAffordances::g_machinedrumDataPages.size());
						m_gamepadPage = (m_gamepadPage + (direction == GamepadDirection::Right ? 1 : pages - 1)) % pages;
						selectMachinedrumDataPage(m_gamepadPage);
					}
					else
					{
						m_gamepadTrack = std::clamp(m_gamepadTrack + (direction == GamepadDirection::Down ? 1 : -1), 0, 15);
						selectMachinedrumTrack(m_gamepadTrack);
					}
				}
				else if(pageLayer)
				{
					if(direction == GamepadDirection::Left)
						pressHeldControl(md::PanelControl::DataPageBackward, m_gamepadLatchHeld);
					else if(direction == GamepadDirection::Right)
						pressHeldControl(md::PanelControl::DataPageForward, m_gamepadLatchHeld);
					else
					{
						m_gamepadTrack = std::clamp(m_gamepadTrack + (direction == GamepadDirection::Down ? 1 : -1), 0, 5);
						queuePanelPulse(static_cast<md::PanelControl>(
							static_cast<int>(md::PanelControl::Track1) + m_gamepadTrack));
					}
				}
				else if(actKnob())
				{
					turnHeldKnob(direction);
					startRepeat(direction);
				}
				else if(!m_gamepadActHeld)
				{
					moveGamepadFocus(direction);
					startRepeat(direction);
				}
			}
			else if(releasedNow(button))
			{
				releaseHeldControl(arrow);
				if(direction == GamepadDirection::Left)
					releaseHeldControl(md::PanelControl::DataPageBackward);
				else if(direction == GamepadDirection::Right)
					releaseHeldControl(md::PanelControl::DataPageForward);
				if(m_gamepadRepeatDirection == direction)
					m_gamepadRepeatDirection.reset();
			}
		}

		if(m_gamepadRepeatDirection && _nowMilliseconds >= m_gamepadRepeatNextMilliseconds)
		{
			m_gamepadRepeatNextMilliseconds = _nowMilliseconds + g_gamepadRepeatIntervalMilliseconds;
			if(actKnob())
				turnHeldKnob(*m_gamepadRepeatDirection);
			else if(!m_gamepadActHeld && !arrowLayer && !pageLayer)
				moveGamepadFocus(*m_gamepadRepeatDirection);
		}

		// The left stick moves the cursor, which focuses what it passes over. It stays put while
		// Cross holds a control, so the held control keeps the focus.
		const bool cursorPushed = !m_gamepadActHeld && std::hypot(state.leftX, state.leftY) > g_gamepadStickDeadzone;
		m_gamepadCursorStick = cursorPushed ? Rml::Vector2f(state.leftX, state.leftY) : Rml::Vector2f(0.0f, 0.0f);
		if(!cursorPushed)
		{
			m_gamepadCursorLastFrameMilliseconds = 0.0;
		}
		else
		{
			m_gamepadCursorLastMoveMilliseconds = _nowMilliseconds;
			if(m_gamepadCursorLastFrameMilliseconds <= 0.0)
			{
				// Start the frame loop: the first frame measures from now, so the cursor doesn't jump.
				m_gamepadCursorLastFrameMilliseconds = _nowMilliseconds;
				if(m_gamepadCursor)
					m_gamepadCursor->GetContext()->RequestNextUpdate(0.0f);
			}
		}
		// The cursor shows only while the stick is in use, and lingers briefly to show where it stopped.
		const auto showCursor = m_gamepadCursorLastMoveMilliseconds > 0.0
			&& _nowMilliseconds - m_gamepadCursorLastMoveMilliseconds < g_gamepadCursorLingerMilliseconds;
		if(showCursor != m_gamepadCursorVisible && m_gamepadCursor)
		{
			m_gamepadCursorVisible = showCursor;
			m_gamepadCursor->SetProperty(Rml::PropertyId::Display,
				showCursor ? Rml::Style::Display::Block : Rml::Style::Display::None);
			if(auto* const rml = getRmlComponent())
				rml->enqueueUpdateOnce();
		}

		// Cross acts on the focused control. On a panel key it holds the key. On a knob it pushes the knob, like
		// pressing the real one: a tap is a click, and turning while Cross is held is a press-turn.
		if(pressedNow(Button::South) && m_gamepadFocus < m_gamepadTargets.size())
		{
			m_gamepadActHeld = true;
			m_gamepadActTarget = m_gamepadFocus;
			const auto& target = m_gamepadTargets[m_gamepadFocus];
			if(target.button)
				pressHeldControl(target.control, m_gamepadLatchHeld);
			else if(target.knob)
				pushGamepadKnob(target, _nowMilliseconds);
		}
		else if(releasedNow(Button::South) && m_gamepadActHeld)
		{
			m_gamepadActHeld = false;
			m_gamepadRepeatDirection.reset();
			const auto& target = m_gamepadTargets[m_gamepadActTarget];
			if(target.button)
				releaseHeldControl(target.control);
			else
				m_gamepadKnobReleaseMilliseconds = m_gamepadKnobPushMilliseconds + g_gamepadMinimumPushMilliseconds;
		}
		if(m_gamepadPushedKnobPacket && !m_gamepadActHeld && _nowMilliseconds >= m_gamepadKnobReleaseMilliseconds)
			releaseGamepadKnob();

		// Stick clicks jump focus to the trig row and to the data-entry knobs.
		if(!m_gamepadActHeld)
		{
			std::optional<size_t> jump;
			if(pressedNow(Button::LeftStick))
				jump = findGamepadTarget(md::PanelControl::Trigger1);
			if(pressedNow(Button::RightStick))
				jump = findGamepadTarget(m_encoders[0]);
			if(jump)
			{
				setGamepadFocus(*jump);
				centreGamepadCursor();
			}
		}

		// Touchpad and gyro: each axis turns the knob chosen for it in the settings. By default both act on the
		// focused data entry knob's column: sideways turns the top knob, up/down (forward/back) the bottom one.
		// The touchpad works while a finger slides; the gyro while the PS / Guide button is held, and only
		// rotation moves the knobs, so holding still changes nothing.
		const bool touchEngaged = state.touching && previous.touching;
		const bool tiltEngaged = state.pressed(Button::Guide);
		const auto wantsFunction = [&](const gamepadAxes::Axis _a, const gamepadAxes::Axis _b)
		{
			return m_gamepadAxes[static_cast<size_t>(_a)].function || m_gamepadAxes[static_cast<size_t>(_b)].function;
		};
		using gamepadAxes::Axis;
		setGamepadAxisFunction((touchEngaged && wantsFunction(Axis::TouchX, Axis::TouchY))
			|| (tiltEngaged && wantsFunction(Axis::TiltRoll, Axis::TiltPitch)));
		// Touchpad: bottom-left is 0 on both axes, top-right the maximum.
		driveGamepadAxis(Axis::TouchX, state.touching, state.touchX,
			touchEngaged ? (state.touchX - previous.touchX) * g_gamepadTouchDetentsPerWidth : 0.0f);
		driveGamepadAxis(Axis::TouchY, state.touching, 1.0f - state.touchY,
			touchEngaged ? (previous.touchY - state.touchY) * g_gamepadTouchDetentsPerWidth * g_gamepadTouchpadAspect : 0.0f);

		// Tilt: the angle comes from gravity (accelerometer); relative mode uses the rotation rate (gyro). Held
		// flat is the middle; rolled left or tipped away from you is 0.
		const auto seconds = static_cast<float>(elapsedMilliseconds / 1000.0);
		const auto rate = [](const float _radiansPerSecond)
		{
			return std::abs(_radiansPerSecond) > g_gamepadGyroDeadzone ? _radiansPerSecond : 0.0f;
		};
		const auto tiltPosition = [&](const float _angle, const gamepadAxes::Axis _axis)
		{
			const auto range = g_gamepadTiltRangeRadians * 100.0f
				/ static_cast<float>(std::max(1, m_gamepadAxes[static_cast<size_t>(_axis)].speedPercent));
			return std::clamp(0.5f + _angle / (2.0f * range), 0.0f, 1.0f);
		};
		const bool tiltKnown = tiltEngaged && state.hasAccel;
		const auto roll = std::atan2(-state.accelX, state.accelY);	// right side down = positive
		const auto pitch = std::atan2(-state.accelZ, state.accelY);	// far edge up = positive
		driveGamepadAxis(Axis::TiltRoll, tiltEngaged, tiltKnown ? tiltPosition(roll, Axis::TiltRoll) : -1.0f,
			-rate(state.gyroZ) * g_gamepadGyroDetentsPerRadian * seconds);
		driveGamepadAxis(Axis::TiltPitch, tiltEngaged, tiltKnown ? tiltPosition(pitch, Axis::TiltPitch) : -1.0f,
			rate(state.gyroX) * g_gamepadGyroDetentsPerRadian * seconds);

		// Right stick turns the focused knob, faster the further it is pushed.
		if(auto* const knob = focusKnob(); knob && std::abs(state.rightX) > g_gamepadStickDeadzone)
		{
			const auto magnitude = (std::abs(state.rightX) - g_gamepadStickDeadzone) / (1.0f - g_gamepadStickDeadzone);
			const auto rate = std::copysign(magnitude * magnitude * g_gamepadStickDetentsPerSecond, state.rightX);
			turnGamepadKnob(knob, rate * static_cast<float>(elapsedMilliseconds / 1000.0));
		}
	}

	namespace
	{
		using Key = Rml::Input::KeyIdentifier;

		struct KeyboardButton
		{
			Key key;
			md::PanelControl control;
		};

		// Panel keys on the computer keyboard. Holding a key holds the panel key; Shift
		// latches it exactly like Shift-click.
		constexpr KeyboardButton g_keyboardButtons[] =
		{
			{ Key::KI_F1, md::PanelControl::Trigger1 }, { Key::KI_F2, md::PanelControl::Trigger2 },
			{ Key::KI_F3, md::PanelControl::Trigger3 }, { Key::KI_F4, md::PanelControl::Trigger4 },
			{ Key::KI_F5, md::PanelControl::Trigger5 }, { Key::KI_F6, md::PanelControl::Trigger6 },
			{ Key::KI_F7, md::PanelControl::Trigger7 }, { Key::KI_F8, md::PanelControl::Trigger8 },
			{ Key::KI_1, md::PanelControl::Trigger9 }, { Key::KI_2, md::PanelControl::Trigger10 },
			{ Key::KI_3, md::PanelControl::Trigger11 }, { Key::KI_4, md::PanelControl::Trigger12 },
			{ Key::KI_5, md::PanelControl::Trigger13 }, { Key::KI_6, md::PanelControl::Trigger14 },
			{ Key::KI_7, md::PanelControl::Trigger15 }, { Key::KI_8, md::PanelControl::Trigger16 },
			{ Key::KI_UP, md::PanelControl::Up }, { Key::KI_DOWN, md::PanelControl::Down },
			{ Key::KI_LEFT, md::PanelControl::Left }, { Key::KI_RIGHT, md::PanelControl::Right },
			{ Key::KI_RETURN, md::PanelControl::Enter }, { Key::KI_BACK, md::PanelControl::Exit },
			{ Key::KI_Q, md::PanelControl::Track1 }, { Key::KI_W, md::PanelControl::Track2 },
			{ Key::KI_E, md::PanelControl::Track3 }, { Key::KI_R, md::PanelControl::Track4 },
			{ Key::KI_T, md::PanelControl::Track5 }, { Key::KI_Y, md::PanelControl::Track6 },
			{ Key::KI_SPACE, md::PanelControl::Play }, { Key::KI_END, md::PanelControl::Stop },
			{ Key::KI_HOME, md::PanelControl::Record },
			{ Key::KI_PRIOR, md::PanelControl::DataPageBackward }, { Key::KI_NEXT, md::PanelControl::DataPageForward },
			{ Key::KI_F9, md::PanelControl::BankA }, { Key::KI_F10, md::PanelControl::BankB },
			{ Key::KI_F11, md::PanelControl::BankC }, { Key::KI_F12, md::PanelControl::BankD },
			{ Key::KI_OEM_3, md::PanelControl::BankGroup },
			{ Key::KI_K, md::PanelControl::Kit }, { Key::KI_P, md::PanelControl::PatternSong },
			{ Key::KI_U, md::PanelControl::Tempo }, { Key::KI_I, md::PanelControl::Scale },
			{ Key::KI_O, md::PanelControl::SynthesisEffectsRouting },
			{ Key::KI_B, md::PanelControl::TrigSelect }, { Key::KI_N, md::PanelControl::ClassicExtended },
			{ Key::KI_M, md::PanelControl::SongEnable },
		};

		// Hold one of these, then turn with - / = (one step) or [ / ] (eight steps). Tapping
		// the key without turning clicks the encoder. A-D / E-H follow the panel's 2x4 grid.
		constexpr Key g_keyboardEncoders[] =
		{
			Key::KI_A, Key::KI_S, Key::KI_D, Key::KI_F,
			Key::KI_Z, Key::KI_X, Key::KI_C, Key::KI_V,
			Key::KI_G,	// LEVEL
			Key::KI_H,	// Machinedrum SOUND wheel
		};
	}

	void Editor::createKeyboardControl()
	{
		auto* const document = getDocument();
		if(getProcessor().wrapperType != juce::AudioProcessor::wrapperType_Standalone || !document)
			return;

		m_keyboardControl = true;
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown,
			[this](Rml::Event& _event) { onPanelKey(_event, true); });
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup,
			[this](Rml::Event& _event) { onPanelKey(_event, false); });
	}

	void Editor::releaseKeyboardEncoderPress()
	{
		if(!m_keyboardEncoderPressPacket)
			return;
		const auto combined = m_panelRows.release(*m_keyboardEncoderPressPacket);
		(void)sendPanelEvent(combined.row, combined.mask);
		if(m_keyboardPressedKnob)
			m_keyboardPressedKnob->SetClass("encoderPressed", false);
		m_keyboardEncoderPressPacket.reset();
		m_keyboardPressedKnob = nullptr;
	}

	void Editor::resetKeyboardControl()
	{
		releaseKeyboardEncoderPress();
		if(m_keyboardFunctionHeld)
			releaseHeldControl(md::PanelControl::Function);
		for(const auto& button : g_keyboardButtons)
			if(std::find(m_keyboardHeldKeys.begin(), m_keyboardHeldKeys.end(), button.key) != m_keyboardHeldKeys.end())
				releaseHeldControl(button.control);
		m_keyboardHeldKeys.clear();
		m_keyboardPendingReleases.clear();
		m_keyboardFunctionHeld = false;
		m_keyboardEncoder.reset();
		m_keyboardEncoderTurned = false;
	}

	void Editor::onPanelKey(Rml::Event& _event, const bool _down)
	{
		if(!m_keyboardControl)
			return;

		const auto key = static_cast<Key>(_event.GetParameter<int>("key_identifier", 0));
		const bool shift = _event.GetParameter<int>("shift_key", 0) != 0;
		const bool ctrl = _event.GetParameter<int>("ctrl_key", 0) != 0;

		// Ctrl is FUNCTION. Every key event carries the modifier state, including the
		// synthetic ones the Rml component sends for a modifier-only change.
		if(ctrl != m_keyboardFunctionHeld)
		{
			m_keyboardFunctionHeld = ctrl;
			if(ctrl)
				pressHeldControl(md::PanelControl::Function, shift);
			else
				releaseHeldControl(md::PanelControl::Function);
		}

		if(key == Key::KI_UNKNOWN)
			return;

		// X11 auto-repeat sends a release/press pair while a key is held. JUCE drops the pair only
		// when the press is already queued, which isn't guaranteed while the emulator keeps the
		// message thread busy. A leaked pair would tap a held trig (clearing it), so a release only
		// counts once no press of the same key follows within a short grace period.
		const auto pending = std::find_if(m_keyboardPendingReleases.begin(), m_keyboardPendingReleases.end(),
			[&](const auto& _p) { return _p.first == key; });
		if(!_down)
		{
			const bool pressTurn = key == Key::KI_OEM_4 || key == Key::KI_OEM_6;
			if(pressTurn || std::find(m_keyboardHeldKeys.begin(), m_keyboardHeldKeys.end(), key) != m_keyboardHeldKeys.end())
			{
				if(pending == m_keyboardPendingReleases.end())
					m_keyboardPendingReleases.emplace_back(key, juce::Time::getMillisecondCounterHiRes() + g_keyReleaseGraceMilliseconds);
				_event.StopPropagation();
				return;
			}
		}
		else if(pending != m_keyboardPendingReleases.end())
		{
			m_keyboardPendingReleases.erase(pending);	// the release was auto-repeat; the key never went up
		}
		if(handlePanelKey(key, _down, shift))
			_event.StopPropagation();
	}
	void Editor::serviceKeyboardReleases(const double _nowMilliseconds)
	{
		for(auto it = m_keyboardPendingReleases.begin(); it != m_keyboardPendingReleases.end();)
		{
			if(it->second > _nowMilliseconds)
			{
				++it;
				continue;
			}
			const auto key = it->first;
			it = m_keyboardPendingReleases.erase(it);
			handlePanelKey(key, false, false);
			it = m_keyboardPendingReleases.begin();	// handlePanelKey may reset the list
		}
	}
	bool Editor::handlePanelKey(const int _key, const bool _down, const bool _shift)
	{
		using Key = Rml::Input::KeyIdentifier;
		const auto key = static_cast<Key>(_key);
		const bool shift = _shift;

		// Delete lets go of every held or latched panel key. (Escape does this too, but only
		// when something is held; otherwise it opens the standalone's menu.)
		if(key == Key::KI_DELETE)
		{
			if(_down)
			{
				resetKeyboardControl();
				cancelPanelInputGestures();
				m_gamepadHeldControls.clear();
			}
			return true;
		}

		const auto encoderKnob = [this](const size_t _index) -> std::pair<juceRmlUi::ElemKnob*, md::PanelEncoder>
		{
			if(_index < m_encoders.size())
				return { m_encoders[_index], static_cast<md::PanelEncoder>(_index) };
			if(_index == m_encoders.size())
				return { m_levelEncoder, md::PanelEncoder::Level };
			return { m_soundEncoder, md::PanelEncoder::SoundSelection };
		};

		// Turning keys repeat on purpose: holding = keeps turning. [ and ] are press-turns: the
		// encoder switch is held while turning, so the firmware picks a coarse step suited to
		// the parameter, exactly as on the hardware.
		const bool pressTurn = key == Key::KI_OEM_4 || key == Key::KI_OEM_6;
		const float turn = key == Key::KI_OEM_MINUS || key == Key::KI_OEM_4 ? -1.0f
			: key == Key::KI_OEM_PLUS || key == Key::KI_OEM_6 ? 1.0f : 0.0f;
		if(turn != 0.0f)
		{
			if(_down && m_keyboardEncoder)
			{
				const auto [knob, encoder] = encoderKnob(*m_keyboardEncoder);
				if(pressTurn && !m_keyboardEncoderPressPacket)
				{
					if(const auto packet = md::panelEncoderPressPacket(getModel(), encoder); packet && knob)
					{
						const auto combined = m_panelRows.press(*packet);
						(void)sendPanelEvent(combined.row, combined.mask);
						m_keyboardEncoderPressPacket = packet;
						m_keyboardPressedKnob = knob;
						knob->SetClass("encoderPressed", true);
					}
				}
				turnGamepadKnob(knob, turn);
				m_keyboardEncoderTurned = true;
			}
			else if(!_down && pressTurn)
			{
				releaseKeyboardEncoderPress();
			}
			return true;
		}

		const auto held = std::find(m_keyboardHeldKeys.begin(), m_keyboardHeldKeys.end(), key);
		if(_down)
		{
			if(held != m_keyboardHeldKeys.end())
			{
				return true;	// operating-system auto-repeat of a held key
			}
		}
		else
		{
			if(held == m_keyboardHeldKeys.end())
				return false;
			m_keyboardHeldKeys.erase(held);
		}

		for(size_t i = 0; i < std::size(g_keyboardEncoders); ++i)
		{
			if(g_keyboardEncoders[i] != key)
				continue;
			if(_down)
			{
				m_keyboardHeldKeys.push_back(key);
				m_keyboardEncoder = i;
				m_keyboardEncoderTurned = false;
			}
			else if(m_keyboardEncoder == i)
			{
				if(!m_keyboardEncoderTurned)
				{
					if(const auto packet = md::panelEncoderPressPacket(getModel(), encoderKnob(i).second))
					{
						m_panelSteps.push_back({ *packet, true });
						m_panelSteps.push_back({ *packet, false });
					}
				}
				releaseKeyboardEncoderPress();
				m_keyboardEncoder.reset();
			}
			return true;
		}

		// The Machinedrum has no page keys; step its data pages directly, as the gamepad does.
		if(getModel() == md::MachineModel::Machinedrum && (key == Key::KI_PRIOR || key == Key::KI_NEXT))
		{
			if(_down)
			{
				m_keyboardHeldKeys.push_back(key);
				const auto pages = static_cast<int>(panelAffordances::g_machinedrumDataPages.size());
				m_gamepadPage = (m_gamepadPage + (key == Key::KI_NEXT ? 1 : pages - 1)) % pages;
				selectMachinedrumDataPage(m_gamepadPage);
			}
			return true;
		}

		for(const auto& button : g_keyboardButtons)
		{
			if(button.key != key)
				continue;
			if(_down)
			{
				m_keyboardHeldKeys.push_back(key);
				// A keyboard can hold a bank key while pressing a step, so no bank latch is needed.
				pressHeldControl(button.control, shift, true);
			}
			else
			{
				releaseHeldControl(button.control);
			}
			return true;
		}
		return false;
	}

	std::optional<int> Editor::currentMonomachineDataPage() const
	{
		if(getModel() != md::MachineModel::Monomachine || !m_frontPanelSnapshotValid)
			return std::nullopt;
		// The DATA PAGE LEDs are active-low bits in panel LED banks 0x25 and 0x26.
		constexpr uint8_t banks[] = { 0x25, 0x25, 0x25, 0x25, 0x26, 0x26, 0x26 };
		constexpr uint8_t bits[] = { 4, 5, 6, 7, 0, 1, 2 };
		std::array<bool, panelAffordances::g_monomachineDataPages.size()> active{};
		for(size_t page = 0; page < active.size(); ++page)
		{
			const auto raw = m_frontPanelSnapshot.getLedBankRaw(banks[page]);
			active[page] = (raw & static_cast<uint8_t>(1u << bits[page])) == 0;
		}
		return panelAffordances::singleActiveIndex(active);
	}

	std::optional<int> Editor::currentMachinedrumDataPage() const
	{
		if(getModel() != md::MachineModel::Machinedrum || !m_frontPanelSnapshotValid)
			return std::nullopt;
		constexpr md::FrontPanel::StatusLed pages[] =
		{
			md::FrontPanel::StatusLed::Synthesis,
			md::FrontPanel::StatusLed::Effects,
			md::FrontPanel::StatusLed::Routing,
		};
		std::array<bool, panelAffordances::g_machinedrumDataPages.size()> active{};
		for(size_t page = 0; page < active.size(); ++page)
			active[page] = m_frontPanelSnapshot.getStatusLed(pages[page]);
		return panelAffordances::singleActiveIndex(active);
	}

	void Editor::createParameterTooltip()
	{
		auto* const document = getDocument();
		if(!document)
			return;

		m_lcdArea = findChild("lcdArea", false);

		// Tooltip wording can be changed without rebuilding: tooltips.txt in the data folder
		// overrides the built-in text, and tooltips-defaults.txt beside it lists every key.
		const std::filesystem::path dataFolder = std::filesystem::u8path(getProcessor().getDataFolder());
		HelpOverrides::writeDefaults(dataFolder / "tooltips-defaults.txt");
		m_help.setFile(dataFolder / "tooltips.txt");

		// One tooltip, reparented under whatever it describes so it follows panel scaling.
		auto tooltip = document->CreateElement("div");
		tooltip->SetAttribute("style",
			"position: absolute; top: 100%; left: 50%; width: 250dp; margin-left: -125dp; margin-top: 6dp;"
			" padding: 6dp 8dp; background-color: #1b221dee; border: 1dp #56635a; color: #e6ebe2;"
			" font-size: 11dp; line-height: 14dp; text-align: left; white-space: normal;"
			" pointer-events: none; z-index: 2000; display: none;");
		m_parameterTooltip = tooltip.get();
		if(m_encoders[0])
			m_encoders[0]->AppendChild(std::move(tooltip));
		else
			document->AppendChild(std::move(tooltip));

		for(unsigned i = 0; i < m_encoders.size(); ++i)
		{
			if(!m_encoders[i])
				continue;
			juceRmlUi::EventListener::Add(m_encoders[i], Rml::EventId::Mouseover, [this, i](Rml::Event&)
			{
				m_tooltipHoverKnob = i;
				updateParameterTooltip();
			});
			juceRmlUi::EventListener::Add(m_encoders[i], Rml::EventId::Mouseout, [this, i](Rml::Event&)
			{
				if(m_tooltipHoverKnob != i)
					return;
				m_tooltipHoverKnob.reset();
				updateParameterTooltip();
			});
		}
	}

	namespace
	{
		// ", knob A" .. ", knob H", the tail of a tooltip footer.
		std::string knobSuffix(const unsigned _encoder)
		{
			return std::string(", knob ") + static_cast<char>('A' + _encoder);
		}
	}

	Editor::TooltipText Editor::tooltipFor(const parameterHelp::Entry& _entry, std::string _footer) const
	{
		return { _entry.abbreviation, m_help(_entry.name), m_help(_entry.description), std::move(_footer) };
	}

	std::string Editor::nowText(const parameterHelp::ValueHash& _value) const
	{
		return std::string(" Now: ") + _value.text + ". " + m_help(_value.meaning);
	}

	std::optional<Editor::TooltipText> Editor::describeMachineName() const
	{
		if(getModel() == md::MachineModel::Machinedrum)
		{
			uint16_t key = 0;
			const auto* const machine = machinedrumHelp::machineForHash(
				lcdText::hash(m_frontPanelSnapshot, lcdText::g_mdMachineName), key);
			if(!machine)
				return std::nullopt;
			TooltipText text{ machine->lcdName, m_help(machine->name), m_help(machine->description), "Machine on the active track" };
			// Numbered machines (e.g. TRX-B2) show their two-digit number on the LCD.
			if(machine->number)
			{
				const auto number = machine->number + (key - machine->first);
				text.abbreviation += static_cast<char>('0' + number / 10);
				text.abbreviation += static_cast<char>('0' + number % 10);
			}
			return text;
		}

		const auto* const machine = monomachineHelp::machineForHash(lcdText::hash(m_frontPanelSnapshot, lcdText::g_machineName));
		if(!machine)
			return std::nullopt;
		return TooltipText{ machine->lcdName, m_help(machine->name), m_help(machine->description), "Machine on the active track" };
	}

	std::optional<Editor::TooltipText> Editor::describeMachinedrumEncoder(const unsigned _encoder) const
	{
		if(_encoder >= 8)
			return std::nullopt;
		const auto knob = knobSuffix(_encoder);

		// LFO and master FX windows are recognised by the fork's classifier.
		if(m_lcdInteractionState)
		{
			using lcdInteraction::SurfaceKind;
			const auto surface = m_lcdInteractionState->surface;
			if(m_lcdInteractionState->layout == lcdInteraction::LayoutKind::Lfo && surface == SurfaceKind::Lfo)
			{
				auto text = tooltipFor(machinedrumHelp::g_lfo[_encoder], "LFO window" + knob);
				// UPDTE and PARAM print their value; say what the LFO is currently doing and aimed at.
				if(_encoder == 4)
				{
					if(const auto* const value = machinedrumHelp::lfoUpdateForHash(
						lcdText::hash(m_frontPanelSnapshot, lcdText::mdLfoValue(4))))
						text.description += nowText(*value);
				}
				else if(_encoder == 1)
				{
					if(const auto* const label = machinedrumHelp::labelForHash(
						lcdText::inkHash(m_frontPanelSnapshot, lcdText::mdLfoValue(1))))
					{
						text.description += std::string(" Now: ") + label + ".";
						// Describe the target too if it is an EFFECTS or ROUTING parameter.
						const auto* entry = parameterHelp::entryForLabel(machinedrumHelp::g_effects, label);
						if(!entry)
							entry = parameterHelp::entryForLabel(machinedrumHelp::g_routing, label);
						if(entry)
							text.description += std::string(" ") + m_help(entry->name) + ": " + m_help(entry->description);
					}
				}
				return text;
			}
			if(m_lcdInteractionState->layout == lcdInteraction::LayoutKind::MasterFx)
			{
				const auto index = static_cast<size_t>(surface) - static_cast<size_t>(SurfaceKind::MasterFxEcho);
				if(index >= 4)
					return std::nullopt;
				const auto* const entry = machinedrumHelp::parameter(machinedrumHelp::g_masterFxFamilies[index],
					machinedrumHelp::g_masterFxLabels[index][_encoder]);
				if(!entry)
					return std::nullopt;
				return tooltipFor(*entry, std::string("Master FX, ") + machinedrumHelp::g_masterFxNames[index] + knob);
			}
		}

		// Data pages: read the field label and the machine name off the LCD.
		const auto page = currentMachinedrumDataPage();
		if(!page)
			return std::nullopt;
		uint16_t key = 0;
		const auto* const machine = machinedrumHelp::machineForHash(
			lcdText::hash(m_frontPanelSnapshot, lcdText::g_mdMachineName), key);
		bool allTracks = false;
		const auto* const entry = machinedrumHelp::entry(*page, machine, machinedrumHelp::labelForHash(
			lcdText::inkHash(m_frontPanelSnapshot, lcdText::fieldLabel(_encoder))), allTracks);
		if(!entry)
			return std::nullopt;
		constexpr const char* pageNames[] = { "synthesis", "Effects page", "Routing page" };
		// SYNTHESIS entries only resolve with a known machine, so machine is set on page 0.
		const auto where = *page == 0 ? std::string(m_help(machine->name)) + " synthesis" : std::string(pageNames[*page]);
		auto text = tooltipFor(*entry, where + knob);
		if(allTracks)
		{
			text.name += ", all tracks";
			text.description += " CTR-AL applies it to all 16 tracks.";
		}
		return text;
	}

	std::optional<Editor::TooltipText> Editor::describeMonomachineEncoder(const unsigned _encoder) const
	{
		const auto page = currentMonomachineDataPage();
		if(!page)
			return std::nullopt;
		const auto knob = knobSuffix(_encoder);

		if(*page == 0)
		{
			// SYNTHESIS depends on the machine: read both the field label and the machine
			// name off the LCD. Anything unrecognised (a menu, an overlay) shows nothing.
			const auto* const label = monomachineHelp::labelForHash(
				lcdText::hash(m_frontPanelSnapshot, lcdText::fieldLabel(_encoder)));
			const auto* const machine = monomachineHelp::machineForHash(
				lcdText::hash(m_frontPanelSnapshot, lcdText::g_machineName));
			const auto* const entry = label && machine ? monomachineHelp::parameter(machine->id, label) : nullptr;
			if(!entry)
				return std::nullopt;
			auto text = tooltipFor(*entry, std::string(m_help(machine->name)) + " synthesis" + knob);
			// Settings print their value under the knob; say what it is set to.
			if(const auto* const value = monomachineHelp::machineValueForHash(machine->id, label,
				lcdText::hash(m_frontPanelSnapshot, lcdText::lfoValue(_encoder))))
				text.description += nowText(*value);
			return text;
		}

		const auto* const entry = parameterHelp::monomachineEntry(*page, _encoder);
		if(!entry)
			return std::nullopt;
		auto text = tooltipFor(*entry, std::string(parameterHelp::g_monomachinePageNames[*page]) + " page" + knob);

		// On an LFO page, say what the knob is currently set to, read off the LCD.
		const bool lfoPage = *page >= 4;
		if(lfoPage && _encoder <= 1)
		{
			text.description += lfoTargetDescription(_encoder);
		}
		else if(lfoPage)
		{
			if(const auto* const value = monomachineHelp::lfoValueForHash(_encoder,
				lcdText::hash(m_frontPanelSnapshot, lcdText::lfoValue(_encoder))))
				text.description += nowText(*value);
		}
		return text;
	}

	std::string Editor::lfoTargetDescription(const unsigned _encoder) const
	{
		const auto* const target = monomachineHelp::lfoPageForHash(lcdText::hash(m_frontPanelSnapshot, lcdText::lfoValue(0)));
		if(!target)
			return {};
		if(_encoder == 0)
			return std::string(" Now: ") + target->text + " (" + m_help(target->name) + ").";

		// DEST: describe the parameter the LFO is aimed at on the selected page.
		const auto destinationHash = lcdText::hash(m_frontPanelSnapshot, lcdText::lfoValue(1));
		const parameterHelp::Entry* destination = monomachineHelp::lfoSpecialDestinationForHash(destinationHash);
		if(!destination)
		{
			if(const auto* const label = monomachineHelp::labelForHash(destinationHash))
			{
				if(target->dataPage == 0)
				{
					if(const auto* const machine = monomachineHelp::machineForHash(
						lcdText::hash(m_frontPanelSnapshot, lcdText::g_machineName)))
						destination = monomachineHelp::parameter(machine->id, label);
				}
				else if(target->dataPage > 0)
				{
					destination = monomachineHelp::fixedPageEntry(target->dataPage, label);
				}
			}
		}
		if(!destination)
			return std::string(" Now on the ") + m_help(target->name) + " page.";
		return std::string(" Now: ") + destination->abbreviation + " (" + m_help(destination->name) + ") on the "
			+ m_help(target->name) + " page. " + m_help(destination->description);
	}

	Editor::TooltipTarget Editor::tooltipTarget() const
	{
		// In priority order: mouse over a knob, mouse over the LCD (the machine name or a recognised
		// field), a held keyboard knob key, then the gamepad's focused knob.
		if(m_tooltipHoverKnob && m_encoders[*m_tooltipHoverKnob])
			return { m_encoders[*m_tooltipHoverKnob], m_tooltipHoverKnob, false };
		if(m_lcdArea && m_tooltipLcdMachineName)
			return { m_lcdArea, std::nullopt, true };
		if(m_lcdArea && m_lcdHoverEncoder)
			return { m_lcdArea, m_lcdHoverEncoder, false };
		if(m_keyboardEncoder && *m_keyboardEncoder < m_encoders.size() && m_encoders[*m_keyboardEncoder])
		{
			const auto encoder = static_cast<unsigned>(*m_keyboardEncoder);
			return { m_encoders[encoder], encoder, false };
		}
		if(m_gamepadHighlightVisible && m_gamepadFocus < m_gamepadTargets.size() && m_gamepadTargets[m_gamepadFocus].knob)
		{
			const auto encoder = static_cast<unsigned>(m_gamepadTargets[m_gamepadFocus].encoder);
			if(encoder < m_encoders.size())
				return { m_gamepadTargets[m_gamepadFocus].knob, encoder, false };
		}
		return {};
	}

	std::optional<Editor::TooltipText> Editor::describe(const TooltipTarget& _target) const
	{
		if(!m_frontPanelSnapshotValid)
			return std::nullopt;
		if(_target.machineName)
			return describeMachineName();
		if(!_target.encoder)
			return std::nullopt;
		auto text = getModel() == md::MachineModel::Machinedrum
			? describeMachinedrumEncoder(*_target.encoder)
			: describeMonomachineEncoder(*_target.encoder);
		if(text)
			appendCurrentValue(*text, *_target.encoder);
		return text;
	}

	void Editor::appendCurrentValue(TooltipText& _text, const unsigned _encoder) const
	{
		// Only on data pages, from the controller's copy of the kit: the Machinedrum LFO and master
		// FX windows' knobs aren't track parameters.
		std::optional<int> dataPage;
		if(getModel() == md::MachineModel::Monomachine)
			dataPage = currentMonomachineDataPage();
		else if(!m_lcdInteractionState || m_lcdInteractionState->layout == lcdInteraction::LayoutKind::Standard)
			dataPage = currentMachinedrumDataPage();
		const auto track = m_controller.getCurrentTrack();
		if(!dataPage || track < 0)
			return;
		const auto value = m_controller.getTrackParameterValue(static_cast<uint8_t>(track),
			static_cast<uint8_t>(*dataPage), static_cast<uint8_t>(_encoder));
		if(value >= 0)
			_text.footer += ", value " + std::to_string(value);
	}

	void Editor::updateParameterTooltip()
	{
		if(!m_parameterTooltip)
			return;

		// Wait until the pointer has rested on the same thing for the configured delay. This runs on
		// every UI tick, so the tooltip appears once the delay has passed.
		const auto target = tooltipTarget();
		const auto now = std::chrono::steady_clock::now();
		if(target != m_tooltipRestTarget)
		{
			m_tooltipRestTarget = target;
			m_tooltipRestSince = now;
		}
		const bool rested = now - m_tooltipRestSince >= std::chrono::milliseconds(m_tooltipDelayMs);

		std::optional<TooltipText> text;
		if(target.anchor && m_tooltipsEnabled && rested)
		{
			m_help.refresh();
			text = describe(target);
		}
		if(!text)
		{
			hideParameterTooltip();
			return;
		}

		using Rml::StringUtilities::EncodeRml;
		const auto content = "<span style=\"color: #ffc83a;\">" + EncodeRml(text->abbreviation) + "</span>  "
			+ EncodeRml(text->name) + "<br/><span style=\"color: #b3bdb0;\">" + EncodeRml(text->description) + "</span><br/>"
			+ "<span style=\"color: #7f8a82; font-size: 9dp;\">" + EncodeRml(text->footer) + "</span>";
		if(content == m_parameterTooltipContent && m_parameterTooltip->GetParentNode() == target.anchor)
			return;
		m_parameterTooltipContent = content;

		// Move the tooltip under its anchor, which must be positioned for top: 100% to work.
		auto* const anchor = target.anchor;
		if(m_parameterTooltip->GetParentNode() != anchor)
		{
			if(anchor->GetComputedValues().position() == Rml::Style::Position::Static)
				anchor->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Relative);
			if(auto* const parent = m_parameterTooltip->GetParentNode())
				if(auto owned = parent->RemoveChild(m_parameterTooltip))
					anchor->AppendChild(std::move(owned));
		}
		m_parameterTooltip->SetInnerRML(content);
		m_parameterTooltip->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::Block);
		if(auto* const rml = getRmlComponent())
			rml->enqueueUpdateOnce();
	}

	void Editor::hideParameterTooltip()
	{
		if(m_parameterTooltipContent.empty())
			return;
		m_parameterTooltip->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		m_parameterTooltipContent.clear();
		if(auto* const rml = getRmlComponent())
			rml->enqueueUpdateOnce();
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}
}
