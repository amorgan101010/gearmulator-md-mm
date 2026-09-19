#pragma once

#include "mdSampleImport.h"
#include <array>
#include <chrono>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "jucePluginEditorLib/pluginEditor.h"
#include "baseLib/event.h"

#include "mdFrontPanelPresentation.h"
#include "mdGamepad.h"
#include "mdGamepadAxes.h"
#include "mdHelpOverrides.h"
#include "mdLcdGesture.h"
#include "mdLcdInteractionModel.h"
#include "mdPanelAffordances.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdsyseximport.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juce
{
	class Image;
	class Graphics;
}

namespace Rml
{
	class Element;
	class Event;
}

namespace juceRmlUi
{
	class ElemButton;
	class ElemCanvas;
	class ElemKnob;
}

namespace md
{
	class Hardware;
}

namespace mdJucePlugin
{
	class Controller;
	class PixelPerfectPanel;
	struct EditorIdentityTestAccess;

	namespace parameterHelp
	{
		struct Entry;
		struct ValueHash;
	}

	class SampleDropTarget;
	class MachineRack;

	class Editor final : public jucePluginEditorLib::Editor, juce::MultiTimer,
		private juce::FocusChangeListener
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> createDeviceSpecificSettings(
			const std::string& _templateName, Rml::Element* _root) override;
		std::string getSettingsTemplateSuffix() const override;

		// Reapplies the configured wheel/encoder drag-speed percentages to the
		// panel knobs. Called on create and from the settings page.
		void applyPanelSpeeds();
		void applyPixelPerfectPanel();
		void applyLcdInteraction();
		// Rereads the tooltip on/off and pop-up delay settings. Called on create and from the settings page.
		void applyTooltipSettings();
		// Rereads the touchpad and gyro axis settings. Called on create and from the settings page.
		void applyGamepadSettings();
		md::MachineModel getModel() const;
		void loadInstalledFactoryStorage();
		void chooseStorageImage();
		void restorePreviousStorage();
		bool hasStorageRecoveryImage() const;

		// Firmware image (stock OS or a community OS such as X.13 / EMS) for this instance
		void bindSettingsButton();
		void chooseFirmwareImage();
		void useStockFirmware();
		std::string getFirmwareDescription() const;
		void chooseUserSysexFile();
		// Machinedrum UW: decode audio files, pick RAM slots, send them as SDS
		void chooseSampleFiles();
		// files dropped on the panel: one .syx file, or audio files (Machinedrum)
		void importDroppedFiles(const std::vector<std::string>& _files);
		void cancelUserSysexTransfer();
		bool canResumeUserSysexTransfer() const;
		void resumeUserSysexTransfer();
		std::string getUserSysexMenuText() const;
		bool isUserSysexTransferActive() const;
		bool canCancelUserSysexTransfer() const;
		std::weak_ptr<void> getLifetimeToken() const { return m_lifetimeToken; }

		static constexpr int g_panelSpeedPercents[] = {50, 75, 100, 150, 200, 300};
		static constexpr int g_tooltipDelaysMs[] = {0, 250, 500, 1000, 2000};
		static constexpr int g_defaultTooltipDelayMs = 500;
		static constexpr const char* g_tooltipsEnabledKey = "tooltipsEnabled";
		static constexpr const char* g_tooltipDelayKey = "tooltipDelayMs";

	private:
		friend struct EditorIdentityTestAccess;

		void timerCallback(int _timerId) override;

		std::shared_ptr<md::FrontPanelPublisher> getFrontPanelPublisher() const;
		bool sendPanelEvent(uint8_t _command, uint8_t _argument) const;
		bool refreshFrontPanelState(double _nowMilliseconds);
		void createLcd();
		void updateLcdInteractionState();
		std::optional<unsigned> lcdTargetAt(const Rml::Event& _event) const;
		// Mouse position in native LCD pixels, or nothing outside the drawn display.
		std::optional<std::pair<int, int>> lcdNativePointAt(const Rml::Event& _event) const;
		void updateLcdHover(const Rml::Event& _event);
		void clearLcdHover();
		void cancelLcdGesture();
		void emitEncoderSteps(md::PanelEncoder _encoder, int _steps) const;
		void createButtons();
		void createPanelAffordances();
		void bindPanelTarget(const char* _id, md::PanelControl _control);
		void bindPanelChord(const char* _id, md::PanelControl _control);
		void pressPanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet, bool _shiftDown);
		void releasePanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet);
		void releaseActivePanelButtons();
		void beginPanelGesture(Rml::Element* _element,
			std::initializer_list<md::PanelControl> _controls);
		void endPanelGesture();
		void releasePanelButtonGestures();
		void releaseEncoderPress();
		void cancelPanelInputGestures();
		void releaseAllPanelInputs();
		void globalFocusChanged(juce::Component* _focusedComponent) override;
		void queuePanelPulse(md::PanelControl _control, int _count = 1);
		void servicePanelQueue();
		void servicePanelNavigation();
		void selectMachinedrumTrack(int _track);
		void selectMachinedrumDataPage(int _page);
		void selectMonomachineDataPage(int _page);
		void selectMonomachineTrigMode(int _mode);
		void togglePatternBankLatch(juceRmlUi::ElemButton* _button, const md::PanelPacket& _packet);
		void releasePatternBankLatch();
		void createEncoders();
		void createMasterVolume();
		void syncMasterVolume();
		void configureEncoder(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		void onEncoderChanged(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		// Standalone game controller: LSDJ-style focus over the panel controls.
		struct GamepadTarget
		{
			Rml::Element* element = nullptr;
			juceRmlUi::ElemButton* button = nullptr;	// set for a panel button
			juceRmlUi::ElemKnob* knob = nullptr;		// set for an encoder
			md::PanelControl control{};
			md::PanelEncoder encoder{};
		};
		enum class GamepadDirection { Up, Down, Left, Right };

		void createGamepad();
		void serviceGamepad(double _nowMilliseconds);
		void moveGamepadFocus(GamepadDirection _direction);
		void setGamepadFocus(size_t _index);
		// The left stick's cursor: _x/_y are the stick, and whatever control it overlaps takes the focus.
		void moveGamepadCursor(float _x, float _y, double _elapsedMilliseconds);
		void centreGamepadCursor();		// onto the focused control, after the D-pad moved the focus
		void placeGamepadCursor(bool _requestUpdate = true);
		void stepGamepadCursor();		// once per drawn frame while the left stick is pushed
		std::optional<size_t> findGamepadTarget(md::PanelControl _control) const;
		std::optional<size_t> findGamepadTarget(const juceRmlUi::ElemKnob* _knob) const;
		// _momentaryBanks: MM bank keys act like any held key instead of toggling the click latch.
		void pressHeldControl(md::PanelControl _control, bool _latch, bool _momentaryBanks = false);
		void releaseHeldControl(md::PanelControl _control);
		void turnGamepadKnob(juceRmlUi::ElemKnob* _knob, float _detents);
		void releaseGamepadInputs();
		// Touchpad and gyro axes: the knob an axis turns right now (null for none), turning it, and holding
		// FUNCTION for the axes that want it.
		juceRmlUi::ElemKnob* gamepadAxisKnob(gamepadAxes::Axis _axis) const;
		// _position 0..1 for absolute mode (negative when unknown); _detents for relative movement.
		void driveGamepadAxis(gamepadAxes::Axis _axis, bool _engaged, float _position, float _detents);
		md::PanelEncoder knobEncoder(const juceRmlUi::ElemKnob* _knob) const;
		// The parameter value under a data entry knob on the current data page and track, or -1 if unknown.
		int currentKnobValue(const juceRmlUi::ElemKnob* _knob) const;
		void setGamepadAxisFunction(bool _held);
		// Cross on a knob: push its switch, and let go of it again.
		void pushGamepadKnob(const GamepadTarget& _target, double _nowMilliseconds);
		void releaseGamepadKnob();

		// Standalone computer-keyboard control of the panel.
		void createKeyboardControl();
		void onPanelKey(Rml::Event& _event, bool _down);
		// Acts on a key after auto-repeat filtering; true if the key belongs to panel control.
		bool handlePanelKey(int _key, bool _down, bool _shift);
		void serviceKeyboardReleases(double _nowMilliseconds);
		void resetKeyboardControl();
		void releaseKeyboardEncoderPress();

		// DATA PAGE currently lit on the Monomachine: 0 SYNTHESIS .. 6 LFO 3.
		std::optional<int> currentMonomachineDataPage() const;
		// Page currently lit on the Machinedrum: 0 SYNTHESIS, 1 EFFECTS, 2 ROUTING.
		std::optional<int> currentMachinedrumDataPage() const;

		// Hover help for parameter abbreviations.
		struct TooltipText
		{
			std::string abbreviation;	// as the LCD shows it, e.g. ATK
			std::string name;
			std::string description;
			std::string footer;			// where it is, e.g. "Amplification page, knob A"
		};
		// What the pointer is over: a knob or LCD field (encoder), or the machine name on the LCD.
		struct TooltipTarget
		{
			Rml::Element* anchor = nullptr;	// the tooltip is shown under this; null for nothing
			std::optional<unsigned> encoder;
			bool machineName = false;

			bool operator==(const TooltipTarget& _other) const
			{
				return anchor == _other.anchor && encoder == _other.encoder && machineName == _other.machineName;
			}
			bool operator!=(const TooltipTarget& _other) const { return !(*this == _other); }
		};
		void createParameterTooltip();
		void updateParameterTooltip();
		void hideParameterTooltip();
		TooltipTarget tooltipTarget() const;
		// The help for _target on the current screen, or nothing if it isn't recognised.
		std::optional<TooltipText> describe(const TooltipTarget& _target) const;
		std::optional<TooltipText> describeMachineName() const;
		std::optional<TooltipText> describeMachinedrumEncoder(unsigned _encoder) const;
		std::optional<TooltipText> describeMonomachineEncoder(unsigned _encoder) const;
		// Appends ", value N" on data pages, the knob's value on the current track.
		void appendCurrentValue(TooltipText& _text, unsigned _encoder) const;
		// On a Monomachine LFO page: " Now: ..." text for PAGE (encoder 0) or DEST (encoder 1).
		std::string lfoTargetDescription(unsigned _encoder) const;
		// A table entry with any user overrides of its text applied.
		TooltipText tooltipFor(const parameterHelp::Entry& _entry, std::string _footer) const;
		// " Now: <value>. <what it means>" for a setting whose value the LCD prints as text.
		std::string nowText(const parameterHelp::ValueHash& _value) const;

		void createLeds();
		bool updateLeds();
		void paintLcd(const juce::Image& _target, juce::Graphics& _graphics) const;

		enum class StorageImageBookmark
		{
			None,
			Factory,
			Other
		};

		void chooseStorageImage(StorageImageBookmark _bookmark);
		void confirmStorageImage(const juce::File& _file,
			StorageImageBookmark _bookmark);
		void showStorageOperationResult(bool _success, const juce::String& _message);
		std::optional<md::SysexImportProgress> getUserSysexProgress() const;
		void sendUserSysexFile(const juce::File& _file, const md::SysexImportTicket& _ticket);
		void sendUserSysexBytes(std::vector<uint8_t>&& _bytes, const juce::File& _source, const md::SysexImportTicket& _ticket);
		std::optional<md::SysexImportTicket> beginUserSysexTicket();
		void importSampleFiles(const std::vector<juce::File>& _files, const md::SysexImportTicket& _ticket);
		void openSampleSlotMenu(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, const md::SysexImportTicket& _ticket);
		void confirmSampleSlots(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, uint32_t _firstSlot, const md::SysexImportTicket& _ticket);
		void sendSamples(const std::shared_ptr<std::vector<sampleImport::DecodedSample>>& _samples, uint32_t _firstSlot, const md::SysexImportTicket& _ticket);
		void showSampleError(const juce::String& _message);
		void startUserSysexTransfer(const std::shared_ptr<md::PreparedMidiSysexTransfer>& _prepared,
			const juce::File& _file, const md::SysexImportTicket& _ticket, bool _receiveModeConfirmed);
		void launchUserSysexFileChooser(const md::SysexImportTicket& _ticket);
		void showUserSysexError(const juce::String& _message);
		void serviceUserSysexProgress();

		enum class StorageImageFlow
		{
			None,
			Choosing,
			AwaitingConfirmation
		};

		Controller& m_controller;
		const md::MachineModel m_model;
		juceRmlUi::ElemCanvas* m_lcdCanvas = nullptr;
		std::unique_ptr<PixelPerfectPanel> m_pixelPerfectPanel;
		md::FrontPanel m_frontPanelSnapshot;
		bool m_frontPanelSnapshotValid = false;
		// Kept between frames: fetching it takes the device lock, which waits for the audio block to finish.
		std::shared_ptr<md::FrontPanelPublisher> m_frontPanelPublisher;
		bool m_lcdChanged = true;
		bool m_lcdInteractionInputChanged = true;
		std::optional<lcdInteraction::State> m_lcdInteractionState;
		std::optional<unsigned> m_lcdHoverEncoder;
		std::optional<unsigned> m_lcdWheelEncoder;
		lcdInteraction::DragGesture m_lcdDragGesture;
		lcdInteraction::DetentAccumulator m_lcdWheelAccumulator;
		FrontPanelLedPresentation m_ledPresentation;
		bool m_ledsChanged = true;
		md::FrontPanelLedTransitionStatus m_ledTransitionStatus;
		bool m_ledTransitionStatusValid = false;
		bool m_ledResyncPending = false;
		uint64_t m_ledResyncSequence = 0;

		md::PanelRowState m_panelRows;
		juceRmlUi::ElemButton* m_patternBankButton = nullptr;
		std::optional<md::PanelPacket> m_patternBankPacket;
		Rml::Element* m_panelGestureElement = nullptr;
		std::vector<md::PanelPacket> m_panelGesturePackets;
		panelAffordances::ShiftPanelLatch m_shiftPanelLatch;
		panelAffordances::EncoderPressGesture m_encoderPress;
		juceRmlUi::ElemKnob* m_pressedEncoder = nullptr;

		struct ActivePanelButton
		{
			juceRmlUi::ElemButton* button = nullptr;
			md::PanelPacket packet;
		};
		std::vector<ActivePanelButton> m_activePanelButtons;

		struct PanelStep
		{
			md::PanelPacket packet;
			bool press = false;
		};
		std::deque<PanelStep> m_panelSteps;
		int m_panelSettleTicks = 0;
		panelAffordances::PendingTarget<panelAffordances::g_machinedrumDataPages.size()>
			m_machinedrumDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineDataPages.size()>
			m_monomachineDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineTrigModes.size()>
			m_monomachineTrigModeTarget;

		std::array<juceRmlUi::ElemKnob*, 8> m_encoders{};
		std::array<float, 8> m_encLast{};
		std::array<float, 8> m_encAccum{};
		juceRmlUi::ElemKnob* m_levelEncoder = nullptr;
		float m_levelLast = 0.0f;
		float m_levelAccum = 0.0f;
		juceRmlUi::ElemKnob* m_soundEncoder = nullptr;
		float m_soundLast = 0.0f;
		float m_soundAccum = 0.0f;
		juceRmlUi::ElemKnob* m_masterVolume = nullptr;
		float m_masterVolumeGain = 1.0f;	// last gain this knob published, to detect settings-page edits

		std::array<Rml::Element*, 16> m_stepLeds{};
		std::array<Rml::Element*, 16> m_drumLeds{};

		struct StatusLedElem
		{
			Rml::Element* elem;
			uint8_t bit;	// md::FrontPanel::StatusLed
		};
		std::array<StatusLedElem, 5> m_statusLeds{};
		std::array<StatusLedElem, 6> m_mdModeLeds{};

		struct RawLedElem
		{
			Rml::Element* elem = nullptr;
			uint8_t bank = 0;
			uint8_t bit = 0;
		};
		std::array<RawLedElem, 4> m_mdPageLeds{};
		std::array<RawLedElem, 20> m_mmPanelLeds{};
		std::unique_ptr<juce::FileChooser> m_storageFileChooser;
		std::unique_ptr<juce::FileChooser> m_firmwareFileChooser;
		bool m_firmwareDialogOpen = false;
		void confirmFirmwareImage(const std::string& _path);
		StorageImageFlow m_storageImageFlow = StorageImageFlow::None;
		std::unique_ptr<juce::FileChooser> m_sysexFileChooser;
		std::unique_ptr<juce::FileChooser> m_sampleFileChooser;
		std::unique_ptr<SampleDropTarget> m_sampleDropTarget;
		std::unique_ptr<MachineRack> m_machineRack;
		// what goes into which slot once the running transfer completes
		struct PendingSampleSlots
		{
			md::SysexImportTicket ticket;
			sampleImport::SlotLedger slots;
		};
		std::optional<PendingSampleSlots> m_pendingSampleSlots;
		bool m_sysexChooserOpen = false;
		bool m_sysexTransferWasActive = false;
		md::SysexImportTicket m_sysexMonitoredTicket;
		md::MidiSysexTransferState m_sysexLastState =
			md::MidiSysexTransferState::Idle;
		size_t m_sysexLastSent = 0;
		uint32_t m_sysexLastServiceSerial = 0;
		uint32_t m_sysexReceivePromptId = 0;
		size_t m_sysexReceivePromptStep = 0;
		double m_sysexLastAdvanceMilliseconds = 0.0;
		bool m_sysexStallWarningShown = false;
		std::shared_ptr<void> m_lifetimeToken = std::make_shared<int>(0);

		std::unique_ptr<Gamepad> m_gamepad;
		Gamepad::State m_gamepadPrevious;
		double m_gamepadLastPollMilliseconds = 0.0;
		std::vector<GamepadTarget> m_gamepadTargets;
		size_t m_gamepadFocus = 0;
		Rml::Element* m_gamepadFocusRing = nullptr;
		Rml::Element* m_gamepadCursor = nullptr;
		Rml::Vector2f m_gamepadCursorPosition{ 0.5f, 0.5f };	// fraction of the panel, so it survives a resize
		double m_gamepadCursorLastMoveMilliseconds = 0.0;	// last time the left stick was pushed
		Rml::Vector2f m_gamepadCursorStick{ 0.0f, 0.0f };	// left stick, zero while it isn't moving the cursor
		double m_gamepadCursorLastFrameMilliseconds = 0.0;	// time of the last cursor frame, 0 while idle
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_gamepadCursorFrame;
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_gamepadCursorFrameDone;
		bool m_gamepadCursorVisible = false;
		bool m_gamepadLatchHeld = false;				// L1: gamepad equivalent of holding Shift
		std::vector<md::PanelControl> m_gamepadHeldControls;	// pressed by the pad, awaiting release
		// Cross held on the focused control. On a knob it holds the knob's switch pushed.
		bool m_gamepadActHeld = false;
		size_t m_gamepadActTarget = 0;
		std::optional<md::PanelPacket> m_gamepadPushedKnobPacket;
		juceRmlUi::ElemKnob* m_gamepadPushedKnob = nullptr;
		double m_gamepadKnobPushMilliseconds = 0.0;
		double m_gamepadKnobReleaseMilliseconds = 0.0;	// when to let go, once Cross is up
		// D-pad auto-repeat.
		std::optional<GamepadDirection> m_gamepadRepeatDirection;
		double m_gamepadRepeatNextMilliseconds = 0.0;
		int m_gamepadTrack = 0;		// 0-5 on the Monomachine, 0-15 on the Machinedrum
		int m_gamepadPage = 0;		// Machinedrum data page
		std::array<gamepadAxes::Settings, std::size(gamepadAxes::g_axes)> m_gamepadAxes{};
		bool m_gamepadAxisFunctionHeld = false;	// FUNCTION pressed for a touchpad or gyro axis
		struct GamepadAxisRun
		{
			juceRmlUi::ElemKnob* knob = nullptr;	// the knob being driven while engaged
			int value = -1;							// absolute mode: the knob's value as we have set it
		};
		std::array<GamepadAxisRun, std::size(gamepadAxes::g_axes)> m_gamepadAxisRuns{};

		bool m_keyboardControl = false;
		std::vector<std::pair<int, double>> m_keyboardPendingReleases;	// key, deadline: releases waiting out auto-repeat
		std::vector<int> m_keyboardHeldKeys;			// Rml key identifiers currently down, to ignore auto-repeat
		bool m_keyboardFunctionHeld = false;			// Ctrl
		std::optional<size_t> m_keyboardEncoder;		// index into g_keyboardEncoders while its key is held
		bool m_keyboardEncoderTurned = false;
		std::optional<md::PanelPacket> m_keyboardEncoderPressPacket;	// encoder switch held by [ or ]
		juceRmlUi::ElemKnob* m_keyboardPressedKnob = nullptr;

		Rml::Element* m_parameterTooltip = nullptr;
		Rml::Element* m_lcdArea = nullptr;
		std::optional<unsigned> m_tooltipHoverKnob;		// mouse over encoder A-H
		bool m_tooltipLcdMachineName = false;			// mouse over the machine name on the LCD
		// The gamepad focus highlight shows only after controller input and hides after a quiet spell.
		double m_gamepadLastActivityMilliseconds = 0.0;
		bool m_gamepadHighlightVisible = false;
		HelpOverrides m_help;							// user edits to the tooltip text, see mdHelpOverrides.h
		bool m_tooltipsEnabled = true;
		int m_tooltipDelayMs = g_defaultTooltipDelayMs;
		// What the pointer rests on (anchor, knob, machine name) and since when, for the pop-up delay.
		TooltipTarget m_tooltipRestTarget;
		std::chrono::steady_clock::time_point m_tooltipRestSince{};
		std::string m_parameterTooltipContent;
	};
}
