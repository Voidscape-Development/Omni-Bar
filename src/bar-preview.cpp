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

#include "bar-preview.hpp"
#include "button-action.hpp"
#include "omni-bar.hpp"

#include <obs-module.h>
#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

// Outline drawn around the selected entry, on top of the bar's own widgets.
class OmniBarPreviewOverlay : public QWidget {
public:
	OmniBarPreviewOverlay(OmniBarPreview *owner, QWidget *parent) : QWidget(parent), preview(owner)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		setAttribute(Qt::WA_NoSystemBackground, true);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QWidget *target = preview->widgetsById.value(preview->selectedId);
		if (!target || !target->isVisible())
			return;

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QPen pen(palette().color(QPalette::Highlight), 2.0, Qt::DashLine);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		QRectF area = QRectF(target->geometry()).adjusted(-3, -3, 3, 3);
		painter.drawRoundedRect(area, 5, 5);
	}

private:
	OmniBarPreview *preview;
};

static QPushButton *segmentButton(const char *textKey, QButtonGroup *group, int id)
{
	QPushButton *button = new QPushButton(obs_module_text(textKey));
	button->setCheckable(true);
	// Inside a dialog every push button would otherwise put itself forward
	// as the default, and Enter in the inspector would press it.
	button->setAutoDefault(false);
	button->setFocusPolicy(Qt::TabFocus);
	group->addButton(button, id);
	return button;
}

OmniBarPreview::OmniBarPreview(QWidget *parent) : QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	// Header: what the preview is showing, and how. The chosen option of each
	// row is marked in the highlight colour, since a checked push button is
	// easy to miss in some themes.
	QWidget *headerWidget = new QWidget();
	headerWidget->setStyleSheet(QStringLiteral(
		"QPushButton:checked { background-color: palette(highlight); color: palette(highlighted-text); }"));
	QHBoxLayout *header = new QHBoxLayout(headerWidget);
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(4);
	header->addWidget(new QLabel(obs_module_text("OmniBar.Preview.Title")));
	header->addStretch();

	header->addWidget(new QLabel(obs_module_text("OmniBar.Preview.As")));
	stateGroup = new QButtonGroup(this);
	stateGroup->setExclusive(true);
	const std::pair<PreviewAs, const char *> states[] = {{PreviewAs::Now, "OmniBar.Preview.Now"},
							     {PreviewAs::Idle, "OmniBar.Preview.Idle"},
							     {PreviewAs::Live, "OmniBar.Preview.Live"},
							     {PreviewAs::Recording, "OmniBar.Preview.Recording"},
							     {PreviewAs::LiveRecording,
							      "OmniBar.Preview.LiveRecording"},
							     {PreviewAs::Paused, "OmniBar.Preview.Paused"}};
	for (const auto &state : states)
		header->addWidget(segmentButton(state.second, stateGroup, static_cast<int>(state.first)));
	stateGroup->button(static_cast<int>(PreviewAs::LiveRecording))->setChecked(true);

	// The outputs that run alongside streaming and recording, any of which
	// can be on in any state.
	alsoRunningButton = new QToolButton();
	alsoRunningButton->setText(obs_module_text("OmniBar.Preview.AlsoRunning"));
	alsoRunningButton->setPopupMode(QToolButton::InstantPopup);
	QMenu *alsoMenu = new QMenu(alsoRunningButton);
	replayAction = alsoMenu->addAction(obs_module_text("OmniBar.Condition.ReplayBuffer"));
	virtualCamAction = alsoMenu->addAction(obs_module_text("OmniBar.Condition.VirtualCam"));
	studioAction = alsoMenu->addAction(obs_module_text("OmniBar.Condition.StudioMode"));
	for (QAction *action : {replayAction, virtualCamAction, studioAction})
		action->setCheckable(true);
	// Checked before anything listens, since the drawing does not exist yet.
	replayAction->setChecked(true);
	for (QAction *action : {replayAction, virtualCamAction, studioAction})
		connect(action, &QAction::toggled, this, &OmniBarPreview::refresh);
	alsoRunningButton->setMenu(alsoMenu);
	header->addWidget(alsoRunningButton);

	header->addSpacing(12);
	orientationGroup = new QButtonGroup(this);
	orientationGroup->setExclusive(true);
	header->addWidget(segmentButton("OmniBar.Preview.Horizontal", orientationGroup, Qt::Horizontal));
	header->addWidget(segmentButton("OmniBar.Preview.Vertical", orientationGroup, Qt::Vertical));
	orientationGroup->button(Qt::Horizontal)->setChecked(true);

	layout->addWidget(headerWidget);

	connect(stateGroup, &QButtonGroup::idClicked, this, [this](int id) {
		// Following OBS leaves the other outputs to OBS as well.
		alsoRunningButton->setEnabled(static_cast<PreviewAs>(id) != PreviewAs::Now);
		refresh();
	});
	connect(orientationGroup, &QButtonGroup::idClicked, this,
		[this](int id) { setOrientation(static_cast<Qt::Orientation>(id)); });

	// The drawing itself, scrolling when the bar is longer than the window.
	scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	canvas = new QWidget();
	canvas->setObjectName("OmniBarPreviewBackdrop");
	canvasLayout = new QBoxLayout(QBoxLayout::LeftToRight, canvas);
	canvasLayout->setContentsMargins(10, 10, 10, 10);
	canvas->installEventFilter(this);
	scroll->setWidget(canvas);

	overlay = new OmniBarPreviewOverlay(this, canvas);
	layout->addWidget(scroll);

	followTimer = new QTimer(this);
	followTimer->setInterval(1000);
	connect(followTimer, &QTimer::timeout, this, [this]() {
		if (static_cast<PreviewAs>(stateGroup->checkedId()) != PreviewAs::Now)
			return;
		ObsStateSnapshot now = ObsStateSnapshot::current();
		if (now != lastFollowed)
			refresh();
	});
	followTimer->start();
}

void OmniBarPreview::setEntries(const QList<std::shared_ptr<ButtonConfig>> &newEntries)
{
	entries = newEntries;
	refresh();
}

void OmniBarPreview::setBarStyle(const BarStyle &newStyle)
{
	style = newStyle;
	refresh();
}

void OmniBarPreview::setSelectedId(const QString &id)
{
	selectedId = id;
	updateOverlay();
	if (QWidget *target = widgetsById.value(id))
		scroll->ensureWidgetVisible(target);
}

void OmniBarPreview::setOrientation(Qt::Orientation value)
{
	orientation = value;
	if (QAbstractButton *button = orientationGroup->button(value))
		button->setChecked(true);
	refresh();
}

ObsStateSnapshot OmniBarPreview::previewState() const
{
	auto chosen = static_cast<PreviewAs>(stateGroup->checkedId());
	if (chosen == PreviewAs::Now)
		return ObsStateSnapshot::current();

	ObsStateSnapshot state;
	state.streaming = chosen == PreviewAs::Live || chosen == PreviewAs::LiveRecording;
	state.recording = chosen == PreviewAs::Recording || chosen == PreviewAs::LiveRecording ||
			  chosen == PreviewAs::Paused;
	state.recordingPaused = chosen == PreviewAs::Paused;
	state.replayBuffer = replayAction->isChecked();
	state.virtualCam = virtualCamAction->isChecked();
	state.studioMode = studioAction->isChecked();
	return state;
}

void OmniBarPreview::clearCanvas()
{
	widgetsById.clear();
	while (QLayoutItem *item = canvasLayout->takeAt(0)) {
		if (QWidget *widget = item->widget())
			delete widget;
		delete item;
	}
}

void OmniBarPreview::refresh()
{
	ObsStateSnapshot state = previewState();
	lastFollowed = state;

	clearCanvas();
	canvas->setStyleSheet(style.panelStyleSheet(QStringLiteral("OmniBarPreviewBackdrop")));
	canvasLayout->setDirection(orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
	canvasLayout->setSpacing(style.spacing);

	for (const auto &config : entries)
		addEntry(config, state);

	if (widgetsById.isEmpty()) {
		QLabel *empty = new QLabel(obs_module_text("OmniBar.Preview.Empty"));
		empty->setStyleSheet(QString("color: %1;").arg(style.effectiveTextColor().name()));
		canvasLayout->addWidget(empty);
	}

	// Without a flexible space the entries sit at the start, as on the bar.
	bool hasFlexible = false;
	for (const auto &config : entries) {
		auto *spacer = config ? dynamic_cast<SpacerAction *>(config->action.get()) : nullptr;
		if (spacer && spacer->isExpanding() && config->showCondition.isSatisfied(config->action.get(), &state))
			hasFlexible = true;
	}
	if (!hasFlexible)
		canvasLayout->addStretch();

	updateScrollHeight();
	overlay->raise();
	QTimer::singleShot(0, this, &OmniBarPreview::updateOverlay);
}

void OmniBarPreview::addEntry(const std::shared_ptr<ButtonConfig> &config, const ObsStateSnapshot &state)
{
	if (!config || !config->action)
		return;

	// Off the bar in this state, so off the preview too: that is the point
	// of previewing a state.
	if (!config->showCondition.isSatisfied(config->action.get(), &state))
		return;

	QWidget *widget = nullptr;
	bool horizontal = orientation == Qt::Horizontal;

	if (config->isSpacer()) {
		auto *spacer = dynamic_cast<SpacerAction *>(config->action.get());
		int size = spacer ? spacer->getWidth() : 10;
		widget = new QWidget(canvas);
		if (spacer && spacer->isExpanding()) {
			widget->setSizePolicy(horizontal ? QSizePolicy::Expanding : QSizePolicy::Preferred,
					      horizontal ? QSizePolicy::Preferred : QSizePolicy::Expanding);
			if (horizontal)
				widget->setMinimumWidth(size);
			else
				widget->setMinimumHeight(size);
		} else if (horizontal) {
			widget->setFixedSize(size, style.buttonExtent());
		} else {
			widget->setFixedSize(style.buttonExtent(), size);
		}
	} else if (config->isDivider()) {
		widget = new OmniBarDivider(config, orientation, style, canvas);
	} else if (config->isDisplay()) {
		auto *display = new OmniBarDisplay(config, canvas);
		display->setPreviewMode(true);
		display->applyStyle(style);
		display->setSimulatedState(state);
		widget = display;
	} else {
		auto *button = new OmniBarButton(config, canvas);
		button->setPreviewMode(true);
		button->setBarArea(horizontal ? Qt::TopToolBarArea : Qt::LeftToolBarArea);
		button->applyStyle(style);
		button->setGroupIndicator(config->isGroup, false, false);

		// A group set to open on its condition opens here when the
		// previewed state meets it; an unset condition means its own
		// action, as on the bar.
		bool expanded = false;
		if (config->isGroup && config->groupExpand == GroupExpandMode::WhenActive) {
			ActivationCondition condition = config->expandCondition;
			if (!condition.isSet())
				condition.source = ConditionSource::OwnAction;
			expanded = condition.isSatisfied(config->action.get(), &state);
		}

		bool active = config->hasAction() ? config->action->isActiveIn(state) : expanded;
		button->setChecked(active);
		button->refreshPulse();
		widget = button;

		// Hidden on the bar for want of a target; shown faded here, so the
		// entry can still be found and fixed.
		if (config->targetMissing()) {
			auto *fade = new QGraphicsOpacityEffect(button);
			fade->setOpacity(0.35);
			button->setGraphicsEffect(fade);
			button->setToolTip(obs_module_text("OmniBar.Settings.MissingHint"));
		}

		canvasLayout->addWidget(widget);
		widget->setProperty(kOmniBarEntryIdProperty, config->id);
		widget->installEventFilter(this);
		widgetsById.insert(config->id, widget);

		if (expanded && config->groupDisplay == GroupDisplayMode::Inline) {
			for (const auto &child : config->children)
				addEntry(child, state);
		}
		return;
	}

	canvasLayout->addWidget(widget);
	widget->setProperty(kOmniBarEntryIdProperty, config->id);
	widget->installEventFilter(this);
	widgetsById.insert(config->id, widget);
}

void OmniBarPreview::updateOverlay()
{
	overlay->setGeometry(canvas->rect());
	overlay->raise();
	overlay->update();
}

void OmniBarPreview::updateScrollHeight()
{
	// Tall enough for a horizontal bar and its scroll bar, or for a useful
	// length of a vertical one.
	canvas->adjustSize();
	int bar = scroll->horizontalScrollBar()->sizeHint().height();
	if (orientation == Qt::Horizontal)
		scroll->setFixedHeight(canvasLayout->sizeHint().height() + bar + 2);
	else
		scroll->setFixedHeight(qMin(320, canvasLayout->sizeHint().height() + bar + 2));
}

bool OmniBarPreview::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == canvas) {
		if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)
			QTimer::singleShot(0, this, &OmniBarPreview::updateOverlay);
		return QWidget::eventFilter(watched, event);
	}

	// A click on an entry selects it for editing rather than pressing it.
	if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton) {
			QString id = watched->property(kOmniBarEntryIdProperty).toString();
			if (!id.isEmpty())
				emit entryClicked(id);
			return true;
		}
	}
	if (event->type() == QEvent::MouseButtonRelease)
		return true;

	return QWidget::eventFilter(watched, event);
}
