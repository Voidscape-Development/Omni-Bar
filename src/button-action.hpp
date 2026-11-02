/*
OBS Omni Bar
Copyright (C) 2025 Eion

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs.h>
#include <obs-data.h>
#include <QString>
#include <QIcon>
#include <QColor>
#include <memory>
#include <QList>

#include "display-metrics.hpp"

// Forward declaration
class ButtonConfig;

// Stored by name in the configuration file, so the numbers are free to move.
enum class ActionType {
	None,
	Frontend,
	SourceHotkey,
	SourceFilter,
	SourceVisibility,
	Spacer,
	Divider,
	Display,
	Scene,
	AudioMute
};

// What a mute button does each time it is pressed. Stored as numbers.
enum class MuteMode { Toggle = 0, Mute = 1, Unmute = 2 };

enum class FrontendActionType {
	StartStreaming,
	StopStreaming,
	ToggleStreaming,
	StartRecording,
	StopRecording,
	ToggleRecording,
	PauseRecording,
	UnpauseRecording,
	TogglePauseRecording,
	StartReplayBuffer,
	StopReplayBuffer,
	ToggleReplayBuffer,
	SaveReplayBuffer,
	StartVirtualCam,
	StopVirtualCam,
	ToggleVirtualCam,
	EnableStudioMode,
	DisableStudioMode,
	ToggleStudioMode,
	TransitionToProgram
};

// Everything about one frontend action that is not behaviour: the name it is
// stored under, the name shown, its default icon, what it reports on, and the
// family it belongs to. Actions that start, stop or toggle the same thing share
// a family, which the action picker shows as one entry with a choice of
// variant. One table holds all of them, so adding an action is one line there
// plus its behaviour.
struct FrontendActionInfo {
	FrontendActionType type;
	// Written to the configuration file, so it must never change.
	const char *id;
	const char *labelKey;
	const char *icon;
	const char *family;
	// Short name of the variant within the family: Toggle, Start, Stop...
	const char *variantKey;
	BarState state;
};

// Where a button's label sits relative to its icon. The numbers are written to
// the configuration file, so existing ones must keep their values.
enum class ButtonDisplayMode { IconOnly = 0, TextOnly = 1, TextRight = 2, TextBelow = 3, TextLeft = 4, TextAbove = 5 };

// Whether a button asks before it runs. FollowObs asks exactly when OBS's own
// controls would - stopping the stream, for instance, if "confirm before
// stopping" is on in the OBS settings. Stored as numbers.
enum class ConfirmMode { FollowObs = 0, Always = 1, Never = 2 };

// How a group presents its children.
enum class GroupDisplayMode { Flyout = 0, Inline = 1 };

// What causes a group to expand. In WhenActive the group's condition is what
// opens and closes it; in the other two the condition only gates whether it may
// open at all.
enum class GroupExpandMode { Click = 0, WhenActive = 1, Hover = 2 };

// The OBS state a condition watches. The numbers are written to the
// configuration file, so existing ones must keep their values.
enum class ConditionSource {
	Always = 0,
	OwnAction = 1,
	Streaming = 2,
	Recording = 3,
	RecordingPaused = 4,
	ReplayBuffer = 5,
	VirtualCam = 6,
	StudioMode = 7
};

class ButtonAction;

// An optional test against what OBS is currently doing, used both to decide
// whether an entry appears on the bar and whether a group may be open.
struct ActivationCondition {
	ConditionSource source = ConditionSource::Always;
	bool inverted = false;

	bool isSet() const { return source != ConditionSource::Always; }

	// An unset condition always holds. action supplies the OwnAction source
	// and may be null. With a simulated state the test is made against that
	// instead of what OBS is doing, for the settings dialog's preview.
	bool isSatisfied(const ButtonAction *action, const ObsStateSnapshot *simulated = nullptr) const;

	// Short phrase for the settings list.
	QString describe() const;

	void serialize(obs_data_t *data, const char *prefix) const;
	static ActivationCondition deserialize(obs_data_t *data, const char *prefix);
};

class ButtonAction {
public:
	virtual ~ButtonAction() = default;
	virtual ActionType getType() const = 0;
	virtual void execute() = 0;
	virtual bool isActive() const { return false; }
	virtual bool isValid() const { return true; }
	virtual QString getDisplayName() const = 0;
	virtual obs_data_t *serialize() const = 0;
	static std::unique_ptr<ButtonAction> deserialize(obs_data_t *data);

	// What this action reports on, which picks the colour it lights up in.
	virtual BarState barState() const { return BarState::None; }

	// Whether the action would show as active in a simulated state. Actions
	// that report on OBS's outputs answer from the state; the rest, such as a
	// filter, have nothing simulated to go on and say what is true now.
	virtual bool isActiveIn(const ObsStateSnapshot &state) const;

	// True when OBS's own controls would ask before doing what this is about
	// to do, given what is running right now.
	virtual bool obsWouldConfirm() const { return false; }

	// Question put to the user before running, when one is asked.
	virtual QString confirmationQuestion() const;
};

// Placeholder for a button that does nothing on its own, used by groups that
// exist purely to hold children.
class NoAction : public ButtonAction {
public:
	ActionType getType() const override { return ActionType::None; }
	void execute() override {}
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;
};

class FrontendAction : public ButtonAction {
public:
	FrontendAction(FrontendActionType type);

	ActionType getType() const override { return ActionType::Frontend; }
	void execute() override;
	bool isActive() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;
	BarState barState() const override;
	bool obsWouldConfirm() const override;
	QString confirmationQuestion() const override;

	FrontendActionType getActionType() const { return actionType; }
	void setActionType(FrontendActionType type) { actionType = type; }

	// Stable identifier written to the configuration file.
	static QString getFrontendActionName(FrontendActionType type);
	static FrontendActionType getFrontendActionFromName(const QString &name);

	// Translated name shown to the user.
	static QString getFrontendActionLabel(FrontendActionType type);

	static const FrontendActionInfo &info(FrontendActionType type);
	static const QList<FrontendActionInfo> &all();
	// The members of a family, in the order the picker offers them.
	static QList<FrontendActionType> familyMembers(const QString &family);

private:
	FrontendActionType actionType;
};

class SourceHotkeyAction : public ButtonAction {
public:
	SourceHotkeyAction(const QString &source, const QString &hotkey);

	ActionType getType() const override { return ActionType::SourceHotkey; }
	void execute() override;
	bool isValid() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	QString getSourceName() const { return sourceName; }
	QString getHotkeyName() const { return hotkeyName; }
	void setSourceName(const QString &name) { sourceName = name; }
	void setHotkeyName(const QString &name) { hotkeyName = name; }

	// Name of the source a hotkey is registered against, or an empty string
	// for hotkeys that belong to OBS itself rather than to a source.
	static QString hotkeyOwnerName(obs_hotkey_t *hotkey);

private:
	QString sourceName;
	QString hotkeyName;
	obs_hotkey_id hotkeyId = OBS_INVALID_HOTKEY_ID;
	// Hotkeys are stored by internal name; the readable description is
	// looked up once for display.
	mutable QString cachedDescription;

	void refreshHotkeyId();
	QString hotkeyDescription() const;
};

class SourceFilterAction : public ButtonAction {
public:
	SourceFilterAction(const QString &source, const QString &filter);

	ActionType getType() const override { return ActionType::SourceFilter; }
	void execute() override;
	bool isActive() const override;
	bool isValid() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	QString getSourceName() const { return sourceName; }
	QString getFilterName() const { return filterName; }
	void setSourceName(const QString &name) { sourceName = name; }
	void setFilterName(const QString &name) { filterName = name; }

private:
	QString sourceName;
	QString filterName;
};

// Shows or hides a source in a scene. An empty scene name means whichever scene
// is on air at the moment of pressing, so one button can serve a webcam that
// appears in several scenes.
class SourceVisibilityAction : public ButtonAction {
public:
	SourceVisibilityAction(const QString &scene, const QString &source);

	ActionType getType() const override { return ActionType::SourceVisibility; }
	void execute() override;
	bool isActive() const override;
	bool isValid() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	QString getSceneName() const { return sceneName; }
	QString getSourceName() const { return sourceName; }
	void setSceneName(const QString &name) { sceneName = name; }
	void setSourceName(const QString &name) { sourceName = name; }

	bool followsCurrentScene() const { return sceneName.isEmpty(); }

	// True when the source no longer exists at all. For a button that
	// follows the current scene, the source being absent from the scene on
	// air is expected rather than a fault.
	bool sourceMissing() const;

private:
	// The scene the action works on, with a reference the caller releases.
	obs_source_t *targetScene() const;

	QString sceneName;
	QString sourceName;
};

// Switches the scene on air, as clicking it in the Scenes dock does: in studio
// mode it goes to the preview, ready to transition.
class SceneAction : public ButtonAction {
public:
	explicit SceneAction(const QString &scene);

	ActionType getType() const override { return ActionType::Scene; }
	void execute() override;
	// Lit while the scene is the one on air.
	bool isActive() const override;
	bool isValid() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	QString getSceneName() const { return sceneName; }
	void setSceneName(const QString &name) { sceneName = name; }

private:
	QString sceneName;
};

// Mutes or unmutes an audio source - a microphone, desktop audio, a media
// source with sound.
class AudioMuteAction : public ButtonAction {
public:
	AudioMuteAction(const QString &source, MuteMode mode = MuteMode::Toggle);

	ActionType getType() const override { return ActionType::AudioMute; }
	void execute() override;
	// Lit while the source is muted, the way the OBS mixer marks it.
	bool isActive() const override;
	bool isValid() const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	QString getSourceName() const { return sourceName; }
	void setSourceName(const QString &name) { sourceName = name; }
	MuteMode getMode() const { return mode; }
	void setMode(MuteMode value) { mode = value; }

private:
	QString sourceName;
	MuteMode mode;
};

class SpacerAction : public ButtonAction {
public:
	SpacerAction(int width = 10, bool expanding = false);

	ActionType getType() const override { return ActionType::Spacer; }
	void execute() override {}
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	int getWidth() const { return spacerWidth; }
	void setWidth(int w) { spacerWidth = w; }

	// A flexible space takes whatever length the bar has left over, pushing
	// everything after it to the far end. Its width is then only a minimum.
	bool isExpanding() const { return expanding; }
	void setExpanding(bool value) { expanding = value; }

private:
	int spacerWidth;
	bool expanding;
};

// A line drawn across the bar to visually separate neighbouring buttons.
class DividerAction : public ButtonAction {
public:
	DividerAction(int thickness = 1, int lengthPercent = 70);

	ActionType getType() const override { return ActionType::Divider; }
	void execute() override {}
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	int getThickness() const { return thickness; }
	void setThickness(int value) { thickness = value; }

	// Length of the line across the bar, as a percentage of the bar's
	// thickness, so it can be inset from the edges.
	int getLengthPercent() const { return lengthPercent; }
	void setLengthPercent(int value) { lengthPercent = value; }

	bool hasCustomColor() const { return useCustomColor && color.isValid(); }
	QColor getCustomColor() const { return color; }
	void setCustomColor(const QColor &value)
	{
		color = value;
		useCustomColor = value.isValid();
	}
	void clearCustomColor()
	{
		useCustomColor = false;
		color = QColor();
	}

private:
	int thickness;
	int lengthPercent;
	bool useCustomColor = false;
	QColor color;
};

// A live readout on the bar rather than something to press: how long the stream
// has been up, what the recording is costing in bitrate, and so on. The value
// is collected by OmniBarMetrics; this only says which one to show.
class DisplayAction : public ButtonAction {
public:
	DisplayAction(DisplayMetric metric = DisplayMetric::StreamTime, int minimumWidth = 0);

	ActionType getType() const override { return ActionType::Display; }
	void execute() override {}
	// Reports whether the metric has anything live behind it, which is what
	// makes "only while this entry's own value is live" a usable condition.
	bool isActive() const override;
	bool isActiveIn(const ObsStateSnapshot &state) const override;
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	DisplayMetric getMetric() const { return metric; }
	void setMetric(DisplayMetric value) { metric = value; }

	// Only the Time of day metric reads this; it is kept on every readout so
	// switching a readout to the clock and back does not lose the choice.
	ClockFormat getClockFormat() const { return clockFormat; }
	void setClockFormat(ClockFormat value) { clockFormat = value; }

	// Floor for the entry's width, in pixels. Digits are not all the same
	// width in every font, so a timer that is free to shrink shuffles its
	// neighbours about as it counts. Zero fits the content.
	int getMinimumWidth() const { return minimumWidth; }
	void setMinimumWidth(int value) { minimumWidth = value; }

	// Draw the value in the bar's warning or critical colour once it crosses
	// its threshold. Only means anything for metrics that have one.
	bool getWarnColors() const { return warnColors; }
	void setWarnColors(bool value) { warnColors = value; }

	// Reading as it stands right now, formatted for the bar.
	QString currentValue() const;

private:
	DisplayMetric metric;
	int minimumWidth;
	ClockFormat clockFormat = ClockFormat::Hours24;
	bool warnColors = true;
};

// Button configuration class
class ButtonConfig {
public:
	ButtonConfig();

	QString id;
	QString label;
	QString tooltip;
	QString iconPath;
	std::unique_ptr<ButtonAction> action;

	ButtonDisplayMode displayMode = ButtonDisplayMode::IconOnly;

	// While this does not hold, the entry is left off the bar entirely.
	ActivationCondition showCondition;

	// Recolour a user-supplied icon to match the bar. Bundled icons always
	// follow the bar; a custom file is drawn as authored unless this is set,
	// since recolouring would ruin a deliberately coloured graphic.
	bool tintIcon = false;

	// Per-button accent override for the hover/active states.
	bool useCustomColor = false;
	QColor customColor;

	// Breathe the active fill in and out while the action is running, so a
	// live button is noticeable without having to be looked at directly. The
	// speed and depth of the breath are part of the bar's style.
	bool pulseWhenActive = false;

	// Whether pressing the button asks first.
	ConfirmMode confirmMode = ConfirmMode::FollowObs;

	// Group settings. Children are limited to one level deep: a child is
	// never itself a group.
	bool isGroup = false;
	GroupDisplayMode groupDisplay = GroupDisplayMode::Flyout;
	GroupExpandMode groupExpand = GroupExpandMode::Click;
	// In WhenActive this drives the group open and closed; otherwise it gates
	// whether clicking or hovering may open it.
	ActivationCondition expandCondition;
	QList<std::shared_ptr<ButtonConfig>> children;

	obs_data_t *serialize() const;
	static std::shared_ptr<ButtonConfig> deserialize(obs_data_t *data);

	bool isValid() const;
	bool isSpacer() const;
	bool isDivider() const;
	bool isDisplay() const;

	// True for entries that are part of the bar rather than a button to
	// press. These are laid out along the bar, never go inside a group, and
	// have no active state of their own to pulse.
	bool isDecoration() const;

	// True when the button runs something of its own, as opposed to a group
	// that only opens and closes.
	bool hasAction() const;

	// True when pressing the button right now should ask before running.
	bool needsConfirmation() const;

	// The colour the button lights up in while active: its own override, or
	// the style's colour for what its action reports on.
	QColor activeColor(const BarStyle &style) const;

	// True when the button points at a source, filter or hotkey that cannot
	// currently be found.
	bool targetMissing() const;

	// Text shown for this button in the settings tree.
	QString displayText() const;

	// One-line summary of what the button does, for the settings tree.
	QString summaryText() const;

	// Deep copy, used so the editor can be cancelled without touching the
	// live configuration.
	std::shared_ptr<ButtonConfig> clone() const;
};
