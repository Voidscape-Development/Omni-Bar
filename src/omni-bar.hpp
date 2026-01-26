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

#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QMenu>
#include <QHash>
#include <obs-frontend-api.h>
#include <memory>

class ButtonConfig;
class QMainWindow;

class OmniBarButton : public QToolButton {
	Q_OBJECT

public:
	OmniBarButton(std::shared_ptr<ButtonConfig> config, QWidget *parent = nullptr);

	void updateState();
	std::shared_ptr<ButtonConfig> getConfig() const { return buttonConfig; }

private slots:
	void onClicked();

private:
	std::shared_ptr<ButtonConfig> buttonConfig;
};

class OmniBar : public QToolBar {
	Q_OBJECT

public:
	explicit OmniBar(QWidget *parent = nullptr);
	~OmniBar();

	static OmniBar *getInstance() { return instance; }

	void attachToMainWindow(QMainWindow *mainWindow);
	void repositionToolbar();
	void rebuildToolbar();
	void updateButtonStates();

public slots:
	void refreshFromConfiguration();

private slots:
	void onUpdateTimer();
	void onContextMenuRequested(const QPoint &pos);
	void onConfigureClicked();

private:
	static OmniBar *instance;
	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	void handleFrontendEvent(enum obs_frontend_event event);

	void clearButtons();
	void createButtonsFromConfig();
	QIcon getIconForButton(const std::shared_ptr<ButtonConfig> &config);
	void scheduleUpdate();

	QList<OmniBarButton *> buttons;
	QHash<QString, OmniBarButton *> buttonMap;
	QTimer *updateTimer;
	QMenu *contextMenu;
	QMainWindow *mainWindow = nullptr;
	bool updatePending = false;
	bool isRebuilding = false;
};
