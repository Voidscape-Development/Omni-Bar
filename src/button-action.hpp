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

// Forward declaration
class ButtonConfig;

enum class ActionType { None, Frontend, SourceHotkey, SourceFilter, SourceVisibility, Spacer, Divider };

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

// Where a button's label sits relative to its icon. The numbers are written to
// the configuration file, so existing ones must keep their values.
enum class ButtonDisplayMode { IconOnly = 0, TextOnly = 1, TextRight = 2, TextBelow = 3, TextLeft = 4, TextAbove = 5 };

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
	// and may be null.
	bool isSatisfied(const ButtonAction *action) const;

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

	FrontendActionType getActionType() const { return actionType; }
	void setActionType(FrontendActionType type) { actionType = type; }

	// Stable identifier written to the configuration file.
	static QString getFrontendActionName(FrontendActionType type);
	static FrontendActionType getFrontendActionFromName(const QString &name);

	// Translated name shown to the user.
	static QString getFrontendActionLabel(FrontendActionType type);

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

private:
	QString sceneName;
	QString sourceName;
};

class SpacerAction : public ButtonAction {
public:
	SpacerAction(int width = 10);

	ActionType getType() const override { return ActionType::Spacer; }
	void execute() override {}
	QString getDisplayName() const override;
	obs_data_t *serialize() const override;

	int getWidth() const { return spacerWidth; }
	void setWidth(int w) { spacerWidth = w; }

private:
	int spacerWidth;
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

	// True for entries that are bar decoration rather than a button.
	bool isDecoration() const;

	// True when the button runs something of its own, as opposed to a group
	// that only opens and closes.
	bool hasAction() const;

	// Text shown for this button in the settings tree.
	QString displayText() const;

	// One-line summary of what the button does, for the settings tree.
	QString summaryText() const;

	// Deep copy, used so the editor can be cancelled without touching the
	// live configuration.
	std::shared_ptr<ButtonConfig> clone() const;
};
