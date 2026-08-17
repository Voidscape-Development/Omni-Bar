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

enum class ActionType { None, Frontend, SourceHotkey, SourceFilter, SourceVisibility, Spacer };

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

// How a button renders its label alongside its icon.
enum class ButtonDisplayMode { IconOnly = 0, TextOnly = 1, TextBeside = 2, TextUnder = 3 };

// How a group presents its children.
enum class GroupDisplayMode { Flyout = 0, Inline = 1 };

// What causes a group to expand.
enum class GroupExpandMode { Click = 0, ParentActive = 1, Hover = 2 };

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

	static QString getFrontendActionName(FrontendActionType type);
	static FrontendActionType getFrontendActionFromName(const QString &name);

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

	void refreshHotkeyId();
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

	// Per-button accent override for the hover/active states.
	bool useCustomColor = false;
	QColor customColor;

	// Group settings. Children are limited to one level deep: a child is
	// never itself a group.
	bool isGroup = false;
	GroupDisplayMode groupDisplay = GroupDisplayMode::Flyout;
	GroupExpandMode groupExpand = GroupExpandMode::Click;
	QList<std::shared_ptr<ButtonConfig>> children;

	obs_data_t *serialize() const;
	static std::shared_ptr<ButtonConfig> deserialize(obs_data_t *data);

	bool isValid() const;
	bool isSpacer() const;

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
