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
#include <QPointer>
#include <QElapsedTimer>
#include <obs-frontend-api.h>
#include <memory>
#include <vector>

#include "bar-style.hpp"

class ButtonConfig;
class QMainWindow;
class QBoxLayout;

// Icon for a button, recoloured to tint and rendered at size, falling back to a
// sensible default for the button's action type.
QIcon omniBarIconForConfig(const std::shared_ptr<ButtonConfig> &config, const QColor &tint, int size);

// Apply a config's label, display mode, icon and colour override to a tool
// button. Shared with the settings dialog so its preview matches the real bar.
void omniBarApplyButtonAppearance(QToolButton *button, const std::shared_ptr<ButtonConfig> &config,
				  const BarStyle &style);

class OmniBarButton : public QToolButton {
	Q_OBJECT

public:
	OmniBarButton(std::shared_ptr<ButtonConfig> config, QWidget *parent = nullptr);

	void applyStyle(const BarStyle &style);
	void updateState();

	std::shared_ptr<ButtonConfig> getConfig() const { return buttonConfig; }

	// Draw a chevron marking this button as a group. When interactive is set
	// a click emits expandRequested instead of running the action; adding
	// splitTarget narrows that to the chevron corner so the rest of the
	// button still runs the group's own action.
	void setGroupIndicator(bool visible, bool interactive, bool splitTarget);

	// Set by the owning group when it collapses. Kept separate from action
	// validity so a state refresh can never re-show a collapsed child.
	void setCollapsed(bool collapsed);

	// Previews render the button whether or not its target currently exists.
	void setPreviewMode(bool enabled);

signals:
	void expandRequested();
	void hoverEntered();
	void hoverLeft();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private slots:
	void onClicked();

private:
	QRect indicatorRect() const;
	void refreshVisibility();
	void paintLabelAbove();

	std::shared_ptr<ButtonConfig> buttonConfig;
	bool showIndicator = false;
	bool indicatorInteractive = false;
	bool indicatorIsSplit = false;
	bool collapsed = false;
	bool actionValid = true;
	bool previewMode = false;
};

// A line across the bar, separating neighbouring buttons.
class OmniBarDivider : public QWidget {
	Q_OBJECT

public:
	OmniBarDivider(std::shared_ptr<ButtonConfig> config, Qt::Orientation barOrientation, const BarStyle &style,
		       QWidget *parent = nullptr);

	void applyStyle(Qt::Orientation barOrientation, const BarStyle &style);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	std::shared_ptr<ButtonConfig> dividerConfig;
	Qt::Orientation barOrientation = Qt::Horizontal;
	QColor lineColor;
};

// Floating panel holding a group's children.
class OmniBarFlyout : public QWidget {
	Q_OBJECT

public:
	OmniBarFlyout(Qt::Orientation orientation, bool grabInput, QWidget *parent = nullptr);

	void addButton(OmniBarButton *button);
	void applyStyle(const BarStyle &style);
	void showNear(QWidget *anchor, Qt::ToolBarArea area);

signals:
	void hoverEntered();
	void hoverLeft();
	void dismissed();

protected:
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	QBoxLayout *contentLayout;
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
	void onOrientationChanged(Qt::Orientation orientation);

private:
	struct GroupRuntime {
		std::shared_ptr<ButtonConfig> config;
		OmniBarButton *parentButton = nullptr;
		QList<OmniBarButton *> childButtons;
		// Inline groups collapse by hiding the toolbar actions rather
		// than the widgets, so button validity and group collapse never
		// fight over the same visibility flag.
		QList<QAction *> childActions;
		QPointer<OmniBarFlyout> flyout;
		QTimer *hoverTimer = nullptr;
		QElapsedTimer flyoutClosedAt;
		bool expanded = false;
	};

	static OmniBar *instance;
	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	void handleFrontendEvent(enum obs_frontend_event event);

	void clearButtons();
	void createButtonsFromConfig();
	OmniBarButton *createButton(const std::shared_ptr<ButtonConfig> &config);
	void buildGroup(GroupRuntime *group);
	void connectGroupTriggers(GroupRuntime *group);
	void setGroupExpanded(GroupRuntime *group, bool expanded);
	void startHoverCollapse(GroupRuntime *group);
	void cancelHoverCollapse(GroupRuntime *group);
	void applyStyleToBar();
	Qt::ToolBarArea currentArea() const;
	void scheduleUpdate();

	QList<OmniBarButton *> buttons;
	std::vector<std::unique_ptr<GroupRuntime>> groups;
	QList<std::shared_ptr<ButtonConfig>> activeConfigs;
	// QToolBar::clear() only detaches actions, so the widget actions this bar
	// creates are tracked and destroyed explicitly on rebuild.
	QList<QAction *> barActions;
	QTimer *updateTimer;
	QMenu *contextMenu;
	QMainWindow *mainWindow = nullptr;
	bool updatePending = false;
	bool isRebuilding = false;
};
