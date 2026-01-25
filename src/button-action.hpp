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
#include <memory>
#include <QList>

// Forward declaration
class ButtonConfig;

enum class ActionType {
	Frontend,
	SourceHotkey,
	SourceFilter,
	SourceVisibility,
	Spacer
};

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
	QString getDisplayName() const override { return "Spacer"; }
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
	QString tooltip;
	QString iconPath;
	std::unique_ptr<ButtonAction> action;
	bool expandable = false;
	bool expandWhenActive = false;
	QList<std::shared_ptr<ButtonConfig>> children;

	obs_data_t *serialize() const;
	static std::shared_ptr<ButtonConfig> deserialize(obs_data_t *data);

	bool isValid() const;
};
