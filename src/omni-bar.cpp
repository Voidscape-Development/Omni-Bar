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
#include "display-metrics.hpp"
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
#include <QBuffer>
#include <QImageReader>
#include <QHash>
#include <QStylePainter>
#include <QStyleOptionToolButton>

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

// Locate a bundled icon by file name.
static QString findBundledIcon(const QString &name)
{
	if (name.isEmpty())
		return QString();

	// Icons used to ship as _dark/_light pairs. Configurations written back
	// then name one of those variants, which no longer exist.
	QString base = name;
	base.replace(QLatin1String("_dark."), QLatin1String("."));
	base.replace(QLatin1String("_light."), QLatin1String("."));

	QString path = moduleFilePath(QString("icons/%1").arg(base));
	if (!path.isEmpty())
		return path;

	return moduleFilePath(base);
}

// Bundled icons are drawn with a currentColor token. Qt's SVG renderer does not
// resolve it, so it is substituted before the bytes reach the image plugin -
// which also lets one file serve every theme and custom text colour.
static QIcon loadIcon(const QString &path, const QColor &tint, int size, bool recolor)
{
	if (path.isEmpty())
		return QIcon();

	if (!recolor || !path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive) || !tint.isValid())
		return QIcon(path);

	qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
	int pixels = qMax(1, qRound(size * ratio));

	static QHash<QString, QIcon> cache;
	QString key = QString("%1|%2|%3").arg(path, tint.name(QColor::HexArgb)).arg(pixels);
	auto cached = cache.constFind(key);
	if (cached != cache.constEnd())
		return *cached;

	QIcon icon;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) {
		QByteArray data = file.readAll();
		data.replace("currentColor", tint.name(QColor::HexRgb).toUtf8());

		QBuffer buffer(&data);
		QImageReader reader(&buffer, "svg");
		reader.setScaledSize(QSize(pixels, pixels));

		QImage image = reader.read();
		if (!image.isNull()) {
			QPixmap pixmap = QPixmap::fromImage(image);
			pixmap.setDevicePixelRatio(ratio);
			icon = QIcon(pixmap);
		}
	}

	// If anything above failed, let Qt load the file untouched.
	if (icon.isNull())
		icon = QIcon(path);

	// Dragging the icon-size spin box would otherwise grow this without end.
	if (cache.size() > 256)
		cache.clear();
	cache.insert(key, icon);

	return icon;
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

		// Start, stop and toggle each get their own glyph, so two buttons
		// from the same family never look identical.
		switch (frontendAction->getActionType()) {
		case FrontendActionType::ToggleStreaming:
			return "stream.svg";
		case FrontendActionType::StartStreaming:
			return "stream-start.svg";
		case FrontendActionType::StopStreaming:
			return "stream-stop.svg";
		case FrontendActionType::ToggleRecording:
			return "record.svg";
		case FrontendActionType::StartRecording:
			return "record-start.svg";
		case FrontendActionType::StopRecording:
			return "record-stop.svg";
		case FrontendActionType::TogglePauseRecording:
			return "pause-toggle.svg";
		case FrontendActionType::PauseRecording:
			return "pause.svg";
		case FrontendActionType::UnpauseRecording:
			return "play.svg";
		case FrontendActionType::ToggleReplayBuffer:
			return "replay.svg";
		case FrontendActionType::StartReplayBuffer:
			return "replay-start.svg";
		case FrontendActionType::StopReplayBuffer:
			return "replay-stop.svg";
		case FrontendActionType::SaveReplayBuffer:
			return "save-replay.svg";
		case FrontendActionType::ToggleVirtualCam:
			return "virtual-cam.svg";
		case FrontendActionType::StartVirtualCam:
			return "virtual-cam-start.svg";
		case FrontendActionType::StopVirtualCam:
			return "virtual-cam-stop.svg";
		case FrontendActionType::ToggleStudioMode:
			return "studio-mode.svg";
		case FrontendActionType::EnableStudioMode:
			return "studio-mode-on.svg";
		case FrontendActionType::DisableStudioMode:
			return "studio-mode-off.svg";
		case FrontendActionType::TransitionToProgram:
			return "transition.svg";
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
	case ActionType::Divider:
		return "divider.svg";
	case ActionType::Display:
		return "display.svg";
	case ActionType::None:
		// A container-only group reads better as a group than as nothing.
		return config->isGroup ? "group.svg" : "none.svg";
	}

	return QString();
}

QIcon omniBarIconForConfig(const std::shared_ptr<ButtonConfig> &config, const QColor &tint, int size)
{
	if (!config)
		return QIcon();

	const QString &iconPath = config->iconPath;

	// A custom icon is stored as a filesystem path; bundled icons are stored
	// as a bare file name.
	if (!iconPath.isEmpty()) {
		if (iconPath.contains('/') || iconPath.contains('\\')) {
			if (QFile::exists(iconPath))
				return loadIcon(iconPath, tint, size, config->tintIcon);
		} else {
			QString bundled = findBundledIcon(iconPath);
			if (!bundled.isEmpty())
				return loadIcon(bundled, tint, size, true);
		}
	}

	QString fallback = findBundledIcon(defaultIconName(config));
	return fallback.isEmpty() ? QIcon() : loadIcon(fallback, tint, size, true);
}

// Space between a stacked label and the icon it sits over. Shared by the height
// a stacked button asks for and the placement paintLabelAbove draws at, so the
// two agree.
static const int kStackedLabelGap = 4;

// Apply a config's label, display mode, icon and colour override to a tool
// button.
static void omniBarApplyButtonAppearance(QToolButton *button, const std::shared_ptr<ButtonConfig> &config,
					 const BarStyle &style)
{
	if (!button || !config)
		return;

	button->setIcon(omniBarIconForConfig(config, style.effectiveTextColor(), style.iconSize));
	button->setIconSize(QSize(style.iconSize, style.iconSize));

	QString label = config->label;
	if (label.isEmpty())
		label = config->tooltip;
	if (label.isEmpty() && config->action)
		label = config->action->getDisplayName();
	button->setText(label);

	// A readout with its text hidden would have nothing left to say, so that
	// placement is quietly treated as text only.
	ButtonDisplayMode mode = config->displayMode;
	if (config->isDisplay() && mode == ButtonDisplayMode::IconOnly)
		mode = ButtonDisplayMode::TextOnly;

	// Qt only lays a tool button's label out to the right of or below the
	// icon. The other two placements are produced from those: mirroring the
	// button moves the label to the left, and OmniBarToolButton paints the
	// text-above case itself. Both borrow the matching Qt style so the size
	// hint stays correct.
	button->setLayoutDirection(mode == ButtonDisplayMode::TextLeft ? Qt::RightToLeft : Qt::LeftToRight);

	switch (mode) {
	case ButtonDisplayMode::IconOnly:
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		break;
	case ButtonDisplayMode::TextOnly:
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		break;
	case ButtonDisplayMode::TextRight:
	case ButtonDisplayMode::TextLeft:
		button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		break;
	case ButtonDisplayMode::TextBelow:
	case ButtonDisplayMode::TextAbove:
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		break;
	}

	button->setToolTip(config->tooltip.isEmpty() ? label : config->tooltip);

	// One height for every entry that is not stacking its label over its icon,
	// whether it shows an icon, a label beside one, or nothing but text: they
	// sit side by side on the bar and have to line up with one another. The
	// style's extent already allows for the application font; this takes the
	// widget's own metrics too, in case a theme has given the bar a larger one.
	int textHeight = button->fontMetrics().height();
	int extent = qMax(style.buttonExtent(), textHeight + (style.buttonPadding * 2) + (style.borderWidth * 2));

	if (mode == ButtonDisplayMode::IconOnly) {
		button->setFixedSize(extent, extent);
	} else {
		// Pinned rather than floored: a floor still leaves the final height
		// to Qt's size hint, and the hint for a labelled tool button carries
		// margins an icon-only one never sees, so the two ended up a few
		// pixels apart.
		bool stacked = mode == ButtonDisplayMode::TextBelow || mode == ButtonDisplayMode::TextAbove;
		int height = stacked ? extent + textHeight + kStackedLabelGap : extent;

		button->setMinimumSize(0, 0);
		button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		button->setFixedHeight(height);
		button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	}

	if (config->useCustomColor && config->customColor.isValid())
		button->setStyleSheet(style.buttonAccentStyleSheet(config->customColor));
	else
		button->setStyleSheet(QString());
}

// ============================================================================
// OmniBarToolButton
// ============================================================================

void OmniBarToolButton::setBarAction(QAction *action)
{
	barAction = action;
	// The entry starts out matching whatever the widget was doing when it
	// was handed over, which is not necessarily what it should be doing now.
	refreshVisibility();
}

void OmniBarToolButton::setPreviewMode(bool enabled)
{
	previewMode = enabled;
	refreshVisibility();
}

void OmniBarToolButton::applyVisible(bool visible)
{
	// A QToolBar lays itself out from its entries, not from its widgets, and
	// shows or hides each widget to match, so a widget in a toolbar cannot
	// hide itself: hiding it is undone by the next relayout, and a widget
	// already hidden when its entry was created leaves that entry invisible
	// for good. The entry is what has to be driven.
	if (barAction)
		barAction->setVisible(visible);
	setVisible(visible);
}

// Qt has no text-above-icon tool button style, so this draws the frame without
// a label and then places the icon and text by hand. The button still carries
// Qt's text-under-icon style, which keeps its size hint correct.
void OmniBarToolButton::paintLabelAbove()
{
	QStylePainter painter(this);
	QStyleOptionToolButton option;
	initStyleOption(&option);

	QStyleOptionToolButton frame = option;
	frame.text.clear();
	frame.icon = QIcon();
	painter.drawComplexControl(QStyle::CC_ToolButton, frame);

	const int gap = kStackedLabelGap;
	QFontMetrics metrics(font());
	QSize icon = iconSize();
	int textHeight = metrics.height();
	int top = rect().top() + (rect().height() - (textHeight + gap + icon.height())) / 2;

	QRect textRect(rect().left(), top, rect().width(), textHeight);
	painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
	painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
			 metrics.elidedText(text(), Qt::ElideRight, textRect.width()));

	QRect iconRect(rect().left() + (rect().width() - icon.width()) / 2, top + textHeight + gap, icon.width(),
		       icon.height());
	option.icon.paint(&painter, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled,
			  isChecked() ? QIcon::On : QIcon::Off);
}

// ============================================================================
// OmniBarButton
// ============================================================================

OmniBarButton::OmniBarButton(std::shared_ptr<ButtonConfig> config, QWidget *parent)
	: OmniBarToolButton(parent),
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
	appliedStyle = style;
	omniBarApplyButtonAppearance(this, buttonConfig, style);
	// The appearance pass has just reset the stylesheet, so a button that was
	// mid-breath needs its current frame putting back.
	refreshPulse();
}

QString OmniBarButton::restingStyleSheet() const
{
	if (buttonConfig && buttonConfig->useCustomColor && buttonConfig->customColor.isValid())
		return appliedStyle.buttonAccentStyleSheet(buttonConfig->customColor);
	return QString();
}

void OmniBarButton::applyPulseFrame()
{
	QColor accent = buttonConfig && buttonConfig->useCustomColor && buttonConfig->customColor.isValid()
				? buttonConfig->customColor
				: appliedStyle.effectiveButtonChecked();

	QString sheet = appliedStyle.buttonPulseStyleSheet(accent, omniBarPulseFactor(appliedStyle));

	// Most frames land on the same rounded alpha as the one before, so this
	// keeps the restyling down to the frames that actually look different.
	if (sheet == pulseSheet)
		return;

	pulseSheet = sheet;
	setStyleSheet(sheet);
}

void OmniBarButton::refreshPulse()
{
	// Only an action can be active. A group with no action of its own uses
	// the checked state to show that it is open, which is not something worth
	// drawing attention to.
	bool wanted = buttonConfig && buttonConfig->pulseWhenActive && buttonConfig->hasAction() && isChecked() &&
		      isVisible();

	if (!wanted) {
		if (pulseTimer)
			pulseTimer->stop();
		if (!pulseSheet.isEmpty()) {
			pulseSheet.clear();
			setStyleSheet(restingStyleSheet());
		}
		return;
	}

	if (!pulseTimer) {
		pulseTimer = new QTimer(this);
		pulseTimer->setInterval(40);
		connect(pulseTimer, &QTimer::timeout, this, &OmniBarButton::applyPulseFrame);
	}

	applyPulseFrame();
	if (!pulseTimer->isActive())
		pulseTimer->start();
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
	applyVisible(previewMode || (actionValid && !collapsed));
	refreshPulse();
}

void OmniBarButton::updateState()
{
	if (!buttonConfig)
		return;

	// Hide buttons whose target no longer exists (source deleted, etc), and
	// those whose show condition does not currently hold.
	actionValid = buttonConfig->isValid() && buttonConfig->showCondition.isSatisfied(buttonConfig->action.get());
	refreshVisibility();

	// Groups without an action of their own use the checked state to show
	// whether they are open, so leave it to the group logic.
	if (buttonConfig->hasAction())
		setChecked(buttonConfig->action->isActive());

	refreshPulse();
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
	if (buttonConfig && buttonConfig->displayMode == ButtonDisplayMode::TextAbove)
		paintLabelAbove();
	else
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

void OmniBarButton::showEvent(QShowEvent *event)
{
	QToolButton::showEvent(event);
	refreshPulse();
}

void OmniBarButton::hideEvent(QHideEvent *event)
{
	QToolButton::hideEvent(event);
	refreshPulse();
}

// ============================================================================
// OmniBarDisplay
// ============================================================================

OmniBarDisplay::OmniBarDisplay(std::shared_ptr<ButtonConfig> config, QWidget *parent)
	: OmniBarToolButton(parent),
	  displayConfig(config)
{
	setCheckable(false);
	setFocusPolicy(Qt::NoFocus);
	// A number to read, not a control: it must not light up under the mouse
	// or take a click meant for the OBS window behind it.
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setToolButtonStyle(Qt::ToolButtonTextOnly);

	applyStyle(SettingsManager::getStyle());
}

void OmniBarDisplay::applyStyle(const BarStyle &style)
{
	appliedStyle = style;
	omniBarApplyButtonAppearance(this, displayConfig, style);

	// The shared pass puts a button's accent override on; on a readout that
	// colour belongs to the text instead, since there is no hover or active
	// state here for it to land on.
	if (displayConfig && displayConfig->useCustomColor && displayConfig->customColor.isValid())
		setStyleSheet(style.displayAccentStyleSheet(displayConfig->customColor));

	// The appearance pass writes the configured label; the reading goes back
	// over the top of it.
	updateValue();
}

void OmniBarDisplay::updateValue()
{
	if (!displayConfig)
		return;

	auto *display = dynamic_cast<DisplayAction *>(displayConfig->action.get());
	if (!display)
		return;

	QString reading = previewMode ? OmniBarMetrics::previewValue(display->getMetric(), display->getClockFormat())
				      : display->currentValue();

	// The configured label reads as a prefix - "Live 01:23:45" - so a row of
	// readouts can say what each number is without a tooltip. Nothing below
	// is reapplied unless it has actually changed: this runs on every tick,
	// and each of these would otherwise ask for a fresh layout twice a
	// second for the whole life of the bar.
	QString text = displayConfig->label.isEmpty() ? reading : displayConfig->label + QLatin1Char(' ') + reading;
	if (text != this->text())
		setText(text);

	if (displayConfig->tooltip.isEmpty()) {
		QString tip = OmniBarMetrics::label(display->getMetric());
		if (tip != toolTip())
			setToolTip(tip);
	}

	// Digits are rarely all the same width, so a timer left to fit its own
	// content nudges everything beside it along as it counts.
	int floor = qMax(0, display->getMinimumWidth());
	if (minimumWidth() != floor)
		setMinimumWidth(floor);

	conditionHolds = displayConfig->showCondition.isSatisfied(displayConfig->action.get());
	refreshVisibility();
}

void OmniBarDisplay::refreshVisibility()
{
	applyVisible(previewMode || conditionHolds);
}

void OmniBarDisplay::paintEvent(QPaintEvent *event)
{
	if (displayConfig && displayConfig->displayMode == ButtonDisplayMode::TextAbove)
		paintLabelAbove();
	else
		QToolButton::paintEvent(event);
}

// ============================================================================
// OmniBarDivider
// ============================================================================

OmniBarDivider::OmniBarDivider(std::shared_ptr<ButtonConfig> config, Qt::Orientation orientation, const BarStyle &style,
			       QWidget *parent)
	: QWidget(parent),
	  dividerConfig(config)
{
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	applyStyle(orientation, style);
}

void OmniBarDivider::applyStyle(Qt::Orientation orientation, const BarStyle &style)
{
	barOrientation = orientation;

	auto *divider = dividerConfig ? dynamic_cast<DividerAction *>(dividerConfig->action.get()) : nullptr;
	int thickness = divider ? divider->getThickness() : 1;

	lineColor = divider && divider->hasCustomColor() ? divider->getCustomColor() : style.effectiveButtonBorder();

	// Thin across the bar with the spacing repeated either side, and as long
	// as a button along it.
	int across = thickness + (style.spacing * 2);
	int along = style.buttonExtent();

	if (orientation == Qt::Horizontal)
		setFixedSize(across, along);
	else
		setFixedSize(along, across);

	update();
}

void OmniBarDivider::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);

	auto *divider = dividerConfig ? dynamic_cast<DividerAction *>(dividerConfig->action.get()) : nullptr;
	int thickness = divider ? divider->getThickness() : 1;
	int percent = divider ? divider->getLengthPercent() : 70;

	QPainter painter(this);
	painter.setPen(Qt::NoPen);
	painter.setBrush(lineColor);

	if (barOrientation == Qt::Horizontal) {
		int length = qMax(2, height() * percent / 100);
		painter.drawRect(QRect((width() - thickness) / 2, (height() - length) / 2, thickness, length));
	} else {
		int length = qMax(2, width() * percent / 100);
		painter.drawRect(QRect((width() - length) / 2, (height() - thickness) / 2, length, thickness));
	}
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

	// Frontend events cover starting and stopping, but nothing announces a
	// filter being switched on elsewhere or a timer ticking over, so the bar
	// also looks for itself. Slow enough to be free, quick enough that a
	// readout never visibly lags.
	pollTimer = new QTimer(this);
	pollTimer->setInterval(500);
	connect(pollTimer, &QTimer::timeout, this, &OmniBar::onPollTimer);
	pollTimer->start();

	// Spacers and dividers are laid out along the bar, so they have to be
	// rebuilt whenever it turns. The bar is built before it is docked, so
	// this also covers the initial move into a left or right dock.
	connect(this, &QToolBar::orientationChanged, this, &OmniBar::onOrientationChanged);

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

	DockPosition position = SettingsManager::getDockPosition();

	if (position == DockPosition::None) {
		// Taken off the window rather than merely hidden, so it gives its
		// space back. The settings stay reachable from the Tools menu, and
		// a position hotkey brings the bar back.
		mainWindow->removeToolBar(this);
		hide();
		return;
	}

	Qt::ToolBarArea area;
	switch (position) {
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
	displays.clear();
	decorations.clear();
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
			QAction *spacerEntry = addWidget(spacer);
			barActions.append(spacerEntry);
			decorations.append({config, spacerEntry});
			continue;
		}

		if (config->isDivider()) {
			auto *divider = new OmniBarDivider(config, orientation(), SettingsManager::getStyle(), this);
			QAction *dividerEntry = addWidget(divider);
			barActions.append(dividerEntry);
			decorations.append({config, dividerEntry});
			continue;
		}

		if (config->isDisplay()) {
			auto *display = new OmniBarDisplay(config, this);
			QAction *displayEntry = addWidget(display);
			barActions.append(displayEntry);
			// A readout keeps its own entry rather than joining the
			// decorations: it has a reading to refresh as well as a
			// condition to follow.
			display->setBarAction(displayEntry);
			displays.append(display);
			continue;
		}

		OmniBarButton *button = createButton(config);
		QAction *buttonEntry = addWidget(button);
		barActions.append(buttonEntry);
		button->setBarAction(buttonEntry);
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
		// Groups hold buttons; bar decoration has no meaning inside one.
		if (!childConfig || !childConfig->action || childConfig->isDecoration())
			continue;

		OmniBarButton *child = createButton(childConfig);
		group->childButtons.append(child);
		buttons.append(child);

		if (isInline) {
			QAction *action = addWidget(child);
			barActions.append(action);
			// Collapsed first, so the entry is created hidden rather
			// than flashing onto the bar for one layout pass.
			child->setCollapsed(true);
			child->setBarAction(action);
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
			if (!g->expanded && !groupConditionHolds(g))
				return;
			setGroupExpanded(g, !g->expanded);
		});
		break;

	case GroupExpandMode::WhenActive:
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
			if (groupConditionHolds(g))
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

// An unset condition on the WhenActive mode means the group's own action, which
// is what "expand when active" meant before conditions existed.
bool OmniBar::groupConditionHolds(GroupRuntime *group) const
{
	if (!group || !group->config)
		return false;

	ActivationCondition condition = group->config->expandCondition;
	if (group->config->groupExpand == GroupExpandMode::WhenActive && !condition.isSet())
		condition.source = ConditionSource::OwnAction;

	return condition.isSatisfied(group->config->action.get());
}

void OmniBar::setGroupExpanded(GroupRuntime *group, bool expanded)
{
	if (!group || !group->parentButton)
		return;
	if (group->expanded == expanded)
		return;

	group->expanded = expanded;

	if (group->config->groupDisplay == GroupDisplayMode::Inline) {
		// Each child puts itself on and off the bar, so a child whose own
		// show condition has stopped holding stays off even here.
		for (auto *child : group->childButtons)
			child->setCollapsed(!expanded);
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

	for (auto *display : displays) {
		display->updateValue();
	}

	for (auto &group : groups) {
		const auto &config = group->config;
		bool holds = groupConditionHolds(group.get());

		if (config->groupExpand == GroupExpandMode::WhenActive) {
			// Here the condition is the trigger.
			setGroupExpanded(group.get(), holds);
		} else if (!holds) {
			// Click and hover open the group themselves; the condition
			// only decides whether they are allowed to.
			setGroupExpanded(group.get(), false);
		}
	}

	for (const auto &decoration : decorations) {
		if (decoration.second)
			decoration.second->setVisible(
				decoration.first->showCondition.isSatisfied(decoration.first->action.get()));
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

void OmniBar::onPollTimer()
{
	// Sampled even while the bar is off the window, so the rates the
	// readouts show are already settled when it comes back.
	OmniBarMetrics::sample();

	if (!isVisible())
		return;

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

void OmniBar::onOrientationChanged(Qt::Orientation orientation)
{
	Q_UNUSED(orientation);
	rebuildToolbar();
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
