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

#include "omni-bar.hpp"
#include "omni-bar-config.hpp"
#include "button-action.hpp"
#include "settings-manager.hpp"
#include <obs-module.h>
#include <QMainWindow>
#include <QAction>
#include <QFile>
#include <QDir>

OmniBar *OmniBar::instance = nullptr;

// OmniBarButton implementation
OmniBarButton::OmniBarButton(std::shared_ptr<ButtonConfig> config, QWidget *parent)
	: QToolButton(parent), buttonConfig(config)
{
	setCheckable(true);
	setToolButtonStyle(Qt::ToolButtonIconOnly);
	connect(this, &QToolButton::clicked, this, &OmniBarButton::onClicked);
	updateState();
}

void OmniBarButton::updateState()
{
	if (!buttonConfig || !buttonConfig->action)
		return;

	setToolTip(buttonConfig->tooltip);
	setChecked(buttonConfig->action->isActive());

	// Hide button if action is invalid (source deleted, etc.)
	bool valid = buttonConfig->action->isValid();
	setVisible(valid);
}

void OmniBarButton::onClicked()
{
	if (buttonConfig && buttonConfig->action) {
		buttonConfig->action->execute();
	}
}

// OmniBar implementation
OmniBar::OmniBar(QWidget *parent) : QToolBar(parent)
{
	instance = this;

	setObjectName("OmniBar");
	setMovable(false);
	setFloatable(false);

	// Context menu
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &OmniBar::onContextMenuRequested);

	contextMenu = new QMenu(this);
	QAction *configureAction = contextMenu->addAction(obs_module_text("OmniBar.Settings.Configure"));
	connect(configureAction, &QAction::triggered, this, &OmniBar::onConfigureClicked);

	// Batched update timer
	updateTimer = new QTimer(this);
	updateTimer->setSingleShot(true);
	updateTimer->setInterval(50);
	connect(updateTimer, &QTimer::timeout, this, &OmniBar::onUpdateTimer);

	// Register frontend event callback
	obs_frontend_add_event_callback(onFrontendEvent, this);

	// Build initial toolbar
	rebuildToolbar();
}

OmniBar::~OmniBar()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);
	instance = nullptr;
}

void OmniBar::attachToMainWindow(QMainWindow *mainWindow)
{
	if (!mainWindow)
		return;

	Qt::ToolBarArea area;
	switch (SettingsManager::getDockPosition()) {
	case DockPosition::Top:
		area = Qt::TopToolBarArea;
		break;
	case DockPosition::Left:
		area = Qt::LeftToolBarArea;
		break;
	case DockPosition::Bottom:
		area = Qt::BottomToolBarArea;
		break;
	case DockPosition::Right:
		area = Qt::RightToolBarArea;
		break;
	default:
		area = Qt::TopToolBarArea;
	}

	mainWindow->addToolBar(area, this);

	// Set orientation based on position
	if (area == Qt::LeftToolBarArea || area == Qt::RightToolBarArea) {
		setOrientation(Qt::Vertical);
	} else {
		setOrientation(Qt::Horizontal);
	}
}

void OmniBar::rebuildToolbar()
{
	if (isRebuilding)
		return;

	isRebuilding = true;
	clearButtons();
	createButtonsFromConfig();
	isRebuilding = false;
}

void OmniBar::clearButtons()
{
	for (auto *button : buttons) {
		removeAction(button->defaultAction());
		button->deleteLater();
	}
	buttons.clear();
	buttonMap.clear();
	clear();
}

void OmniBar::createButtonsFromConfig()
{
	int iconSize = SettingsManager::getIconSize();
	int padding = SettingsManager::getButtonPadding();

	setIconSize(QSize(iconSize, iconSize));
	setStyleSheet(QString("QToolBar { spacing: %1px; }").arg(padding));

	auto buttonConfigs = SettingsManager::getButtons();

	for (const auto &config : buttonConfigs) {
		if (!config || !config->action)
			continue;

		// Handle spacer
		if (config->action->getType() == ActionType::Spacer) {
			auto *spacerAction = dynamic_cast<SpacerAction *>(config->action.get());
			if (spacerAction) {
				QWidget *spacer = new QWidget(this);
				int spacerSize = spacerAction->getWidth();
				if (orientation() == Qt::Horizontal) {
					spacer->setFixedWidth(spacerSize);
				} else {
					spacer->setFixedHeight(spacerSize);
				}
				addWidget(spacer);
			}
			continue;
		}

		// Create button
		auto *button = new OmniBarButton(config, this);
		button->setIcon(getIconForButton(config));
		button->setIconSize(QSize(iconSize, iconSize));

		int buttonSize = iconSize + 8;
		button->setFixedSize(buttonSize, buttonSize);

		addWidget(button);
		buttons.append(button);
		buttonMap[config->id] = button;

		// Handle expandable buttons with children
		if (config->expandable && !config->children.isEmpty()) {
			for (const auto &childConfig : config->children) {
				auto *childButton = new OmniBarButton(childConfig, this);
				childButton->setIcon(getIconForButton(childConfig));
				childButton->setIconSize(QSize(iconSize, iconSize));
				childButton->setFixedSize(buttonSize, buttonSize);

				// Initially hidden, shown when parent is active
				childButton->setVisible(config->expandWhenActive && config->action->isActive());

				addWidget(childButton);
				buttons.append(childButton);
				buttonMap[childConfig->id] = childButton;
			}
		}
	}
}

QIcon OmniBar::getIconForButton(const std::shared_ptr<ButtonConfig> &config)
{
	if (!config)
		return QIcon();

	QString iconPath = config->iconPath;

	// Check if it's a built-in icon
	if (!iconPath.contains('/') && !iconPath.contains('\\')) {
		// Try to load from plugin data directory
		char *dataPath = obs_module_file(iconPath.toUtf8().constData());
		if (dataPath) {
			QString fullPath = QString::fromUtf8(dataPath);
			bfree(dataPath);
			if (QFile::exists(fullPath)) {
				return QIcon(fullPath);
			}
		}

		// Try icons subdirectory
		QString iconsPath = QString("icons/%1").arg(iconPath);
		dataPath = obs_module_file(iconsPath.toUtf8().constData());
		if (dataPath) {
			QString fullPath = QString::fromUtf8(dataPath);
			bfree(dataPath);
			if (QFile::exists(fullPath)) {
				return QIcon(fullPath);
			}
		}
	}

	// Try as absolute or relative path
	if (QFile::exists(iconPath)) {
		return QIcon(iconPath);
	}

	// Fallback to default icon based on action type
	if (config->action) {
		QString defaultIcon;
		switch (config->action->getType()) {
		case ActionType::Frontend: {
			auto *frontendAction = dynamic_cast<FrontendAction *>(config->action.get());
			if (frontendAction) {
				switch (frontendAction->getActionType()) {
				case FrontendActionType::ToggleStreaming:
				case FrontendActionType::StartStreaming:
				case FrontendActionType::StopStreaming:
					defaultIcon = "icons/stream.svg";
					break;
				case FrontendActionType::ToggleRecording:
				case FrontendActionType::StartRecording:
				case FrontendActionType::StopRecording:
					defaultIcon = "icons/record.svg";
					break;
				case FrontendActionType::TogglePauseRecording:
				case FrontendActionType::PauseRecording:
				case FrontendActionType::UnpauseRecording:
					defaultIcon = "icons/pause.svg";
					break;
				case FrontendActionType::ToggleReplayBuffer:
				case FrontendActionType::StartReplayBuffer:
				case FrontendActionType::StopReplayBuffer:
					defaultIcon = "icons/replay.svg";
					break;
				case FrontendActionType::SaveReplayBuffer:
					defaultIcon = "icons/save-replay.svg";
					break;
				case FrontendActionType::ToggleVirtualCam:
				case FrontendActionType::StartVirtualCam:
				case FrontendActionType::StopVirtualCam:
					defaultIcon = "icons/virtual-cam.svg";
					break;
				case FrontendActionType::ToggleStudioMode:
				case FrontendActionType::EnableStudioMode:
				case FrontendActionType::DisableStudioMode:
				case FrontendActionType::TransitionToProgram:
					defaultIcon = "icons/studio-mode.svg";
					break;
				}
			}
			break;
		}
		case ActionType::SourceFilter:
			defaultIcon = "icons/filter.svg";
			break;
		case ActionType::SourceVisibility:
			defaultIcon = "icons/visibility.svg";
			break;
		case ActionType::SourceHotkey:
			defaultIcon = "icons/hotkey.svg";
			break;
		default:
			break;
		}

		if (!defaultIcon.isEmpty()) {
			char *dataPath = obs_module_file(defaultIcon.toUtf8().constData());
			if (dataPath) {
				QString fullPath = QString::fromUtf8(dataPath);
				bfree(dataPath);
				if (QFile::exists(fullPath)) {
					return QIcon(fullPath);
				}
			}
		}
	}

	return QIcon();
}

void OmniBar::updateButtonStates()
{
	for (auto *button : buttons) {
		button->updateState();
	}

	// Update expandable button children visibility
	auto buttonConfigs = SettingsManager::getButtons();
	for (const auto &config : buttonConfigs) {
		if (config->expandable && !config->children.isEmpty() && config->action) {
			bool parentActive = config->action->isActive();
			for (const auto &childConfig : config->children) {
				if (buttonMap.contains(childConfig->id)) {
					buttonMap[childConfig->id]->setVisible(config->expandWhenActive && parentActive);
				}
			}
		}
	}
}

void OmniBar::scheduleUpdate()
{
	if (!updatePending) {
		updatePending = true;
		updateTimer->start();
	}
}

void OmniBar::onUpdateTimer()
{
	updatePending = false;
	updateButtonStates();
}

void OmniBar::refreshFromConfiguration()
{
	rebuildToolbar();
}

void OmniBar::onContextMenuRequested(const QPoint &pos)
{
	contextMenu->exec(mapToGlobal(pos));
}

void OmniBar::onConfigureClicked()
{
	OmniBarConfig::showDialog();
}

void OmniBar::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	OmniBar *bar = static_cast<OmniBar *>(data);
	if (bar) {
		bar->handleFrontendEvent(event);
	}
}

void OmniBar::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		scheduleUpdate();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		// Rebuild toolbar when scene collection changes
		rebuildToolbar();
		break;
	default:
		break;
	}
}
