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
#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPolygon>
#include <QMouseEvent>
#include <QBoxLayout>
#include <QScreen>

OmniBar *OmniBar::instance = nullptr;

// Resolve a plugin data file, returning an empty string when it is missing.
static QString moduleFilePath(const QString &relativePath)
{
	if (relativePath.isEmpty())
		return QString();

	char *dataPath = obs_module_file(relativePath.toUtf8().constData());
	if (!dataPath)
		return QString();

	QString fullPath = QString::fromUtf8(dataPath);
	bfree(dataPath);
	return QFile::exists(fullPath) ? fullPath : QString();
}

// Locate a bundled icon, preferring the variant matching the current theme.
static QString findBundledIcon(const QString &name)
{
	if (name.isEmpty())
		return QString();

	QStringList candidates;
	int dotIndex = name.lastIndexOf('.');
	if (dotIndex > 0 && !name.contains("_dark.") && !name.contains("_light.")) {
		QString suffix = omniBarIsDarkTheme() ? "_dark" : "_light";
		candidates << name.left(dotIndex) + suffix + name.mid(dotIndex);
	}
	candidates << name;

	for (const QString &candidate : candidates) {
		QString path = moduleFilePath(candidate);
		if (!path.isEmpty())
			return path;
		path = moduleFilePath(QString("icons/%1").arg(candidate));
		if (!path.isEmpty())
			return path;
	}

	return QString();
}

// Bundled icon name to fall back on when a button has none of its own.
static QString defaultIconName(const std::shared_ptr<ButtonConfig> &config)
{
	if (!config || !config->action)
		return QString();

	switch (config->action->getType()) {
	case ActionType::Frontend: {
		auto *frontendAction = dynamic_cast<FrontendAction *>(config->action.get());
		if (!frontendAction)
			return QString();

		switch (frontendAction->getActionType()) {
		case FrontendActionType::ToggleStreaming:
		case FrontendActionType::StartStreaming:
		case FrontendActionType::StopStreaming:
			return "stream.svg";
		case FrontendActionType::ToggleRecording:
		case FrontendActionType::StartRecording:
		case FrontendActionType::StopRecording:
			return "record.svg";
		case FrontendActionType::TogglePauseRecording:
		case FrontendActionType::PauseRecording:
		case FrontendActionType::UnpauseRecording:
			return "pause.svg";
		case FrontendActionType::ToggleReplayBuffer:
		case FrontendActionType::StartReplayBuffer:
		case FrontendActionType::StopReplayBuffer:
			return "replay.svg";
		case FrontendActionType::SaveReplayBuffer:
			return "save-replay.svg";
		case FrontendActionType::ToggleVirtualCam:
		case FrontendActionType::StartVirtualCam:
		case FrontendActionType::StopVirtualCam:
			return "virtual-cam.svg";
		case FrontendActionType::ToggleStudioMode:
		case FrontendActionType::EnableStudioMode:
		case FrontendActionType::DisableStudioMode:
		case FrontendActionType::TransitionToProgram:
			return "studio-mode.svg";
		}
		return QString();
	}
	case ActionType::SourceFilter:
		return "filter.svg";
	case ActionType::SourceVisibility:
		return "visibility.svg";
	case ActionType::SourceHotkey:
		return "hotkey.svg";
	case ActionType::Spacer:
		return "spacer.svg";
	case ActionType::None:
		return QString();
	}

	return QString();
}

QIcon omniBarIconForConfig(const std::shared_ptr<ButtonConfig> &config)
{
	if (!config)
		return QIcon();

	const QString &iconPath = config->iconPath;

	// A custom icon is stored as a filesystem path; bundled icons are stored
	// as a bare file name.
	if (!iconPath.isEmpty()) {
		if (iconPath.contains('/') || iconPath.contains('\\')) {
			if (QFile::exists(iconPath))
				return QIcon(iconPath);
		} else {
			QString bundled = findBundledIcon(iconPath);
			if (!bundled.isEmpty())
				return QIcon(bundled);
		}
	}

	QString fallback = findBundledIcon(defaultIconName(config));
	return fallback.isEmpty() ? QIcon() : QIcon(fallback);
}

void omniBarApplyButtonAppearance(QToolButton *button, const std::shared_ptr<ButtonConfig> &config,
				  const BarStyle &style)
{
	if (!button || !config)
		return;

	button->setIcon(omniBarIconForConfig(config));
	button->setIconSize(QSize(style.iconSize, style.iconSize));

	QString label = config->label;
	if (label.isEmpty())
		label = config->tooltip;
	if (label.isEmpty() && config->action)
		label = config->action->getDisplayName();
	button->setText(label);

	switch (config->displayMode) {
	case ButtonDisplayMode::IconOnly:
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		break;
	case ButtonDisplayMode::TextOnly:
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		break;
	case ButtonDisplayMode::TextBeside:
		button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		break;
	case ButtonDisplayMode::TextUnder:
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		break;
	}

	button->setToolTip(config->tooltip.isEmpty() ? label : config->tooltip);

	int extent = style.buttonExtent();
	if (config->displayMode == ButtonDisplayMode::IconOnly) {
		button->setFixedSize(extent, extent);
	} else {
		button->setMinimumSize(0, 0);
		button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		int minHeight = config->displayMode == ButtonDisplayMode::TextUnder
					? extent + button->fontMetrics().height()
					: extent;
		button->setMinimumHeight(minHeight);
		button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	}

	if (config->useCustomColor && config->customColor.isValid())
		button->setStyleSheet(style.buttonAccentStyleSheet(config->customColor));
	else
		button->setStyleSheet(QString());
}

// ============================================================================
// OmniBarButton
// ============================================================================

OmniBarButton::OmniBarButton(std::shared_ptr<ButtonConfig> config, QWidget *parent)
	: QToolButton(parent),
	  buttonConfig(config)
{
	setCheckable(true);
	setFocusPolicy(Qt::NoFocus);
	setToolButtonStyle(Qt::ToolButtonIconOnly);
	connect(this, &QToolButton::clicked, this, &OmniBarButton::onClicked);
	updateState();
}

void OmniBarButton::applyStyle(const BarStyle &style)
{
	omniBarApplyButtonAppearance(this, buttonConfig, style);
}

void OmniBarButton::setGroupIndicator(bool visible, bool interactive, bool splitTarget)
{
	showIndicator = visible;
	indicatorInteractive = interactive;
	indicatorIsSplit = splitTarget;
	update();
}

void OmniBarButton::setCollapsed(bool value)
{
	if (collapsed == value)
		return;
	collapsed = value;
	refreshVisibility();
}

void OmniBarButton::refreshVisibility()
{
	setVisible(actionValid && !collapsed);
}

void OmniBarButton::updateState()
{
	if (!buttonConfig)
		return;

	// Hide buttons whose target no longer exists (source deleted, etc).
	actionValid = buttonConfig->isValid();
	refreshVisibility();

	// Groups without an action of their own use the checked state to show
	// whether they are open, so leave it to the group logic.
	if (buttonConfig->hasAction())
		setChecked(buttonConfig->action->isActive());
}

void OmniBarButton::onClicked()
{
	if (buttonConfig && buttonConfig->hasAction()) {
		buttonConfig->action->execute();
	}
}

QRect OmniBarButton::indicatorRect() const
{
	int extent = qMax(7, qMin(width(), height()) / 4);
	return QRect(width() - extent - 2, height() - extent - 2, extent, extent);
}

void OmniBarButton::paintEvent(QPaintEvent *event)
{
	QToolButton::paintEvent(event);

	if (!showIndicator)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	QRect rect = indicatorRect();
	QColor color = palette().color(QPalette::ButtonText);
	color.setAlpha(isChecked() ? 235 : 170);

	painter.setPen(Qt::NoPen);
	painter.setBrush(color);

	QPolygon chevron;
	chevron << QPoint(rect.left(), rect.top() + rect.height() / 3)
		<< QPoint(rect.right(), rect.top() + rect.height() / 3) << QPoint(rect.center().x(), rect.bottom());
	painter.drawPolygon(chevron);

	if (indicatorInteractive && indicatorIsSplit) {
		// Mark the corner off so it reads as its own hit target.
		QColor line = color;
		line.setAlpha(90);
		painter.setPen(line);
		painter.drawLine(rect.left() - 3, height() - 2, rect.left() - 3, height() - rect.height() - 4);
	}
}

void OmniBarButton::mousePressEvent(QMouseEvent *event)
{
	if (showIndicator && indicatorInteractive && event->button() == Qt::LeftButton) {
		// A group with its own action only expands from the chevron
		// corner; a group that does nothing else expands from anywhere.
		bool hitExpand = !indicatorIsSplit || indicatorRect().adjusted(-4, -4, 2, 2).contains(event->pos());
		if (hitExpand) {
			emit expandRequested();
			event->accept();
			return;
		}
	}

	QToolButton::mousePressEvent(event);
}

void OmniBarButton::enterEvent(QEnterEvent *event)
{
	QToolButton::enterEvent(event);
	emit hoverEntered();
}

void OmniBarButton::leaveEvent(QEvent *event)
{
	QToolButton::leaveEvent(event);
	emit hoverLeft();
}

// ============================================================================
// OmniBarFlyout
// ============================================================================

OmniBarFlyout::OmniBarFlyout(Qt::Orientation orientation, bool grabInput, QWidget *parent)
	: QWidget(parent, grabInput ? Qt::Popup
				    : (Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
				       Qt::WindowDoesNotAcceptFocus))
{
	setObjectName("OmniBarFlyout");
	setAttribute(Qt::WA_ShowWithoutActivating);

	contentLayout =
		new QBoxLayout(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom, this);
	contentLayout->setContentsMargins(6, 6, 6, 6);
	contentLayout->setSpacing(4);
}

void OmniBarFlyout::addButton(OmniBarButton *button)
{
	if (button)
		contentLayout->addWidget(button);
}

void OmniBarFlyout::applyStyle(const BarStyle &style)
{
	contentLayout->setSpacing(style.spacing);
	int margin = qMax(4, style.spacing);
	contentLayout->setContentsMargins(margin, margin, margin, margin);
	setStyleSheet(style.flyoutStyleSheet());
}

void OmniBarFlyout::showNear(QWidget *anchor, Qt::ToolBarArea area)
{
	if (!anchor)
		return;

	adjustSize();
	QSize panelSize = sizeHint();
	QPoint position;

	switch (area) {
	case Qt::BottomToolBarArea:
		position = anchor->mapToGlobal(QPoint(0, -panelSize.height() - 2));
		break;
	case Qt::LeftToolBarArea:
		position = anchor->mapToGlobal(QPoint(anchor->width() + 2, 0));
		break;
	case Qt::RightToolBarArea:
		position = anchor->mapToGlobal(QPoint(-panelSize.width() - 2, 0));
		break;
	case Qt::TopToolBarArea:
	default:
		position = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
		break;
	}

	if (QScreen *screen = anchor->screen()) {
		QRect available = screen->availableGeometry();
		position.setX(qBound(available.left(), position.x(), available.right() - panelSize.width()));
		position.setY(qBound(available.top(), position.y(), available.bottom() - panelSize.height()));
	}

	move(position);
	show();
	raise();
}

void OmniBarFlyout::enterEvent(QEnterEvent *event)
{
	QWidget::enterEvent(event);
	emit hoverEntered();
}

void OmniBarFlyout::leaveEvent(QEvent *event)
{
	QWidget::leaveEvent(event);
	emit hoverLeft();
}

void OmniBarFlyout::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	emit dismissed();
}

// ============================================================================
// OmniBar
// ============================================================================

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
	clearButtons();
	instance = nullptr;
}

void OmniBar::attachToMainWindow(QMainWindow *window)
{
	if (!window)
		return;

	mainWindow = window;
	repositionToolbar();
}

Qt::ToolBarArea OmniBar::currentArea() const
{
	if (mainWindow) {
		Qt::ToolBarArea area = mainWindow->toolBarArea(const_cast<OmniBar *>(this));
		if (area != Qt::NoToolBarArea)
			return area;
	}

	switch (SettingsManager::getDockPosition()) {
	case DockPosition::Left:
		return Qt::LeftToolBarArea;
	case DockPosition::Bottom:
		return Qt::BottomToolBarArea;
	case DockPosition::Right:
		return Qt::RightToolBarArea;
	case DockPosition::Top:
	default:
		return Qt::TopToolBarArea;
	}
}

void OmniBar::repositionToolbar()
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

	// Remove from current position and add to new position
	mainWindow->removeToolBar(this);
	mainWindow->addToolBar(area, this);
	show();

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
	for (auto &group : groups) {
		// Drop every connection first: the handlers capture the group,
		// which is about to be destroyed.
		if (group->parentButton)
			group->parentButton->disconnect(this);
		for (auto *child : group->childButtons)
			child->disconnect(this);

		if (group->hoverTimer) {
			group->hoverTimer->stop();
			group->hoverTimer->disconnect();
			delete group->hoverTimer;
			group->hoverTimer = nullptr;
		}

		if (group->flyout) {
			group->flyout->disconnect();
			group->flyout->hide();
			// Deletes the child buttons it holds.
			delete group->flyout.data();
		}
	}
	groups.clear();

	buttons.clear();
	activeConfigs.clear();

	// QToolBar::clear() only detaches the actions, so destroy them here;
	// each widget action owns the widget that was added with it.
	for (QAction *action : barActions) {
		removeAction(action);
		delete action;
	}
	barActions.clear();
	clear();
}

OmniBarButton *OmniBar::createButton(const std::shared_ptr<ButtonConfig> &config)
{
	auto *button = new OmniBarButton(config, this);
	button->applyStyle(SettingsManager::getStyle());
	button->updateState();
	connect(button, &QToolButton::clicked, this, &OmniBar::scheduleUpdate);
	return button;
}

void OmniBar::createButtonsFromConfig()
{
	applyStyleToBar();

	activeConfigs = SettingsManager::getButtons();

	for (const auto &config : activeConfigs) {
		if (!config || !config->action)
			continue;

		if (config->isSpacer()) {
			auto *spacerAction = dynamic_cast<SpacerAction *>(config->action.get());
			int spacerSize = spacerAction ? spacerAction->getWidth() : 10;

			QWidget *spacer = new QWidget(this);
			if (orientation() == Qt::Horizontal) {
				spacer->setFixedWidth(spacerSize);
			} else {
				spacer->setFixedHeight(spacerSize);
			}
			barActions.append(addWidget(spacer));
			continue;
		}

		OmniBarButton *button = createButton(config);
		barActions.append(addWidget(button));
		buttons.append(button);

		if (!config->isGroup || config->children.isEmpty())
			continue;

		auto group = std::make_unique<GroupRuntime>();
		group->config = config;
		group->parentButton = button;
		buildGroup(group.get());
		connectGroupTriggers(group.get());
		groups.push_back(std::move(group));
	}

	updateButtonStates();
}

void OmniBar::buildGroup(GroupRuntime *group)
{
	const BarStyle &style = SettingsManager::getStyle();
	const auto &config = group->config;
	bool isInline = config->groupDisplay == GroupDisplayMode::Inline;

	if (!isInline) {
		// A click-driven flyout grabs input so clicking elsewhere closes
		// it; the automatic modes must not steal the mouse from OBS.
		bool grabInput = config->groupExpand == GroupExpandMode::Click;
		group->flyout = new OmniBarFlyout(orientation(), grabInput, this);
		group->flyout->applyStyle(style);
	}

	for (const auto &childConfig : config->children) {
		if (!childConfig || !childConfig->action)
			continue;

		OmniBarButton *child = createButton(childConfig);
		group->childButtons.append(child);
		buttons.append(child);

		if (isInline) {
			QAction *action = addWidget(child);
			action->setVisible(false);
			child->setCollapsed(true);
			group->childActions.append(action);
			barActions.append(action);
		} else {
			group->flyout->addButton(child);
		}
	}
}

void OmniBar::connectGroupTriggers(GroupRuntime *group)
{
	GroupRuntime *g = group;

	if (g->flyout) {
		connect(g->flyout.data(), &OmniBarFlyout::dismissed, this, [g]() {
			g->expanded = false;
			g->flyoutClosedAt.restart();
			if (!g->config->hasAction() && g->parentButton)
				g->parentButton->setChecked(false);
		});
	}

	switch (g->config->groupExpand) {
	case GroupExpandMode::Click:
		// The chevron is the expand target; when the group also runs an
		// action the rest of the button keeps triggering it.
		g->parentButton->setGroupIndicator(true, true, g->config->hasAction());
		connect(g->parentButton, &OmniBarButton::expandRequested, this, [this, g]() {
			// A popup flyout closes on the very click that lands on
			// the parent, so ignore the reopen that would follow.
			if (!g->expanded && g->flyoutClosedAt.isValid() && g->flyoutClosedAt.elapsed() < 150)
				return;
			setGroupExpanded(g, !g->expanded);
		});
		break;

	case GroupExpandMode::ParentActive:
		// Expansion follows the parent's action state, so the chevron is
		// only a hint and clicks stay with the action.
		g->parentButton->setGroupIndicator(true, false, false);
		break;

	case GroupExpandMode::Hover:
		g->parentButton->setGroupIndicator(true, false, false);

		g->hoverTimer = new QTimer(this);
		g->hoverTimer->setSingleShot(true);
		g->hoverTimer->setInterval(350);
		connect(g->hoverTimer, &QTimer::timeout, this, [this, g]() { setGroupExpanded(g, false); });

		connect(g->parentButton, &OmniBarButton::hoverEntered, this, [this, g]() {
			cancelHoverCollapse(g);
			setGroupExpanded(g, true);
		});
		connect(g->parentButton, &OmniBarButton::hoverLeft, this, [this, g]() { startHoverCollapse(g); });

		for (auto *child : g->childButtons) {
			connect(child, &OmniBarButton::hoverEntered, this, [this, g]() { cancelHoverCollapse(g); });
			connect(child, &OmniBarButton::hoverLeft, this, [this, g]() { startHoverCollapse(g); });
		}

		if (g->flyout) {
			connect(g->flyout.data(), &OmniBarFlyout::hoverEntered, this,
				[this, g]() { cancelHoverCollapse(g); });
			connect(g->flyout.data(), &OmniBarFlyout::hoverLeft, this,
				[this, g]() { startHoverCollapse(g); });
		}
		break;
	}
}

void OmniBar::setGroupExpanded(GroupRuntime *group, bool expanded)
{
	if (!group || !group->parentButton)
		return;
	if (group->expanded == expanded)
		return;

	group->expanded = expanded;

	if (group->config->groupDisplay == GroupDisplayMode::Inline) {
		// Both flags are needed: the action drives the toolbar layout,
		// the widget flag stops a state refresh re-showing the child.
		for (int i = 0; i < group->childActions.size(); i++) {
			group->childActions[i]->setVisible(expanded);
			if (i < group->childButtons.size())
				group->childButtons[i]->setCollapsed(!expanded);
		}
	} else if (group->flyout) {
		if (expanded) {
			group->flyout->applyStyle(SettingsManager::getStyle());
			group->flyout->showNear(group->parentButton, currentArea());
		} else {
			group->flyout->hide();
		}
	}

	if (!group->config->hasAction())
		group->parentButton->setChecked(expanded);
}

void OmniBar::startHoverCollapse(GroupRuntime *group)
{
	if (group && group->hoverTimer)
		group->hoverTimer->start();
}

void OmniBar::cancelHoverCollapse(GroupRuntime *group)
{
	if (group && group->hoverTimer)
		group->hoverTimer->stop();
}

void OmniBar::applyStyleToBar()
{
	const BarStyle &style = SettingsManager::getStyle();
	setIconSize(QSize(style.iconSize, style.iconSize));
	setStyleSheet(style.barStyleSheet());
}

void OmniBar::updateButtonStates()
{
	for (auto *button : buttons) {
		button->updateState();
	}

	for (auto &group : groups) {
		if (group->config->groupExpand != GroupExpandMode::ParentActive)
			continue;

		bool active = group->config->hasAction() && group->config->action->isActive();
		setGroupExpanded(group.get(), active);
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
	repositionToolbar();
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
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		scheduleUpdate();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// Sources the buttons point at have been swapped out wholesale.
		rebuildToolbar();
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		// Themed icons and palette-derived colours both need redoing.
		rebuildToolbar();
		break;
	default:
		break;
	}
}
