#include "mdEditor.h"

#include "mdController.h"
#include "mdPanelAffordances.h"
#include "mdPluginProcessor.h"
#include "mdSettingsAudioInput.h"
#include "mdSettingsPanelFeel.h"
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

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
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
		auto publisher = getFrontPanelPublisher();
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
		createButtons();
		createEncoders();
		createMasterVolume();
		applyPanelSpeeds();
		createLeds();
		createPanelAffordances();
		applyPixelPerfectPanel();
		createGamepad();

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
		return lcdInteraction::hitTest(*m_lcdInteractionState,
			static_cast<int>(std::floor(point->x)), static_cast<int>(std::floor(point->y)));
	}

	void Editor::updateLcdHover(const Rml::Event& _event)
	{
		const auto target = lcdTargetAt(_event);
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

			constexpr md::FrontPanel::StatusLed pages[] =
			{
				md::FrontPanel::StatusLed::Synthesis,
				md::FrontPanel::StatusLed::Effects,
				md::FrontPanel::StatusLed::Routing,
			};
			std::array<bool, panelAffordances::g_machinedrumDataPages.size()> active{};
			for(size_t page = 0; page < active.size(); ++page)
				active[page] = frontPanel.getStatusLed(pages[page]);

			const auto current = panelAffordances::singleActiveIndex(active);
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
			constexpr uint8_t banks[] = { 0x25, 0x25, 0x25, 0x25, 0x26, 0x26, 0x26 };
			constexpr uint8_t bits[] = { 4, 5, 6, 7, 0, 1, 2 };
			std::array<bool, panelAffordances::g_monomachineDataPages.size()> active{};
			for(size_t page = 0; page < active.size(); ++page)
			{
				const auto raw = frontPanel.getLedBankRaw(banks[page]);
				active[page] = (raw & static_cast<uint8_t>(1u << bits[page])) == 0;
			}

			const auto current = panelAffordances::singleActiveIndex(active);
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

		const auto ticket = getProcessor().getPlugin().withDeviceLocked(
			[](synthLib::Device* base) -> std::optional<md::SysexImportTicket>
			{
				auto* device = dynamic_cast<md::Device*>(base);
				return device ? device->beginUserSysexImport() : std::nullopt;
			});
		if(!ticket)
		{
			showUserSysexError("The machine is unavailable, restoring state, or already receiving a file. Try again when it is ready.");
			return;
		}
		m_sysexChooserOpen = true;
		launchUserSysexFileChooser(*ticket);
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
		if(progress && progress->state == md::MidiSysexTransferState::Complete)
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

	void Editor::createMasterVolume()
	{
		m_masterVolume = findChild<juceRmlUi::ElemKnob>("encMaster", false);
		if(!m_masterVolume)
			return;

		m_masterVolume->setMinValue(0.0f);
		m_masterVolume->setMaxValue(1.0f);
		m_masterVolume->setEndless(false);
		m_masterVolume->setValue(
			std::clamp(getProcessor().getOutputGain(), 0.0f, 1.0f), false);

		juceRmlUi::EventListener::Add(m_masterVolume, Rml::EventId::Change,
			[this](Rml::Event&)
			{
				getProcessor().setOutputGain(std::clamp(
					juceRmlUi::ElemValue::getValue(m_masterVolume), 0.0f, 1.0f));
			});
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
		for(int step = 0; step < count; ++step)
			(void)sendPanelEvent(*command, argument);
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
		const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
		if(m_encoderPress.active() && (!modifiers.isAltDown() || !modifiers.isLeftButtonDown()))
			releaseEncoderPress();
		// Some plugin hosts can lose the modifier key-up when focus changes. Poll
		// native state as a fail-safe so no panel row remains held indefinitely.
		// A held gamepad latch button stands in for Shift.
		if(!m_shiftPanelLatch.empty() && !m_gamepadLatchHeld
			&& !juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
			releasePanelButtonGestures();

		const auto hadFrontPanelSnapshot = m_frontPanelSnapshotValid;
		m_frontPanelSnapshotValid = refreshFrontPanelState(nowMilliseconds);
		if(hadFrontPanelSnapshot && !m_frontPanelSnapshotValid)
			m_lcdInteractionInputChanged = true;
		serviceUserSysexProgress();
		if(m_lcdInteractionInputChanged)
			updateLcdInteractionState();

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
		constexpr double g_gamepadEncoderClickMilliseconds = 300.0;
		constexpr double g_gamepadHighlightTimeoutMilliseconds = 120000.0;	// hide focus after 2 minutes idle
		constexpr float g_gamepadTriggerThreshold = 0.5f;
		constexpr float g_gamepadStickDeadzone = 0.2f;
		constexpr float g_gamepadStickNavigateThreshold = 0.6f;
		constexpr float g_gamepadStickDetentsPerSecond = 30.0f;
		constexpr float g_gamepadCoarseDetents = 8.0f;

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
			setGamepadFocus(*best);
	}

	void Editor::pressHeldControl(const md::PanelControl _control, const bool _latch)
	{
		const auto index = findGamepadTarget(_control);
		const auto packet = md::panelPacket(getModel(), _control);
		if(!index || !packet)
			return;
		auto* const button = m_gamepadTargets[*index].button;
		if(button->isChecked())
			return;

		// Mirror the mouse handler: an MM pattern bank normally toggles its own latch.
		if(getModel() == md::MachineModel::Monomachine && panelAffordances::isPatternBank(_control))
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

	void Editor::releaseGamepadInputs()
	{
		const auto held = m_gamepadHeldControls;
		for(auto it = held.rbegin(); it != held.rend(); ++it)
			releaseHeldControl(*it);
		m_gamepadHeldControls.clear();
		m_gamepadActHeld = false;
		m_gamepadRepeatDirection.reset();
		m_gamepadTouchTrig.reset();
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
				setGamepadFocus(m_gamepadFocus);
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
		const auto startRepeat = [&](const GamepadDirection _direction, const bool _fromStick)
		{
			m_gamepadRepeatDirection = _direction;
			m_gamepadRepeatFromStick = _fromStick;
			m_gamepadRepeatNextMilliseconds = _nowMilliseconds + g_gamepadRepeatDelayMilliseconds;
		};
		const auto turnHeldKnob = [&](const GamepadDirection _direction)
		{
			const float detents = _direction == GamepadDirection::Right ? 1.0f
				: _direction == GamepadDirection::Left ? -1.0f
				: _direction == GamepadDirection::Up ? g_gamepadCoarseDetents : -g_gamepadCoarseDetents;
			turnGamepadKnob(actKnob(), detents);
			m_gamepadActTurned = true;
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
					startRepeat(direction, false);
				}
				else if(!m_gamepadActHeld)
				{
					moveGamepadFocus(direction);
					startRepeat(direction, false);
				}
			}
			else if(releasedNow(button))
			{
				releaseHeldControl(arrow);
				if(direction == GamepadDirection::Left)
					releaseHeldControl(md::PanelControl::DataPageBackward);
				else if(direction == GamepadDirection::Right)
					releaseHeldControl(md::PanelControl::DataPageForward);
				if(m_gamepadRepeatDirection == direction && !m_gamepadRepeatFromStick)
					m_gamepadRepeatDirection.reset();
			}
		}

		// Right stick moves focus, with the same auto-repeat as the D-pad.
		std::optional<GamepadDirection> stickDirection;
		if(std::max(std::abs(state.rightX), std::abs(state.rightY)) > g_gamepadStickNavigateThreshold)
		{
			stickDirection = std::abs(state.rightX) > std::abs(state.rightY)
				? (state.rightX > 0.0f ? GamepadDirection::Right : GamepadDirection::Left)
				: (state.rightY > 0.0f ? GamepadDirection::Down : GamepadDirection::Up);
		}
		if(stickDirection)
		{
			if(!(m_gamepadRepeatFromStick && m_gamepadRepeatDirection == stickDirection))
			{
				if(!m_gamepadActHeld && !arrowLayer && !pageLayer)
					moveGamepadFocus(*stickDirection);
				startRepeat(*stickDirection, true);
			}
		}
		else if(m_gamepadRepeatFromStick)
		{
			m_gamepadRepeatDirection.reset();
		}

		if(m_gamepadRepeatDirection && _nowMilliseconds >= m_gamepadRepeatNextMilliseconds)
		{
			m_gamepadRepeatNextMilliseconds = _nowMilliseconds + g_gamepadRepeatIntervalMilliseconds;
			if(actKnob() && !m_gamepadRepeatFromStick)
				turnHeldKnob(*m_gamepadRepeatDirection);
			else if(!m_gamepadActHeld && !arrowLayer && !pageLayer)
				moveGamepadFocus(*m_gamepadRepeatDirection);
		}

		// Cross acts on the focused control: hold a button, or hold a knob to turn it
		// with the D-pad. A short Cross on a knob without turning clicks the encoder.
		if(pressedNow(Button::South) && m_gamepadFocus < m_gamepadTargets.size())
		{
			m_gamepadActHeld = true;
			m_gamepadActTarget = m_gamepadFocus;
			m_gamepadActStartMilliseconds = _nowMilliseconds;
			m_gamepadActTurned = false;
			if(m_gamepadTargets[m_gamepadFocus].button)
				pressHeldControl(m_gamepadTargets[m_gamepadFocus].control, m_gamepadLatchHeld);
		}
		else if(releasedNow(Button::South) && m_gamepadActHeld)
		{
			m_gamepadActHeld = false;
			if(!m_gamepadRepeatFromStick)
				m_gamepadRepeatDirection.reset();
			const auto& target = m_gamepadTargets[m_gamepadActTarget];
			if(target.button)
			{
				releaseHeldControl(target.control);
			}
			else if(target.knob && !m_gamepadActTurned
				&& _nowMilliseconds - m_gamepadActStartMilliseconds < g_gamepadEncoderClickMilliseconds)
			{
				// One press and one release, a timer tick apart, through the existing panel queue.
				if(const auto packet = md::panelEncoderPressPacket(getModel(), target.encoder))
				{
					m_panelSteps.push_back({ *packet, true });
					m_panelSteps.push_back({ *packet, false });
				}
			}
		}

		// Stick clicks jump focus to the trig row and to the data-entry knobs.
		if(!m_gamepadActHeld)
		{
			if(pressedNow(Button::LeftStick))
				if(const auto index = findGamepadTarget(md::PanelControl::Trigger1))
					setGamepadFocus(*index);
			if(pressedNow(Button::RightStick))
				if(const auto index = findGamepadTarget(m_encoders[0]))
					setGamepadFocus(*index);
		}

		// Touchpad: a 16-step strip, left to right.
		if(state.touching && !previous.touching)
		{
			const auto step = std::clamp(static_cast<int>(state.touchX * 16.0f), 0, 15);
			const auto trig = static_cast<md::PanelControl>(static_cast<int>(md::PanelControl::Trigger1) + step);
			pressHeldControl(trig, m_gamepadLatchHeld);
			m_gamepadTouchTrig = trig;
		}
		else if(!state.touching && previous.touching && m_gamepadTouchTrig)
		{
			releaseHeldControl(*m_gamepadTouchTrig);
			m_gamepadTouchTrig.reset();
		}

		// Left stick turns the focused knob, faster the further it is pushed.
		if(auto* const knob = focusKnob(); knob && std::abs(state.leftX) > g_gamepadStickDeadzone)
		{
			const auto magnitude = (std::abs(state.leftX) - g_gamepadStickDeadzone) / (1.0f - g_gamepadStickDeadzone);
			const auto rate = std::copysign(magnitude * magnitude * g_gamepadStickDetentsPerSecond, state.leftX);
			turnGamepadKnob(knob, rate * static_cast<float>(elapsedMilliseconds / 1000.0));
		}
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}
}
