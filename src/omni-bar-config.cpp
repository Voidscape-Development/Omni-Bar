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

#include "omni-bar-config.hpp"
#include "omni-bar.hpp"
#include "button-action.hpp"
#include "display-metrics.hpp"
#include "settings-manager.hpp"
#include "bar-preview.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QMenu>
#include <QApplication>
#include <QPalette>
#include <QFile>
#include <QDropEvent>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QUuid>
#include <QStandardItemModel>
#include <QShortcut>
#include <QKeySequence>
#include <QScrollArea>
#include <QTimer>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QScreen>
#include <QHeaderView>
#include <QSplitter>
#include <QButtonGroup>
#include <QTreeWidgetItemIterator>

static const int kConfigIdRole = Qt::UserRole;

// Rows of mutually exclusive buttons mark the chosen one in the highlight
// colour, since a checked push button is easy to miss in some themes.
static const char *const kSegmentSheet =
	"QPushButton:checked { background-color: palette(highlight); color: palette(highlighted-text); }";
static const int kConfigIsGroupRole = Qt::UserRole + 1;
// Spacers, dividers and readouts are part of the bar itself, so they never go
// inside a group - a group would only drop them on the floor when it built its
// children.
static const int kConfigIsDecorationRole = Qt::UserRole + 2;

// Paint a colour swatch onto a button and label it with its hex value.
static void setColorSwatch(QPushButton *button, const QColor &color)
{
	if (!button)
		return;

	if (!color.isValid()) {
		button->setStyleSheet(QString());
		button->setText(obs_module_text("OmniBar.Style.NoColor"));
		return;
	}

	// A fully transparent colour painted solid would read as black, which is
	// the opposite of what it means.
	if (color.alpha() == 0) {
		button->setStyleSheet(QString());
		button->setText(obs_module_text("OmniBar.Style.Transparent"));
		return;
	}

	// Pick readable text for whichever colour landed on the swatch.
	int luminance = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
	QString foreground = luminance < 128 ? "#ffffff" : "#000000";

	button->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid palette(mid);"
				      " border-radius: 3px; padding: 4px; }")
				      .arg(color.name(QColor::HexRgb), foreground));
	button->setText(color.name(QColor::HexArgb).toUpper());
}

// Selects the entry whose data is value. One that is no longer there - a source
// deleted, or one that belongs to another scene collection - is added and
// marked as missing rather than quietly swapped for whatever happens to be
// first in the list, which pressing OK would then save.
static void selectOrKeepMissing(QComboBox *combo, const QString &value)
{
	if (!combo)
		return;

	int index = combo->findData(value);
	if (index < 0 && !value.isEmpty()) {
		combo->insertItem(0, QString::fromUtf8(obs_module_text("OmniBar.Settings.MissingFormat")).arg(value),
				  value);
		combo->setItemData(0, QString::fromUtf8(obs_module_text("OmniBar.Settings.MissingHint")),
				   Qt::ToolTipRole);
		index = 0;
	}
	if (index >= 0)
		combo->setCurrentIndex(index);
}

// A stacked widget is as tall as its tallest page, which leaves a gap under a
// short one. Only the page showing is allowed a say in the size.
static void fitStackToCurrent(QStackedWidget *stack)
{
	if (!stack)
		return;
	for (int i = 0; i < stack->count(); i++) {
		bool current = i == stack->currentIndex();
		stack->widget(i)->setSizePolicy(current ? QSizePolicy::Preferred : QSizePolicy::Ignored,
						current ? QSizePolicy::Preferred : QSizePolicy::Ignored);
	}
	stack->adjustSize();
	stack->updateGeometry();
}

// ============================================================================
// ConditionEditor
// ============================================================================

ConditionEditor::ConditionEditor(bool allowOwnAction, QWidget *parent) : QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	// Reads as a sentence - "While not / Streaming" - rather than a state
	// with a checkbox saying "not active" hanging off the end of it.
	modeCombo = new QComboBox();
	modeCombo->addItem(obs_module_text("OmniBar.Condition.Mode.Always"), 0);
	modeCombo->addItem(obs_module_text("OmniBar.Condition.Mode.While"), 1);
	modeCombo->addItem(obs_module_text("OmniBar.Condition.Mode.WhileNot"), 2);
	layout->addWidget(modeCombo);

	sourceCombo = new QComboBox();
	if (allowOwnAction)
		sourceCombo->addItem(obs_module_text("OmniBar.Condition.OwnAction"),
				     static_cast<int>(ConditionSource::OwnAction));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.Streaming"),
			     static_cast<int>(ConditionSource::Streaming));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.Recording"),
			     static_cast<int>(ConditionSource::Recording));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.RecordingPaused"),
			     static_cast<int>(ConditionSource::RecordingPaused));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.ReplayBuffer"),
			     static_cast<int>(ConditionSource::ReplayBuffer));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.VirtualCam"),
			     static_cast<int>(ConditionSource::VirtualCam));
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.StudioMode"),
			     static_cast<int>(ConditionSource::StudioMode));
	layout->addWidget(sourceCombo, 1);

	connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
		sourceCombo->setVisible(modeCombo->currentData().toInt() != 0);
		emit changed();
	});
	connect(sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConditionEditor::changed);

	sourceCombo->setVisible(false);
}

void ConditionEditor::setCondition(const ActivationCondition &condition)
{
	QSignalBlocker modeBlocker(modeCombo);
	QSignalBlocker sourceBlocker(sourceCombo);

	int index = sourceCombo->findData(static_cast<int>(condition.source));
	bool set = condition.isSet() && index >= 0;

	modeCombo->setCurrentIndex(!set ? 0 : (condition.inverted ? 2 : 1));
	if (index >= 0)
		sourceCombo->setCurrentIndex(index);
	sourceCombo->setVisible(set);
}

ActivationCondition ConditionEditor::condition() const
{
	ActivationCondition result;
	int mode = modeCombo->currentData().toInt();
	if (mode == 0)
		return result;
	result.source = static_cast<ConditionSource>(sourceCombo->currentData().toInt());
	result.inverted = mode == 2;
	return result;
}

// ============================================================================
// ButtonTreeWidget
// ============================================================================

ButtonTreeWidget::ButtonTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
	setColumnCount(2);
	setHeaderLabels(
		{obs_module_text("OmniBar.Settings.ColumnButton"), obs_module_text("OmniBar.Settings.ColumnAction")});
	setRootIsDecorated(true);
	setUniformRowHeights(true);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setDragDropMode(QAbstractItemView::InternalMove);
	setDefaultDropAction(Qt::MoveAction);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	header()->setStretchLastSection(true);
	header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	// Headings sit over their columns rather than against the left edge; the
	// rows themselves stay left aligned, since they are read as a list.
	header()->setDefaultAlignment(Qt::AlignCenter);
}

bool ButtonTreeWidget::isDropAllowed(QTreeWidgetItem *source, QTreeWidgetItem *target,
				     DropIndicatorPosition position) const
{
	if (!source)
		return false;

	bool sourceIsGroup = source->data(0, kConfigIsGroupRole).toBool();
	bool sourceIsDecoration = source->data(0, kConfigIsDecorationRole).toBool();
	// Neither a group nor a piece of the bar can go inside a group.
	bool nestable = !sourceIsGroup && !sourceIsDecoration;

	if (position == OnItem) {
		if (!target || target == source)
			return false;
		if (!(target->flags() & Qt::ItemIsDropEnabled))
			return false;
		// Groups only nest one level deep, so a group can never be
		// dropped inside another group.
		return nestable && target->parent() == nullptr;
	}

	if (position == OnViewport)
		return true;

	// Dropping between a group's children lands the item in that group.
	if (target && target->parent() != nullptr)
		return nestable;

	return true;
}

void ButtonTreeWidget::dropEvent(QDropEvent *event)
{
	QTreeWidgetItem *source = currentItem();
	QTreeWidgetItem *target = itemAt(event->position().toPoint());

	if (!isDropAllowed(source, target, dropIndicatorPosition())) {
		event->ignore();
		return;
	}

	QTreeWidget::dropEvent(event);
	emit itemsReordered();
}

// ============================================================================
// ActionPicker
// ============================================================================

// One entry in the picker. A frontend family stands for its start, stop and
// toggle variants together; the rest are a kind of action that needs a target.
struct ActionChoice {
	const char *id;
	const char *labelKey;
	const char *categoryKey;
	const char *icon;
	// Shown beside the entry, for the kinds whose variants do not say it.
	const char *hintKey;
};

static const ActionChoice kActionChoices[] = {
	{"frontend:stream", "OmniBar.Choice.Stream", "OmniBar.IconGroup.Streaming", "stream.svg", nullptr},
	{"frontend:record", "OmniBar.Choice.Record", "OmniBar.IconGroup.Recording", "record.svg", nullptr},
	{"frontend:pause", "OmniBar.Choice.Pause", "OmniBar.IconGroup.Recording", "pause-toggle.svg", nullptr},
	{"frontend:replay", "OmniBar.Choice.Replay", "OmniBar.IconGroup.Replay", "replay.svg", nullptr},
	{"frontend:save_replay", "OmniBar.Choice.SaveReplay", "OmniBar.IconGroup.Replay", "save-replay.svg", nullptr},
	{"frontend:virtual_cam", "OmniBar.Choice.VirtualCam", "OmniBar.IconGroup.Camera", "virtual-cam.svg", nullptr},
	{"frontend:studio", "OmniBar.Choice.Studio", "OmniBar.IconGroup.Studio", "studio-mode.svg", nullptr},
	{"frontend:transition", "OmniBar.Choice.Transition", "OmniBar.IconGroup.Studio", "transition.svg", nullptr},
	{"scene", "OmniBar.Choice.Scene", "OmniBar.IconGroup.Scenes", "scene.svg", "OmniBar.Choice.SceneHint"},
	{"mute", "OmniBar.Choice.Mute", "OmniBar.IconGroup.Audio", "audio.svg", "OmniBar.Choice.MuteHint"},
	{"visibility", "OmniBar.Choice.Visibility", "OmniBar.IconGroup.Sources", "visibility.svg",
	 "OmniBar.Choice.VisibilityHint"},
	{"filter", "OmniBar.Choice.Filter", "OmniBar.IconGroup.Sources", "filter.svg", nullptr},
	{"hotkey", "OmniBar.Choice.Hotkey", "OmniBar.IconGroup.Sources", "hotkey.svg", "OmniBar.Choice.HotkeyHint"},
	{"none", "OmniBar.Choice.None", "OmniBar.IconGroup.Other", "group.svg", "OmniBar.Choice.NoneHint"},
};

static const ActionChoice *findChoice(const QString &id)
{
	for (const auto &choice : kActionChoices) {
		if (id == QLatin1String(choice.id))
			return &choice;
	}
	return nullptr;
}

// The frontend family a choice stands for, or an empty string.
static QString choiceFamily(const QString &choice)
{
	static const QString prefix = QStringLiteral("frontend:");
	return choice.startsWith(prefix) ? choice.mid(prefix.size()) : QString();
}

// The picker entry an action belongs to.
static QString choiceForAction(const ButtonAction *action)
{
	if (!action)
		return QStringLiteral("none");

	switch (action->getType()) {
	case ActionType::Frontend: {
		auto *frontend = dynamic_cast<const FrontendAction *>(action);
		return frontend ? QStringLiteral("frontend:") +
					  QString::fromUtf8(FrontendAction::info(frontend->getActionType()).family)
				: QStringLiteral("none");
	}
	case ActionType::SourceHotkey:
		return QStringLiteral("hotkey");
	case ActionType::SourceFilter:
		return QStringLiteral("filter");
	case ActionType::SourceVisibility:
		return QStringLiteral("visibility");
	case ActionType::Scene:
		return QStringLiteral("scene");
	case ActionType::AudioMute:
		return QStringLiteral("mute");
	default:
		return QStringLiteral("none");
	}
}

// What appears beside an entry: its variants for a family with several, its
// own hint otherwise.
static QString choiceHint(const ActionChoice &choice)
{
	QString family = choiceFamily(QString::fromUtf8(choice.id));
	if (!family.isEmpty()) {
		QStringList variants;
		const auto members = FrontendAction::familyMembers(family);
		if (members.size() > 1) {
			for (FrontendActionType type : members)
				variants << QString::fromUtf8(obs_module_text(FrontendAction::info(type).variantKey));
		}
		return variants.join(QStringLiteral(" · "));
	}
	return choice.hintKey ? QString::fromUtf8(obs_module_text(choice.hintKey)) : QString();
}

// Everything a search can match an entry on: its name, its group, its hint,
// and for a family the full name of every action in it, so "stop stream"
// finds Stream.
static QString choiceSearchText(const ActionChoice &choice)
{
	QStringList words;
	words << QString::fromUtf8(obs_module_text(choice.labelKey));
	words << QString::fromUtf8(obs_module_text(choice.categoryKey));
	words << choiceHint(choice);
	QString family = choiceFamily(QString::fromUtf8(choice.id));
	if (!family.isEmpty()) {
		for (FrontendActionType type : FrontendAction::familyMembers(family))
			words << FrontendAction::getFrontendActionLabel(type);
	}
	return words.join(QLatin1Char(' ')).toLower();
}

static const int kChoiceRole = Qt::UserRole;
static const int kSearchRole = Qt::UserRole + 1;

QString ActionPicker::choiceLabel(const QString &choice)
{
	const ActionChoice *found = findChoice(choice);
	return found ? QString::fromUtf8(obs_module_text(found->labelKey)) : QString();
}

QString ActionPicker::choiceIcon(const QString &choice)
{
	const ActionChoice *found = findChoice(choice);
	return found ? QString::fromUtf8(found->icon) : QString();
}

ActionPicker::ActionPicker(QWidget *parent) : QWidget(parent, Qt::Popup)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	search = new QLineEdit();
	search->setPlaceholderText(obs_module_text("OmniBar.Picker.Search"));
	search->setClearButtonEnabled(true);
	search->installEventFilter(this);
	connect(search, &QLineEdit::textChanged, this, &ActionPicker::applyFilter);
	layout->addWidget(search);

	list = new QTreeWidget();
	list->setColumnCount(2);
	list->setHeaderHidden(true);
	list->setRootIsDecorated(false);
	list->setIndentation(10);
	// Headings are shorter than entries, so rows are not all one height.
	list->setUniformRowHeights(false);
	list->setFocusPolicy(Qt::NoFocus);
	list->header()->setStretchLastSection(false);
	list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	connect(list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { choose(item); });
	layout->addWidget(list, 1);

	QColor ink = palette().color(QPalette::Text);
	QColor quiet = ink;
	quiet.setAlphaF(0.6f);
	QHash<QString, QTreeWidgetItem *> categories;

	for (const auto &choice : kActionChoices) {
		QString categoryKey = QString::fromUtf8(choice.categoryKey);
		QTreeWidgetItem *category = categories.value(categoryKey);
		if (!category) {
			category = new QTreeWidgetItem(list);
			category->setText(0, QString::fromUtf8(obs_module_text(choice.categoryKey)).toUpper());
			category->setFlags(Qt::ItemIsEnabled);
			QFont font = category->font(0);
			font.setBold(true);
			font.setPointSizeF(font.pointSizeF() * 0.85);
			category->setFont(0, font);
			category->setForeground(0, quiet);
			category->setFirstColumnSpanned(true);
			// A little air above each heading, so groups read as groups.
			category->setSizeHint(0, QSize(0, QFontMetrics(font).height() + 10));
			categories.insert(categoryKey, category);
		}

		QTreeWidgetItem *item = new QTreeWidgetItem(category);
		item->setText(0, QString::fromUtf8(obs_module_text(choice.labelKey)));
		item->setText(1, choiceHint(choice));
		item->setForeground(1, quiet);
		item->setIcon(0, omniBarBundledIcon(QString::fromUtf8(choice.icon), ink, 16));
		item->setData(0, kChoiceRole, QString::fromUtf8(choice.id));
		item->setData(0, kSearchRole, choiceSearchText(choice));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	}
	list->expandAll();

	resize(380, 480);
}

void ActionPicker::popup(QWidget *anchor, const QString &currentChoice)
{
	search->clear();
	applyFilter(QString());

	for (QTreeWidgetItemIterator it(list); *it; ++it) {
		if ((*it)->data(0, kChoiceRole).toString() == currentChoice) {
			list->setCurrentItem(*it);
			list->scrollToItem(*it);
			break;
		}
	}

	resize(qMax(380, anchor->width()), height());
	QPoint position = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
	if (QScreen *screen = anchor->screen()) {
		QRect available = screen->availableGeometry();
		if (position.y() + height() > available.bottom())
			position.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 2);
		position.setX(qBound(available.left(), position.x(), available.right() - width()));
	}
	move(position);
	show();
	search->setFocus(Qt::PopupFocusReason);
}

void ActionPicker::applyFilter(const QString &text)
{
	const QStringList words = text.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	QTreeWidgetItem *firstVisible = nullptr;

	for (int c = 0; c < list->topLevelItemCount(); c++) {
		QTreeWidgetItem *category = list->topLevelItem(c);
		bool anyShown = false;
		for (int i = 0; i < category->childCount(); i++) {
			QTreeWidgetItem *item = category->child(i);
			QString haystack = item->data(0, kSearchRole).toString();
			bool match = true;
			for (const QString &word : words) {
				if (!haystack.contains(word)) {
					match = false;
					break;
				}
			}
			item->setHidden(!match);
			if (match) {
				anyShown = true;
				if (!firstVisible)
					firstVisible = item;
			}
		}
		category->setHidden(!anyShown);
	}

	// While typing, the best match is the one Enter would pick.
	if (!words.isEmpty() || !list->currentItem() || list->currentItem()->isHidden())
		list->setCurrentItem(firstVisible);
}

void ActionPicker::moveSelection(int step)
{
	QList<QTreeWidgetItem *> visible;
	for (QTreeWidgetItemIterator it(list); *it; ++it) {
		if ((*it)->parent() && !(*it)->isHidden() && !(*it)->parent()->isHidden())
			visible << *it;
	}
	if (visible.isEmpty())
		return;

	int index = visible.indexOf(list->currentItem());
	index = index < 0 ? 0 : qBound(0, index + step, static_cast<int>(visible.size()) - 1);
	list->setCurrentItem(visible[index]);
	list->scrollToItem(visible[index]);
}

void ActionPicker::choose(QTreeWidgetItem *item)
{
	if (!item || !item->parent())
		return;
	QString choice = item->data(0, kChoiceRole).toString();
	hide();
	emit chosen(choice);
}

bool ActionPicker::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == search && event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		switch (key->key()) {
		case Qt::Key_Down:
			moveSelection(1);
			return true;
		case Qt::Key_Up:
			moveSelection(-1);
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			choose(list->currentItem());
			return true;
		case Qt::Key_Escape:
			hide();
			return true;
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

// ============================================================================
// ButtonEditor
// ============================================================================

ButtonEditor::ButtonEditor(std::shared_ptr<ButtonConfig> config, const BarStyle &barStyle, bool groupAllowed,
			   QWidget *parent)
	: QWidget(parent),
	  buttonConfig(config),
	  style(barStyle),
	  allowGroup(groupAllowed),
	  spacerMode(config && config->isSpacer()),
	  dividerMode(config && config->isDivider()),
	  displayEntryMode(config && config->isDisplay())
{
	// A readout is part of the bar rather than a button, so it never becomes
	// a group whatever the caller asked for.
	if (displayEntryMode)
		allowGroup = false;

	// Filling the fields in from the entry must not write them straight
	// back to it, half-filled.
	populating = true;

	if (spacerMode) {
		setupSpacerUI();
	} else if (dividerMode) {
		setupDividerUI();
	} else if (displayEntryMode) {
		setupUI();
	} else {
		setupUI();
		populateSources();
		populateScenes();
		populateAudioSources();
	}

	populateFromConfig();

	// Populating the action combo may not have changed its index, so make
	// sure the group hint reflects the final state either way.
	if (allowGroup && groupCheck)
		onGroupToggled(groupCheck->isChecked());
	fitStackToCurrent(actionStack);

	populating = false;
}

QLabel *ButtonEditor::createHeader()
{
	const char *key = "OmniBar.Inspector.Button";
	if (spacerMode)
		key = "OmniBar.Inspector.Spacer";
	else if (dividerMode)
		key = "OmniBar.Inspector.Divider";
	else if (displayEntryMode)
		key = "OmniBar.Inspector.Display";
	else if (buttonConfig && buttonConfig->isGroup)
		key = "OmniBar.Inspector.Group";

	headerLabel = new QLabel(obs_module_text(key));
	QFont font = headerLabel->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 1.2);
	headerLabel->setFont(font);
	return headerLabel;
}

// A heading over one part of the inspector, in the small capitals used for
// section labels across OBS.
static QLabel *sectionHeading(const char *key)
{
	QLabel *heading = new QLabel(QString::fromUtf8(obs_module_text(key)).toUpper());
	QFont font = heading->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 0.85);
	font.setLetterSpacing(QFont::PercentageSpacing, 108);
	heading->setFont(font);
	heading->setForegroundRole(QPalette::PlaceholderText);
	heading->setContentsMargins(0, 10, 0, 2);
	return heading;
}

void ButtonEditor::focusFirstField()
{
	QWidget *first = labelEdit ? static_cast<QWidget *>(labelEdit)
				   : (spacerWidthSpin ? static_cast<QWidget *>(spacerWidthSpin) : dividerThicknessSpin);
	if (pickerButton)
		first = pickerButton;
	else if (displayMetricCombo)
		first = displayMetricCombo;
	if (first)
		first->setFocus(Qt::TabFocusReason);
}

void ButtonEditor::setupDividerUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->addWidget(createHeader());

	QFormLayout *form = new QFormLayout();

	dividerThicknessSpin = new QSpinBox();
	dividerThicknessSpin->setRange(1, 6);
	dividerThicknessSpin->setValue(1);
	dividerThicknessSpin->setSuffix(" px");
	connect(dividerThicknessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&ButtonEditor::onDividerValueChanged);
	form->addRow(obs_module_text("OmniBar.Settings.DividerThickness"), dividerThicknessSpin);

	dividerLengthSpin = new QSpinBox();
	dividerLengthSpin->setRange(10, 100);
	dividerLengthSpin->setValue(70);
	dividerLengthSpin->setSuffix(" %");
	connect(dividerLengthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&ButtonEditor::onDividerValueChanged);
	form->addRow(obs_module_text("OmniBar.Settings.DividerLength"), dividerLengthSpin);

	QHBoxLayout *colorLayout = new QHBoxLayout();
	dividerColorCheck = new QCheckBox(obs_module_text("OmniBar.Settings.UseCustomColor"));
	connect(dividerColorCheck, &QCheckBox::toggled, this, [this](bool enabled) {
		if (enabled && !dividerColor.isValid())
			dividerColor = style.effectiveButtonBorder();
		dividerColorButton->setEnabled(enabled);
		setColorSwatch(dividerColorButton, enabled ? dividerColor : QColor());
		onDividerValueChanged();
	});
	colorLayout->addWidget(dividerColorCheck);

	dividerColorButton = new QPushButton();
	dividerColorButton->setEnabled(false);
	connect(dividerColorButton, &QPushButton::clicked, this, &ButtonEditor::onDividerColorClicked);
	colorLayout->addWidget(dividerColorButton, 1);
	form->addRow(obs_module_text("OmniBar.Settings.DividerColor"), colorLayout);

	showConditionEditor = new ConditionEditor(false);
	connect(showConditionEditor, &ConditionEditor::changed, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);

	mainLayout->addLayout(form);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.DividerHint"));
	hint->setWordWrap(true);
	mainLayout->addWidget(hint);
	mainLayout->addStretch();
}

void ButtonEditor::onDividerValueChanged()
{
	// The bar preview above draws the divider itself.
	refreshPreview();
}

void ButtonEditor::onDividerColorClicked()
{
	QColor initial = dividerColor.isValid() ? dividerColor : style.effectiveButtonBorder();
	QColor chosen = QColorDialog::getColor(initial, this, obs_module_text("OmniBar.Settings.DividerColor"),
					       QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;

	dividerColor = chosen;
	setColorSwatch(dividerColorButton, dividerColor);
	onDividerValueChanged();
}

void ButtonEditor::setupSpacerUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->addWidget(createHeader());

	QFormLayout *form = new QFormLayout();
	spacerWidthSpin = new QSpinBox();
	spacerWidthSpin->setRange(1, 400);
	spacerWidthSpin->setValue(10);
	spacerWidthSpin->setSuffix(" px");
	form->addRow(obs_module_text("OmniBar.Settings.SpacerWidth"), spacerWidthSpin);

	spacerExpandCheck = new QCheckBox(obs_module_text("OmniBar.Settings.SpacerExpand"));
	spacerExpandCheck->setToolTip(obs_module_text("OmniBar.Settings.SpacerExpandHint"));
	form->addRow(QString(), spacerExpandCheck);
	connect(spacerWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ButtonEditor::refreshPreview);
	connect(spacerExpandCheck, &QCheckBox::toggled, this, &ButtonEditor::refreshPreview);

	showConditionEditor = new ConditionEditor(false);
	connect(showConditionEditor, &ConditionEditor::changed, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);
	mainLayout->addLayout(form);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.SpacerHint"));
	hint->setWordWrap(true);
	mainLayout->addWidget(hint);
	mainLayout->addStretch();
}

void ButtonEditor::setupUI()
{
	// One column of sections rather than tabs: the inspector scrolls, and
	// every setting stays one glance away. What the entry does comes first,
	// since the automatic icon and label both follow from it.
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(2);
	mainLayout->addWidget(createHeader());

	auto addSection = [mainLayout](const char *headingKey, QWidget *page) {
		if (page->layout())
			page->layout()->setContentsMargins(0, 0, 0, 0);
		mainLayout->addWidget(sectionHeading(headingKey));
		mainLayout->addWidget(page);
	};

	addSection(displayEntryMode ? "OmniBar.Settings.TabValue" : "OmniBar.Settings.TabAction", createActionTab());
	addSection("OmniBar.Settings.TabAppearance", createAppearanceTab());
	addSection("OmniBar.Settings.TabVisibility", createVisibilityTab());
	if (allowGroup)
		addSection("OmniBar.Settings.TabGroup", createGroupTab());
	mainLayout->addStretch();
}

// The built-in set is large enough that a flat list is hard to scan, so it is
// broken up by unselectable section headers with each entry showing its glyph.
void ButtonEditor::buildIconCombo()
{
	QStandardItemModel *model = new QStandardItemModel(iconCombo);
	QColor tint = palette().color(QPalette::Text);

	auto addHeader = [&](const char *labelKey) {
		QStandardItem *item = new QStandardItem(obs_module_text(labelKey));
		item->setFlags(Qt::NoItemFlags);
		QFont font = item->font();
		font.setBold(true);
		item->setFont(font);
		model->appendRow(item);
	};

	auto addEntry = [&](const char *labelKey, const QString &file, bool withThumbnail) {
		QStandardItem *item = new QStandardItem(obs_module_text(labelKey));
		item->setData(file, Qt::UserRole);
		if (withThumbnail) {
			auto sample = std::make_shared<ButtonConfig>();
			sample->iconPath = file;
			sample->action = std::make_unique<NoAction>();
			item->setIcon(omniBarIconForConfig(sample, tint, 16));
		}
		model->appendRow(item);
	};

	addEntry("OmniBar.Icon.Auto", QString(), false);

	addHeader("OmniBar.IconGroup.Streaming");
	addEntry("OmniBar.Icon.Stream", "stream.svg", true);
	addEntry("OmniBar.Icon.StreamStart", "stream-start.svg", true);
	addEntry("OmniBar.Icon.StreamStop", "stream-stop.svg", true);

	addHeader("OmniBar.IconGroup.Recording");
	addEntry("OmniBar.Icon.Record", "record.svg", true);
	addEntry("OmniBar.Icon.RecordStart", "record-start.svg", true);
	addEntry("OmniBar.Icon.RecordStop", "record-stop.svg", true);
	addEntry("OmniBar.Icon.PauseToggle", "pause-toggle.svg", true);
	addEntry("OmniBar.Icon.Pause", "pause.svg", true);
	addEntry("OmniBar.Icon.Play", "play.svg", true);

	addHeader("OmniBar.IconGroup.Replay");
	addEntry("OmniBar.Icon.Replay", "replay.svg", true);
	addEntry("OmniBar.Icon.ReplayStart", "replay-start.svg", true);
	addEntry("OmniBar.Icon.ReplayStop", "replay-stop.svg", true);
	addEntry("OmniBar.Icon.SaveReplay", "save-replay.svg", true);

	addHeader("OmniBar.IconGroup.Camera");
	addEntry("OmniBar.Icon.VirtualCam", "virtual-cam.svg", true);
	addEntry("OmniBar.Icon.VirtualCamStart", "virtual-cam-start.svg", true);
	addEntry("OmniBar.Icon.VirtualCamStop", "virtual-cam-stop.svg", true);

	addHeader("OmniBar.IconGroup.Studio");
	addEntry("OmniBar.Icon.StudioMode", "studio-mode.svg", true);
	addEntry("OmniBar.Icon.StudioModeOn", "studio-mode-on.svg", true);
	addEntry("OmniBar.Icon.StudioModeOff", "studio-mode-off.svg", true);
	addEntry("OmniBar.Icon.Transition", "transition.svg", true);

	addHeader("OmniBar.IconGroup.Scenes");
	addEntry("OmniBar.Icon.Scene", "scene.svg", true);

	addHeader("OmniBar.IconGroup.Audio");
	addEntry("OmniBar.Icon.Audio", "audio.svg", true);
	addEntry("OmniBar.Icon.AudioMute", "audio-mute.svg", true);

	addHeader("OmniBar.IconGroup.Sources");
	addEntry("OmniBar.Icon.Visibility", "visibility.svg", true);
	addEntry("OmniBar.Icon.Filter", "filter.svg", true);
	addEntry("OmniBar.Icon.Hotkey", "hotkey.svg", true);

	addHeader("OmniBar.IconGroup.Other");
	addEntry("OmniBar.Icon.Group", "group.svg", true);
	addEntry("OmniBar.Icon.None", "none.svg", true);
	addEntry("OmniBar.Icon.Display", "display.svg", true);
	addEntry("OmniBar.Icon.Divider", "divider.svg", true);
	addEntry("OmniBar.Icon.Spacer", "spacer.svg", true);
	addEntry("OmniBar.Icon.Custom", "custom", false);

	iconCombo->setModel(model);
	iconCombo->setIconSize(QSize(16, 16));
	// The popup would otherwise inherit the combo's width and elide the
	// longer entries.
	iconCombo->view()->setMinimumWidth(260);
}

// Grouped the same way the icon list is, so a readout is found by what it
// belongs to rather than by reading seventeen names in a row.
void ButtonEditor::buildMetricCombo()
{
	QStandardItemModel *model = new QStandardItemModel(displayMetricCombo);

	auto addHeader = [&](const char *labelKey) {
		QStandardItem *item = new QStandardItem(obs_module_text(labelKey));
		item->setFlags(Qt::NoItemFlags);
		QFont font = item->font();
		font.setBold(true);
		item->setFont(font);
		model->appendRow(item);
	};

	auto addEntry = [&](DisplayMetric metric) {
		QStandardItem *item = new QStandardItem(OmniBarMetrics::label(metric));
		item->setData(OmniBarMetrics::name(metric), Qt::UserRole);
		model->appendRow(item);
	};

	addHeader("OmniBar.MetricGroup.Status");
	addEntry(DisplayMetric::Status);

	addHeader("OmniBar.MetricGroup.Streaming");
	addEntry(DisplayMetric::StreamTime);
	addEntry(DisplayMetric::StreamBitrate);
	addEntry(DisplayMetric::StreamDropped);

	addHeader("OmniBar.MetricGroup.Recording");
	addEntry(DisplayMetric::RecordTime);
	addEntry(DisplayMetric::RecordBitrate);
	addEntry(DisplayMetric::RecordSize);
	addEntry(DisplayMetric::DiskFree);

	addHeader("OmniBar.MetricGroup.Buffers");
	addEntry(DisplayMetric::ReplayTime);
	addEntry(DisplayMetric::ReplayLastSaved);
	addEntry(DisplayMetric::VirtualCamTime);

	addHeader("OmniBar.MetricGroup.Performance");
	addEntry(DisplayMetric::CpuUsage);
	addEntry(DisplayMetric::Fps);
	addEntry(DisplayMetric::FrameTime);
	addEntry(DisplayMetric::RenderLag);
	addEntry(DisplayMetric::EncodingLag);
	addEntry(DisplayMetric::Memory);
	addEntry(DisplayMetric::Clock);

	displayMetricCombo->setModel(model);
	displayMetricCombo->view()->setMinimumWidth(240);
	// Row zero is a heading, which is not something to sit on.
	displayMetricCombo->setCurrentIndex(
		displayMetricCombo->findData(OmniBarMetrics::name(DisplayMetric::StreamTime)));
}

QWidget *ButtonEditor::createAppearanceTab()
{
	QWidget *page = new QWidget();
	QFormLayout *form = new QFormLayout(page);

	labelEdit = new QLineEdit();
	labelEdit->setPlaceholderText(obs_module_text(displayEntryMode ? "OmniBar.Settings.PrefixPlaceholder"
								       : "OmniBar.Settings.LabelPlaceholder"));
	connect(labelEdit, &QLineEdit::textChanged, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text(displayEntryMode ? "OmniBar.Settings.Prefix" : "OmniBar.Settings.Label"),
		     labelEdit);

	displayModeCombo = new QComboBox();
	// A readout with its text hidden would have nothing left to show, so
	// icon-only is not on offer for one.
	if (!displayEntryMode)
		displayModeCombo->addItem(obs_module_text("OmniBar.Display.IconOnly"),
					  static_cast<int>(ButtonDisplayMode::IconOnly));
	displayModeCombo->addItem(obs_module_text("OmniBar.Display.TextOnly"),
				  static_cast<int>(ButtonDisplayMode::TextOnly));
	displayModeCombo->addItem(obs_module_text("OmniBar.Display.TextLeft"),
				  static_cast<int>(ButtonDisplayMode::TextLeft));
	displayModeCombo->addItem(obs_module_text("OmniBar.Display.TextRight"),
				  static_cast<int>(ButtonDisplayMode::TextRight));
	displayModeCombo->addItem(obs_module_text("OmniBar.Display.TextAbove"),
				  static_cast<int>(ButtonDisplayMode::TextAbove));
	displayModeCombo->addItem(obs_module_text("OmniBar.Display.TextBelow"),
				  static_cast<int>(ButtonDisplayMode::TextBelow));
	connect(displayModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.DisplayMode"), displayModeCombo);

	tooltipEdit = new QLineEdit();
	connect(tooltipEdit, &QLineEdit::textChanged, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.Tooltip"), tooltipEdit);

	// Icon selection
	QHBoxLayout *iconLayout = new QHBoxLayout();
	iconCombo = new QComboBox();
	buildIconCombo();
	connect(iconCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditor::onIconChanged);
	iconLayout->addWidget(iconCombo, 1);

	iconBrowseButton = new QPushButton("...");
	iconBrowseButton->setFixedWidth(32);
	connect(iconBrowseButton, &QPushButton::clicked, this, &ButtonEditor::onIconBrowse);
	iconLayout->addWidget(iconBrowseButton);

	iconPreview = new QLabel();
	iconPreview->setFixedSize(32, 32);
	iconPreview->setAlignment(Qt::AlignCenter);
	iconPreview->setStyleSheet("QLabel { border: 1px solid palette(mid); background-color: palette(base); }");
	iconLayout->addWidget(iconPreview);

	form->addRow(obs_module_text("OmniBar.Settings.Icon"), iconLayout);

	// Bundled icons always follow the bar's text colour; a custom file is
	// left alone unless the user asks for it to be tinted.
	tintIconCheck = new QCheckBox(obs_module_text("OmniBar.Settings.TintIcon"));
	tintIconCheck->setToolTip(obs_module_text("OmniBar.Settings.TintIconHint"));
	connect(tintIconCheck, &QCheckBox::toggled, this, &ButtonEditor::refreshPreview);
	connect(tintIconCheck, &QCheckBox::toggled, this, &ButtonEditor::updateIconPreview);
	form->addRow(QString(), tintIconCheck);

	// Per-button accent override
	QHBoxLayout *colorLayout = new QHBoxLayout();
	customColorCheck = new QCheckBox(obs_module_text("OmniBar.Settings.UseCustomColor"));
	connect(customColorCheck, &QCheckBox::toggled, this, [this](bool enabled) {
		if (enabled && !customColor.isValid())
			customColor = style.effectiveButtonChecked();
		if (customColorButton)
			customColorButton->setEnabled(enabled);
		updateColorButton();
		refreshPreview();
	});
	colorLayout->addWidget(customColorCheck);

	customColorButton = new QPushButton();
	customColorButton->setEnabled(false);
	connect(customColorButton, &QPushButton::clicked, this, &ButtonEditor::onCustomColorClicked);
	colorLayout->addWidget(customColorButton, 1);

	form->addRow(obs_module_text(displayEntryMode ? "OmniBar.Settings.TextColour" : "OmniBar.Settings.AccentColor"),
		     colorLayout);

	QLabel *colorHint = new QLabel(obs_module_text(displayEntryMode ? "OmniBar.Settings.TextColourHint"
									: "OmniBar.Settings.AccentColorHint"));
	colorHint->setWordWrap(true);
	form->addRow(QString(), colorHint);

	// Nothing to breathe with: a readout has no active state of its own.
	if (displayEntryMode)
		return page;

	pulseCheck = new QCheckBox(obs_module_text("OmniBar.Settings.PulseWhenActive"));
	connect(pulseCheck, &QCheckBox::toggled, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.Pulse"), pulseCheck);

	QLabel *pulseHint = new QLabel(obs_module_text("OmniBar.Settings.PulseHint"));
	pulseHint->setWordWrap(true);
	form->addRow(QString(), pulseHint);

	return page;
}

// When the entry is on the bar at all. Behaviour rather than looks, so it has a
// tab of its own instead of sitting among the colours.
QWidget *ButtonEditor::createVisibilityTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	QFormLayout *form = new QFormLayout();
	showConditionEditor = new ConditionEditor(true);
	connect(showConditionEditor, &ConditionEditor::changed, this, &ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);
	layout->addLayout(form);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.ShowConditionHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addStretch();
	return page;
}

QWidget *ButtonEditor::createActionTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	// A readout has one thing to choose - what it reads - so it gets this tab
	// to itself rather than a type picker over a stack of one.
	if (displayEntryMode) {
		QFormLayout *displayForm = new QFormLayout();

		displayMetricCombo = new QComboBox();
		buildMetricCombo();
		connect(displayMetricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&ButtonEditor::refreshPreview);
		displayForm->addRow(obs_module_text("OmniBar.Settings.DisplayMetric"), displayMetricCombo);

		// Only the clock has an hour to write, so the choice is put away
		// again as soon as the readout is showing anything else.
		displayClockFormatCombo = new QComboBox();
		displayClockFormatCombo->addItem(OmniBarMetrics::clockFormatLabel(ClockFormat::Hours24),
						 OmniBarMetrics::clockFormatName(ClockFormat::Hours24));
		displayClockFormatCombo->addItem(OmniBarMetrics::clockFormatLabel(ClockFormat::Hours12),
						 OmniBarMetrics::clockFormatName(ClockFormat::Hours12));
		connect(displayClockFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&ButtonEditor::refreshPreview);

		QLabel *clockFormatLabel = new QLabel(obs_module_text("OmniBar.Settings.ClockFormat"));
		displayForm->addRow(clockFormatLabel, displayClockFormatCombo);

		auto syncClockRow = [this, clockFormatLabel]() {
			bool isClock = displayMetricCombo->currentData().toString() ==
				       OmniBarMetrics::name(DisplayMetric::Clock);
			clockFormatLabel->setVisible(isClock);
			displayClockFormatCombo->setVisible(isClock);
		};
		connect(displayMetricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncClockRow);
		syncClockRow();

		displayWidthSpin = new QSpinBox();
		displayWidthSpin->setRange(0, 400);
		displayWidthSpin->setSuffix(" px");
		displayWidthSpin->setSpecialValueText(obs_module_text("OmniBar.Settings.DisplayWidthAuto"));
		displayWidthSpin->setToolTip(obs_module_text("OmniBar.Settings.DisplayWidthHint"));
		connect(displayWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
			&ButtonEditor::refreshPreview);
		displayForm->addRow(obs_module_text("OmniBar.Settings.DisplayWidth"), displayWidthSpin);

		// Only some readings have a level worth warning about.
		displayWarnCheck = new QCheckBox(obs_module_text("OmniBar.Settings.DisplayWarn"));
		displayWarnCheck->setToolTip(obs_module_text("OmniBar.Settings.DisplayWarnHint"));
		connect(displayWarnCheck, &QCheckBox::toggled, this, &ButtonEditor::refreshPreview);
		displayForm->addRow(QString(), displayWarnCheck);
		auto syncWarn = [this]() {
			displayWarnCheck->setEnabled(OmniBarMetrics::hasThresholds(
				OmniBarMetrics::fromName(displayMetricCombo->currentData().toString())));
		};
		connect(displayMetricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncWarn);
		syncWarn();

		layout->addLayout(displayForm);
		layout->addStretch();
		return page;
	}

	// What the button does, picked from a searchable list rather than a
	// type and then a second list of that type's actions.
	QFormLayout *typeForm = new QFormLayout();
	pickerButton = new QPushButton();
	pickerButton->setAutoDefault(false);
	// Reads as a field holding the choice, so its text sits at the start.
	pickerButton->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 4px 8px; }"));
	pickerButton->setIconSize(QSize(16, 16));
	pickerButton->setToolTip(obs_module_text("OmniBar.Picker.Tooltip"));
	picker = new ActionPicker(this);
	connect(pickerButton, &QPushButton::clicked, this, [this]() { picker->popup(pickerButton, currentChoice); });
	connect(picker, &ActionPicker::chosen, this, &ButtonEditor::setChoice);
	typeForm->addRow(obs_module_text("OmniBar.Settings.Action"), pickerButton);
	layout->addLayout(typeForm);

	// The settings each kind needs, one page per kind. The order matches
	// pageForChoice below.
	actionStack = new QStackedWidget();

	// No action
	QWidget *nonePage = new QWidget();
	QVBoxLayout *noneLayout = new QVBoxLayout(nonePage);
	QLabel *noneHint = new QLabel(obs_module_text("OmniBar.Settings.NoActionHint"));
	noneHint->setWordWrap(true);
	noneLayout->addWidget(noneHint);
	noneLayout->addStretch();
	actionStack->addWidget(nonePage);

	// OBS's own actions: the variant within the family, spelled out below.
	QWidget *frontendPage = new QWidget();
	QVBoxLayout *frontendLayout = new QVBoxLayout(frontendPage);
	variantRow = new QWidget();
	variantRow->setStyleSheet(QString::fromLatin1(kSegmentSheet));
	variantLayout = new QHBoxLayout(variantRow);
	variantLayout->setContentsMargins(0, 0, 0, 0);
	variantLayout->setSpacing(4);
	variantLayout->addWidget(new QLabel(obs_module_text("OmniBar.Settings.Variant")));
	variantGroup = new QButtonGroup(this);
	variantGroup->setExclusive(true);
	connect(variantGroup, &QButtonGroup::idClicked, this, [this]() {
		updatePickerButton();
		refreshPreview();
	});
	frontendLayout->addWidget(variantRow);
	frontendDescription = new QLabel();
	frontendDescription->setForegroundRole(QPalette::PlaceholderText);
	frontendLayout->addWidget(frontendDescription);
	actionStack->addWidget(frontendPage);

	// Source hotkey
	QWidget *hotkeyPage = new QWidget();
	QFormLayout *hotkeyForm = new QFormLayout(hotkeyPage);
	hotkeySourceCombo = new QComboBox();
	hotkeyCombo = new QComboBox();
	connect(hotkeySourceCombo, &QComboBox::currentTextChanged, this, &ButtonEditor::onHotkeySourceChanged);
	connect(hotkeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditor::refreshPreview);
	hotkeyForm->addRow(obs_module_text("OmniBar.Settings.Source"), hotkeySourceCombo);
	hotkeyForm->addRow(obs_module_text("OmniBar.Settings.Hotkey"), hotkeyCombo);
	QLabel *hotkeyHint = new QLabel(obs_module_text("OmniBar.Settings.HotkeyHint"));
	hotkeyHint->setWordWrap(true);
	hotkeyForm->addRow(QString(), hotkeyHint);
	actionStack->addWidget(hotkeyPage);

	// Source filter
	QWidget *filterPage = new QWidget();
	QFormLayout *filterForm = new QFormLayout(filterPage);
	filterSourceCombo = new QComboBox();
	filterCombo = new QComboBox();
	connect(filterSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this]() { onFilterSourceChanged(filterSourceCombo->currentData().toString()); });
	connect(filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditor::refreshPreview);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Source"), filterSourceCombo);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Filter"), filterCombo);
	actionStack->addWidget(filterPage);

	// Source visibility
	QWidget *visibilityPage = new QWidget();
	QFormLayout *visibilityForm = new QFormLayout(visibilityPage);
	visibilitySceneCombo = new QComboBox();
	visibilitySourceCombo = new QComboBox();
	connect(visibilitySceneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this]() { onSceneChanged(visibilitySceneCombo->currentData().toString()); });
	connect(visibilitySourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditor::refreshPreview);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Scene"), visibilitySceneCombo);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Source"), visibilitySourceCombo);
	actionStack->addWidget(visibilityPage);

	// Scene
	QWidget *scenePage = new QWidget();
	QFormLayout *sceneForm = new QFormLayout(scenePage);
	sceneCombo = new QComboBox();
	connect(sceneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditor::refreshPreview);
	sceneForm->addRow(obs_module_text("OmniBar.Settings.Scene"), sceneCombo);
	QLabel *sceneHint = new QLabel(obs_module_text("OmniBar.Settings.SceneHint"));
	sceneHint->setWordWrap(true);
	sceneForm->addRow(QString(), sceneHint);
	actionStack->addWidget(scenePage);

	// Mute
	QWidget *mutePage = new QWidget();
	QFormLayout *muteForm = new QFormLayout(mutePage);
	audioSourceCombo = new QComboBox();
	connect(audioSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditor::refreshPreview);
	muteForm->addRow(obs_module_text("OmniBar.Settings.Source"), audioSourceCombo);
	QWidget *muteModes = new QWidget();
	muteModes->setStyleSheet(QString::fromLatin1(kSegmentSheet));
	QHBoxLayout *muteModeLayout = new QHBoxLayout(muteModes);
	muteModeLayout->setContentsMargins(0, 0, 0, 0);
	muteModeLayout->setSpacing(4);
	muteModeGroup = new QButtonGroup(this);
	muteModeGroup->setExclusive(true);
	const std::pair<MuteMode, const char *> modes[] = {{MuteMode::Toggle, "OmniBar.Variant.Toggle"},
							   {MuteMode::Mute, "OmniBar.Variant.Mute"},
							   {MuteMode::Unmute, "OmniBar.Variant.Unmute"}};
	for (const auto &mode : modes) {
		QPushButton *button = new QPushButton(obs_module_text(mode.second));
		button->setCheckable(true);
		button->setAutoDefault(false);
		muteModeGroup->addButton(button, static_cast<int>(mode.first));
		muteModeLayout->addWidget(button);
	}
	muteModeLayout->addStretch();
	muteModeGroup->button(static_cast<int>(MuteMode::Toggle))->setChecked(true);
	connect(muteModeGroup, &QButtonGroup::idClicked, this, &ButtonEditor::refreshPreview);
	muteForm->addRow(obs_module_text("OmniBar.Settings.Variant"), muteModes);
	actionStack->addWidget(mutePage);

	layout->addWidget(actionStack, 1);

	QFormLayout *confirmForm = new QFormLayout();
	confirmCombo = new QComboBox();
	confirmCombo->addItem(obs_module_text("OmniBar.Confirm.FollowObs"), static_cast<int>(ConfirmMode::FollowObs));
	confirmCombo->addItem(obs_module_text("OmniBar.Confirm.Always"), static_cast<int>(ConfirmMode::Always));
	confirmCombo->addItem(obs_module_text("OmniBar.Confirm.Never"), static_cast<int>(ConfirmMode::Never));
	confirmCombo->setToolTip(obs_module_text("OmniBar.Confirm.Hint"));
	connect(confirmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditor::refreshPreview);
	confirmForm->addRow(obs_module_text("OmniBar.Settings.Confirm"), confirmCombo);
	layout->addLayout(confirmForm);
	return page;
}

QWidget *ButtonEditor::createGroupTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	groupCheck = new QCheckBox(obs_module_text("OmniBar.Settings.IsGroup"));
	connect(groupCheck, &QCheckBox::toggled, this, &ButtonEditor::onGroupToggled);
	layout->addWidget(groupCheck);

	groupSettings = new QWidget();
	QVBoxLayout *settingsLayout = new QVBoxLayout(groupSettings);
	settingsLayout->setContentsMargins(0, 0, 0, 0);

	QFormLayout *form = new QFormLayout();
	groupDisplayCombo = new QComboBox();
	groupDisplayCombo->addItem(obs_module_text("OmniBar.Group.Display.Flyout"),
				   static_cast<int>(GroupDisplayMode::Flyout));
	groupDisplayCombo->addItem(obs_module_text("OmniBar.Group.Display.Inline"),
				   static_cast<int>(GroupDisplayMode::Inline));
	form->addRow(obs_module_text("OmniBar.Settings.GroupDisplay"), groupDisplayCombo);

	groupExpandCombo = new QComboBox();
	groupExpandCombo->addItem(obs_module_text("OmniBar.Group.Expand.Click"),
				  static_cast<int>(GroupExpandMode::Click));
	groupExpandCombo->addItem(obs_module_text("OmniBar.Group.Expand.WhenActive"),
				  static_cast<int>(GroupExpandMode::WhenActive));
	groupExpandCombo->addItem(obs_module_text("OmniBar.Group.Expand.Hover"),
				  static_cast<int>(GroupExpandMode::Hover));
	connect(groupExpandCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this]() { onGroupToggled(groupCheck->isChecked()); });
	connect(groupDisplayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditor::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.GroupExpand"), groupExpandCombo);

	groupConditionEditor = new ConditionEditor(true);
	connect(groupConditionEditor, &ConditionEditor::changed, this,
		[this]() { onGroupToggled(groupCheck->isChecked()); });
	form->addRow(obs_module_text("OmniBar.Settings.GroupCondition"), groupConditionEditor);
	settingsLayout->addLayout(form);

	groupHint = new QLabel();
	groupHint->setWordWrap(true);
	settingsLayout->addWidget(groupHint);

	// The children are the list's business: they are dragged in and out of
	// the group there, and edited like any other entry.
	QLabel *childrenHint = new QLabel(obs_module_text("OmniBar.Settings.GroupChildrenHint"));
	childrenHint->setWordWrap(true);
	settingsLayout->addWidget(childrenHint);

	layout->addWidget(groupSettings, 1);
	return page;
}

void ButtonEditor::populateFromConfig()
{
	if (!buttonConfig)
		return;

	showConditionEditor->setCondition(buttonConfig->showCondition);

	if (spacerMode) {
		auto *spacerAction = dynamic_cast<SpacerAction *>(buttonConfig->action.get());
		spacerWidthSpin->setValue(spacerAction ? spacerAction->getWidth() : 10);
		spacerExpandCheck->setChecked(spacerAction && spacerAction->isExpanding());
		return;
	}

	if (dividerMode) {
		auto *divider = dynamic_cast<DividerAction *>(buttonConfig->action.get());
		if (divider) {
			dividerThicknessSpin->setValue(divider->getThickness());
			dividerLengthSpin->setValue(divider->getLengthPercent());
			dividerColor = divider->getCustomColor();
			dividerColorCheck->setChecked(divider->hasCustomColor());
			dividerColorButton->setEnabled(divider->hasCustomColor());
			setColorSwatch(dividerColorButton, divider->hasCustomColor() ? dividerColor : QColor());
		}
		onDividerValueChanged();
		return;
	}

	labelEdit->setText(buttonConfig->label);
	tooltipEdit->setText(buttonConfig->tooltip);

	int displayIndex = displayModeCombo->findData(static_cast<int>(buttonConfig->displayMode));
	displayModeCombo->setCurrentIndex(displayIndex >= 0 ? displayIndex : 0);

	customColor = buttonConfig->customColor;
	customColorCheck->setChecked(buttonConfig->useCustomColor && customColor.isValid());
	customColorButton->setEnabled(customColorCheck->isChecked());
	updateColorButton();

	if (pulseCheck) {
		pulseCheck->setChecked(buttonConfig->pulseWhenActive);
		pulseCheck->setEnabled(buttonConfig->hasAction());
	}

	// Icon: a bundled icon matches an entry by name, anything else is custom.
	int iconIndex = iconCombo->findData(buttonConfig->iconPath);
	bool isCustom = iconIndex < 0;
	if (isCustom) {
		customIconPath = buttonConfig->iconPath;
		iconCombo->setCurrentIndex(iconCombo->findData("custom"));
	} else {
		iconCombo->setCurrentIndex(iconIndex);
	}

	tintIconCheck->setChecked(buttonConfig->tintIcon);
	tintIconCheck->setEnabled(isCustom);
	updateIconPreview();

	if (allowGroup) {
		groupCheck->setChecked(buttonConfig->isGroup);
		int displayMode = groupDisplayCombo->findData(static_cast<int>(buttonConfig->groupDisplay));
		groupDisplayCombo->setCurrentIndex(displayMode >= 0 ? displayMode : 0);
		int expandMode = groupExpandCombo->findData(static_cast<int>(buttonConfig->groupExpand));
		groupExpandCombo->setCurrentIndex(expandMode >= 0 ? expandMode : 0);
		groupConditionEditor->setCondition(buttonConfig->expandCondition);
		onGroupToggled(groupCheck->isChecked());
	}

	if (!buttonConfig->action)
		return;

	if (displayEntryMode) {
		auto *display = dynamic_cast<DisplayAction *>(buttonConfig->action.get());
		if (display) {
			int metricIndex = displayMetricCombo->findData(OmniBarMetrics::name(display->getMetric()));
			if (metricIndex >= 0)
				displayMetricCombo->setCurrentIndex(metricIndex);
			int formatIndex = displayClockFormatCombo->findData(
				OmniBarMetrics::clockFormatName(display->getClockFormat()));
			if (formatIndex >= 0)
				displayClockFormatCombo->setCurrentIndex(formatIndex);
			displayWidthSpin->setValue(display->getMinimumWidth());
			displayWarnCheck->setChecked(display->getWarnColors());
		}
		return;
	}

	int confirmIndex = confirmCombo->findData(static_cast<int>(buttonConfig->confirmMode));
	confirmCombo->setCurrentIndex(confirmIndex >= 0 ? confirmIndex : 0);
	confirmCombo->setEnabled(buttonConfig->hasAction());

	ActionType type = buttonConfig->action->getType();

	switch (type) {
	case ActionType::SourceHotkey: {
		auto *hotkeyAction = dynamic_cast<SourceHotkeyAction *>(buttonConfig->action.get());
		if (hotkeyAction) {
			selectOrKeepMissing(hotkeySourceCombo, hotkeyAction->getSourceName());
			populateHotkeys(hotkeyAction->getSourceName());
			selectOrKeepMissing(hotkeyCombo, hotkeyAction->getHotkeyName());
		}
		break;
	}
	case ActionType::SourceFilter: {
		auto *filterAction = dynamic_cast<SourceFilterAction *>(buttonConfig->action.get());
		if (filterAction) {
			selectOrKeepMissing(filterSourceCombo, filterAction->getSourceName());
			populateFilters(filterAction->getSourceName());
			selectOrKeepMissing(filterCombo, filterAction->getFilterName());
		}
		break;
	}
	case ActionType::SourceVisibility: {
		auto *visAction = dynamic_cast<SourceVisibilityAction *>(buttonConfig->action.get());
		if (visAction) {
			selectOrKeepMissing(visibilitySceneCombo, visAction->getSceneName());
			populateSceneItems(visAction->getSceneName());
			selectOrKeepMissing(visibilitySourceCombo, visAction->getSourceName());
		}
		break;
	}
	case ActionType::Scene: {
		auto *sceneAction = dynamic_cast<SceneAction *>(buttonConfig->action.get());
		if (sceneAction)
			selectOrKeepMissing(sceneCombo, sceneAction->getSceneName());
		break;
	}
	case ActionType::AudioMute: {
		auto *muteAction = dynamic_cast<AudioMuteAction *>(buttonConfig->action.get());
		if (muteAction) {
			selectOrKeepMissing(audioSourceCombo, muteAction->getSourceName());
			if (QAbstractButton *mode = muteModeGroup->button(static_cast<int>(muteAction->getMode())))
				mode->setChecked(true);
		}
		break;
	}
	case ActionType::None:
	case ActionType::Frontend:
	case ActionType::Spacer:
	case ActionType::Divider:
	case ActionType::Display:
		break;
	}

	// Last, so the variant row can find the action's own variant.
	setChoice(choiceForAction(buttonConfig->action.get()));
}

void ButtonEditor::updateConfigFromUI(ButtonConfig &target)
{
	target.showCondition = showConditionEditor->condition();

	if (spacerMode) {
		target.action =
			std::make_unique<SpacerAction>(spacerWidthSpin->value(), spacerExpandCheck->isChecked());
		return;
	}

	if (dividerMode) {
		auto divider =
			std::make_unique<DividerAction>(dividerThicknessSpin->value(), dividerLengthSpin->value());
		if (dividerColorCheck->isChecked() && dividerColor.isValid())
			divider->setCustomColor(dividerColor);
		target.action = std::move(divider);
		return;
	}

	target.label = labelEdit->text();
	target.tooltip = tooltipEdit->text();
	target.displayMode = static_cast<ButtonDisplayMode>(displayModeCombo->currentData().toInt());
	target.tintIcon = tintIconCheck->isChecked();

	target.useCustomColor = customColorCheck->isChecked() && customColor.isValid();
	target.customColor = customColor;
	target.pulseWhenActive = pulseCheck && pulseCheck->isChecked();

	QString iconData = iconCombo->currentData().toString();
	target.iconPath = iconData == "custom" ? customIconPath : iconData;

	if (displayEntryMode) {
		target.isGroup = false;
		target.children.clear();
		auto display = std::make_unique<DisplayAction>(
			OmniBarMetrics::fromName(displayMetricCombo->currentData().toString()),
			displayWidthSpin->value());
		display->setClockFormat(
			OmniBarMetrics::clockFormatFromName(displayClockFormatCombo->currentData().toString()));
		display->setWarnColors(displayWarnCheck->isChecked());
		target.action = std::move(display);
		return;
	}

	if (allowGroup) {
		target.isGroup = groupCheck->isChecked();
		target.groupDisplay = static_cast<GroupDisplayMode>(groupDisplayCombo->currentData().toInt());
		target.groupExpand = static_cast<GroupExpandMode>(groupExpandCombo->currentData().toInt());
		target.expandCondition = groupConditionEditor->condition();
	}

	target.confirmMode = static_cast<ConfirmMode>(confirmCombo->currentData().toInt());

	switch (currentActionType()) {
	case ActionType::None:
		target.action = std::make_unique<NoAction>();
		break;
	case ActionType::Frontend:
		target.action = std::make_unique<FrontendAction>(currentFrontendType());
		break;
	case ActionType::SourceHotkey:
		target.action = std::make_unique<SourceHotkeyAction>(hotkeySourceCombo->currentData().toString(),
								     hotkeyCombo->currentData().toString());
		break;
	case ActionType::SourceFilter:
		target.action = std::make_unique<SourceFilterAction>(filterSourceCombo->currentData().toString(),
								     filterCombo->currentData().toString());
		break;
	case ActionType::SourceVisibility:
		target.action =
			std::make_unique<SourceVisibilityAction>(visibilitySceneCombo->currentData().toString(),
								 visibilitySourceCombo->currentData().toString());
		break;
	case ActionType::Scene:
		target.action = std::make_unique<SceneAction>(sceneCombo->currentData().toString());
		break;
	case ActionType::AudioMute:
		target.action = std::make_unique<AudioMuteAction>(audioSourceCombo->currentData().toString(),
								  static_cast<MuteMode>(muteModeGroup->checkedId()));
		break;
	case ActionType::Spacer:
	case ActionType::Divider:
	case ActionType::Display:
		// Not offered in the picker: each is added as its own kind of
		// entry and handled above.
		break;
	}
}

std::shared_ptr<ButtonConfig> ButtonEditor::previewConfig()
{
	auto config = std::make_shared<ButtonConfig>();
	config->id = buttonConfig ? buttonConfig->id : config->id;
	updateConfigFromUI(*config);
	return config;
}

void ButtonEditor::refreshPreview()
{
	if (populating || !buttonConfig)
		return;

	updateConfigFromUI(*buttonConfig);
	emit changed();
}

// Which page of the action stack a choice shows, in the order the pages were
// added in createActionTab.
static int pageForChoice(const QString &choice)
{
	if (!choiceFamily(choice).isEmpty())
		return 1;
	if (choice == QLatin1String("hotkey"))
		return 2;
	if (choice == QLatin1String("filter"))
		return 3;
	if (choice == QLatin1String("visibility"))
		return 4;
	if (choice == QLatin1String("scene"))
		return 5;
	if (choice == QLatin1String("mute"))
		return 6;
	return 0;
}

ActionType ButtonEditor::currentActionType() const
{
	switch (pageForChoice(currentChoice)) {
	case 1:
		return ActionType::Frontend;
	case 2:
		return ActionType::SourceHotkey;
	case 3:
		return ActionType::SourceFilter;
	case 4:
		return ActionType::SourceVisibility;
	case 5:
		return ActionType::Scene;
	case 6:
		return ActionType::AudioMute;
	default:
		return ActionType::None;
	}
}

FrontendActionType ButtonEditor::currentFrontendType() const
{
	const auto members = FrontendAction::familyMembers(choiceFamily(currentChoice));
	int checked = variantGroup ? variantGroup->checkedId() : -1;
	for (FrontendActionType type : members) {
		if (static_cast<int>(type) == checked)
			return type;
	}
	return members.isEmpty() ? FrontendActionType::ToggleStreaming : members.first();
}

void ButtonEditor::populateVariants()
{
	// Keep the variant that was showing if the family is the same, else the
	// entry's own, else the family's first - its toggle.
	const auto members = FrontendAction::familyMembers(choiceFamily(currentChoice));
	int keep = variantGroup->checkedId();
	if (keep < 0 || !members.contains(static_cast<FrontendActionType>(keep))) {
		keep = -1;
		auto *own = buttonConfig ? dynamic_cast<FrontendAction *>(buttonConfig->action.get()) : nullptr;
		if (own && members.contains(own->getActionType()))
			keep = static_cast<int>(own->getActionType());
	}

	for (QAbstractButton *button : variantGroup->buttons()) {
		variantGroup->removeButton(button);
		delete button;
	}
	// The stretch after the buttons goes too; it is put back below.
	while (variantLayout->count() > 1) {
		QLayoutItem *item = variantLayout->takeAt(1);
		delete item;
	}

	for (FrontendActionType type : members) {
		QPushButton *button = new QPushButton(obs_module_text(FrontendAction::info(type).variantKey));
		button->setCheckable(true);
		button->setAutoDefault(false);
		variantGroup->addButton(button, static_cast<int>(type));
		variantLayout->addWidget(button);
	}
	variantLayout->addStretch();

	if (QAbstractButton *selected = variantGroup->button(keep >= 0 ? keep : static_cast<int>(members.value(0))))
		selected->setChecked(true);

	// A family of one - Save replay, Transition - has nothing to choose.
	variantRow->setVisible(members.size() > 1);
}

void ButtonEditor::updatePickerButton()
{
	pickerButton->setText(ActionPicker::choiceLabel(currentChoice));
	pickerButton->setIcon(
		omniBarBundledIcon(ActionPicker::choiceIcon(currentChoice), palette().color(QPalette::ButtonText), 16));
	if (frontendDescription)
		frontendDescription->setText(FrontendAction::getFrontendActionLabel(currentFrontendType()));
}

void ButtonEditor::setChoice(const QString &choice)
{
	currentChoice = findChoice(choice) ? choice : QStringLiteral("none");

	actionStack->setCurrentIndex(pageForChoice(currentChoice));
	if (currentActionType() == ActionType::Frontend)
		populateVariants();
	fitStackToCurrent(actionStack);
	updatePickerButton();

	if (allowGroup && groupCheck)
		onGroupToggled(groupCheck->isChecked());
	// Nothing to pulse to, or to ask about, without an action of its own.
	bool hasAction = currentActionType() != ActionType::None;
	if (pulseCheck)
		pulseCheck->setEnabled(hasAction);
	if (confirmCombo)
		confirmCombo->setEnabled(hasAction);
	refreshPreview();
}

void ButtonEditor::onIconChanged(int index)
{
	Q_UNUSED(index);

	// Tinting only means anything for a file the plugin did not draw.
	tintIconCheck->setEnabled(iconCombo->currentData().toString() == "custom");

	updateIconPreview();
	refreshPreview();
}

void ButtonEditor::updateIconPreview()
{
	// Resolved through the working config so an automatic icon picks the same
	// glyph the bar would, and tinting matches the dialog's own text colour.
	QIcon icon = omniBarIconForConfig(previewConfig(), palette().color(QPalette::Text), 24);
	if (icon.isNull()) {
		iconPreview->clear();
		return;
	}
	iconPreview->setPixmap(icon.pixmap(24, 24));
}

void ButtonEditor::onIconBrowse()
{
	QString file = QFileDialog::getOpenFileName(this, obs_module_text("OmniBar.Settings.SelectIcon"), QString(),
						    "Images (*.png *.svg *.ico *.jpg *.jpeg);;All files (*.*)");
	if (file.isEmpty())
		return;

	customIconPath = file;
	iconCombo->setCurrentIndex(iconCombo->findData("custom"));
	updateIconPreview();
	refreshPreview();
}

void ButtonEditor::updateColorButton()
{
	setColorSwatch(customColorButton, customColorCheck && customColorCheck->isChecked() ? customColor : QColor());
}

void ButtonEditor::onCustomColorClicked()
{
	QColor initial = customColor.isValid() ? customColor : style.effectiveButtonChecked();
	QColor chosen = QColorDialog::getColor(initial, this, obs_module_text("OmniBar.Settings.AccentColor"),
					       QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;

	customColor = chosen;
	updateColorButton();
	refreshPreview();
}

void ButtonEditor::onGroupToggled(bool enabled)
{
	if (!groupSettings)
		return;

	groupSettings->setEnabled(enabled);
	if (headerLabel)
		headerLabel->setText(obs_module_text(enabled ? "OmniBar.Inspector.Group" : "OmniBar.Inspector.Button"));

	if (!enabled) {
		groupHint->clear();
		// Turning the group off is an edit like any other.
		refreshPreview();
		return;
	}

	auto expandMode = static_cast<GroupExpandMode>(groupExpandCombo->currentData().toInt());
	auto actionType = currentActionType();
	ActivationCondition condition = groupConditionEditor->condition();

	// Watching the button's own action needs there to be one.
	bool needsAction = condition.source == ConditionSource::OwnAction ||
			   (expandMode == GroupExpandMode::WhenActive && !condition.isSet());

	QStringList hints;

	if (needsAction && actionType == ActionType::None) {
		hints << obs_module_text("OmniBar.Settings.GroupNeedsAction");
	} else if (expandMode == GroupExpandMode::WhenActive) {
		hints << obs_module_text("OmniBar.Settings.GroupCondWhenActive");
	} else {
		if (expandMode == GroupExpandMode::Click && actionType != ActionType::None)
			hints << obs_module_text("OmniBar.Settings.GroupSplitHint");
		else
			hints << obs_module_text("OmniBar.Settings.GroupHint");

		// In these modes the condition is only a gate, which is worth
		// spelling out so a group that will not open is not a mystery.
		if (condition.isSet())
			hints << obs_module_text("OmniBar.Settings.GroupCondGate");
	}

	groupHint->setText(hints.join(" "));

	refreshPreview();
}

void ButtonEditor::onFilterSourceChanged(const QString &sourceName)
{
	populateFilters(sourceName);
	refreshPreview();
}

void ButtonEditor::onHotkeySourceChanged(const QString &sourceName)
{
	Q_UNUSED(sourceName);
	populateHotkeys(hotkeySourceCombo->currentData().toString());
	refreshPreview();
}

void ButtonEditor::onSceneChanged(const QString &sceneName)
{
	populateSceneItems(sceneName);
	refreshPreview();
}

void ButtonEditor::populateSources()
{
	hotkeySourceCombo->clear();
	filterSourceCombo->clear();

	// Hotkeys that belong to OBS rather than to a source.
	hotkeySourceCombo->addItem(obs_module_text("OmniBar.Settings.GlobalHotkeys"), QString());

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			ButtonEditor *dialog = static_cast<ButtonEditor *>(data);
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				dialog->hotkeySourceCombo->addItem(sourceName, sourceName);
				dialog->filterSourceCombo->addItem(sourceName, sourceName);
			}
			return true;
		},
		this);

	// Scenes accept filters and register hotkeys too.
	obs_enum_scenes(
		[](void *data, obs_source_t *source) {
			ButtonEditor *dialog = static_cast<ButtonEditor *>(data);
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				dialog->hotkeySourceCombo->addItem(sourceName, sourceName);
				dialog->filterSourceCombo->addItem(sourceName, sourceName);
			}
			return true;
		},
		this);

	populateHotkeys(QString());
}

void ButtonEditor::populateScenes()
{
	visibilitySceneCombo->clear();
	sceneCombo->clear();

	// A visibility button can follow whichever scene is on air, which is the
	// empty name.
	visibilitySceneCombo->addItem(obs_module_text("OmniBar.Settings.CurrentScene"), QString());

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *name = obs_source_get_name(scenes.sources.array[i]);
		if (name) {
			visibilitySceneCombo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			sceneCombo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
		}
	}
	obs_frontend_source_list_free(&scenes);
}

void ButtonEditor::populateAudioSources()
{
	audioSourceCombo->clear();

	// Only sources that make sound can be muted.
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			auto *combo = static_cast<QComboBox *>(data);
			const char *name = obs_source_get_name(source);
			if (name && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			return true;
		},
		audioSourceCombo);
}

void ButtonEditor::populateFilters(const QString &sourceName)
{
	filterCombo->clear();
	if (sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return;

	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *data) {
			QComboBox *combo = static_cast<QComboBox *>(data);
			const char *name = obs_source_get_name(filter);
			if (name) {
				combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			}
		},
		filterCombo);

	obs_source_release(source);
}

void ButtonEditor::populateSceneItems(const QString &sceneName)
{
	visibilitySourceCombo->clear();

	// Following the current scene offers what is in the scene on air now.
	obs_source_t *sceneSource = sceneName.isEmpty() ? obs_frontend_get_current_scene()
							: obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sceneSource)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return;
	}

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
			QComboBox *combo = static_cast<QComboBox *>(data);
			obs_source_t *source = obs_sceneitem_get_source(item);
			const char *name = obs_source_get_name(source);
			if (name) {
				combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			}
			return true;
		},
		visibilitySourceCombo);

	obs_source_release(sceneSource);
}

void ButtonEditor::populateHotkeys(const QString &sourceName)
{
	hotkeyCombo->clear();

	struct HotkeyFilter {
		QString source;
		QComboBox *combo;
	};

	HotkeyFilter filter{sourceName, hotkeyCombo};

	// Only offer the hotkeys the chosen source actually registered - the
	// full list is thousands of internal names that mean nothing here.
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			Q_UNUSED(id);
			HotkeyFilter *filter = static_cast<HotkeyFilter *>(data);

			if (SourceHotkeyAction::hotkeyOwnerName(key) != filter->source)
				return true;

			const char *name = obs_hotkey_get_name(key);
			if (!name)
				return true;

			const char *description = obs_hotkey_get_description(key);
			QString label = description && *description ? QString::fromUtf8(description)
								    : QString::fromUtf8(name);
			filter->combo->addItem(label, QString::fromUtf8(name));
			return true;
		},
		&filter);
}

// ============================================================================
// OmniBarConfig
// ============================================================================

OmniBarConfig *OmniBarConfig::instance = nullptr;

// Small glyphs for the list's tool row, drawn in the dialog's text colour.
static QIcon toolIcon(QWidget *widget, const char *file)
{
	return omniBarBundledIcon(QString::fromUtf8(file), widget->palette().color(QPalette::ButtonText), 16);
}

static QToolButton *listToolButton(QWidget *owner, const char *iconFile, const char *tipKey)
{
	QToolButton *button = new QToolButton();
	button->setIcon(toolIcon(owner, iconFile));
	button->setIconSize(QSize(16, 16));
	button->setAutoRaise(true);
	button->setToolTip(obs_module_text(tipKey));
	button->setAccessibleName(obs_module_text(tipKey));
	return button;
}

// A strip of the bar drawn in a given style - a status chip and a few
// buttons, some active - for the theme gallery.
static QPixmap renderStyleSample(const BarStyle &style)
{
	QWidget panel;
	panel.setAttribute(Qt::WA_DontShowOnScreen, true);
	panel.setObjectName("OmniBarPreviewBackdrop");
	panel.setStyleSheet(style.panelStyleSheet(QStringLiteral("OmniBarPreviewBackdrop")));
	QHBoxLayout *layout = new QHBoxLayout(&panel);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(qMin(style.spacing, 4));

	// Smaller than the real thing, so a card shows the whole strip.
	BarStyle sample = style;
	sample.iconSize = 18;
	sample.buttonPadding = qMin(style.buttonPadding, 5);

	ObsStateSnapshot live;
	live.streaming = true;
	live.recording = true;
	live.replayBuffer = true;

	auto status = std::make_shared<ButtonConfig>();
	status->action = std::make_unique<DisplayAction>(DisplayMetric::Status);
	status->displayMode = ButtonDisplayMode::TextOnly;
	status->label = QString();
	auto *chip = new OmniBarDisplay(status, &panel);
	chip->setPreviewMode(true);
	chip->applyStyle(sample);
	ObsStateSnapshot liveOnly;
	liveOnly.streaming = true;
	chip->setSimulatedState(liveOnly);
	layout->addWidget(chip);

	const FrontendActionType samples[] = {FrontendActionType::ToggleStreaming, FrontendActionType::ToggleRecording,
					      FrontendActionType::ToggleReplayBuffer,
					      FrontendActionType::ToggleStudioMode};
	for (FrontendActionType type : samples) {
		auto config = std::make_shared<ButtonConfig>();
		config->action = std::make_unique<FrontendAction>(type);
		auto *button = new OmniBarButton(config, &panel);
		button->setPreviewMode(true);
		button->applyStyle(sample);
		button->setChecked(config->action->isActiveIn(live));
		layout->addWidget(button);
	}

	panel.show();
	layout->activate();
	panel.resize(panel.sizeHint());
	QPixmap pixmap = panel.grab();
	panel.hide();
	return pixmap;
}

OmniBarConfig::OmniBarConfig(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("OmniBar.Settings.Title"));
	setMinimumSize(820, 600);
	setupUI();
	loadSettings();
	resize(1000, 760);
}

OmniBarConfig::~OmniBarConfig()
{
	if (instance == this)
		instance = nullptr;
}

void OmniBarConfig::showDialog(const QString &editId)
{
	if (!instance) {
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		instance = new OmniBarConfig(mainWindow);
		instance->setAttribute(Qt::WA_DeleteOnClose);
	}
	instance->show();
	instance->raise();
	instance->activateWindow();

	if (!editId.isEmpty())
		instance->editEntry(editId);
}

void OmniBarConfig::editEntry(const QString &id)
{
	tabs->setCurrentIndex(0);
	selectEntry(id);
	if (editor)
		editor->focusFirstField();
}

QTreeWidgetItem *OmniBarConfig::itemForId(const QString &id) const
{
	for (QTreeWidgetItemIterator it(buttonTree); *it; ++it) {
		if ((*it)->data(0, kConfigIdRole).toString() == id)
			return *it;
	}
	return nullptr;
}

void OmniBarConfig::selectEntry(const QString &id)
{
	QTreeWidgetItem *item = itemForId(id);
	if (!item)
		return;
	buttonTree->setCurrentItem(item);
	buttonTree->scrollToItem(item);
	// Selecting the row that is already current changes nothing, so the
	// inspector is brought up to date either way.
	if (!editor || !editor->getButtonConfig() || editor->getButtonConfig()->id != id)
		onSelectionChanged();
}

void OmniBarConfig::dockPositionChanged(DockPosition position)
{
	if (!instance || !instance->positionGroup)
		return;

	// The move is already saved, so this is not an edit to keep.
	instance->setSelectedPosition(position);
}

bool OmniBarConfig::hasUnappliedChanges()
{
	return instance && instance->dirty;
}

void OmniBarConfig::reloadIfClean()
{
	if (!instance || instance->dirty)
		return;

	QTreeWidgetItem *current = instance->buttonTree->currentItem();
	QString selectedId = current ? current->data(0, kConfigIdRole).toString() : QString();
	instance->loadSettings();
	if (!selectedId.isEmpty())
		instance->selectEntry(selectedId);
}

void OmniBarConfig::showAddMenu()
{
	showDialog();
	if (!instance)
		return;
	instance->tabs->setCurrentIndex(0);
	// After the window has been laid out, so the menu opens under the +.
	QTimer::singleShot(0, instance, &OmniBarConfig::onAddButton);
}

DockPosition OmniBarConfig::selectedPosition() const
{
	int id = positionGroup->checkedId();
	return id >= 0 ? static_cast<DockPosition>(id) : DockPosition::Top;
}

void OmniBarConfig::setSelectedPosition(DockPosition position)
{
	if (QAbstractButton *button = positionGroup->button(static_cast<int>(position)))
		button->setChecked(true);
	if (position == DockPosition::Left || position == DockPosition::Right)
		barPreview->setOrientation(Qt::Vertical);
	else if (position != DockPosition::None)
		barPreview->setOrientation(Qt::Horizontal);
}

void OmniBarConfig::markDirty()
{
	setDirty(true);
}

void OmniBarConfig::setDirty(bool value)
{
	dirty = value;
	if (unsavedLabel)
		unsavedLabel->setVisible(dirty);
}

void OmniBarConfig::reject()
{
	if (dirty) {
		QMessageBox::StandardButton answer = QMessageBox::question(
			this, obs_module_text("OmniBar.Settings.Title"),
			obs_module_text("OmniBar.Settings.ConfirmDiscard"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
		if (answer == QMessageBox::Cancel)
			return;
		if (answer == QMessageBox::Save)
			saveSettings();
	}

	QDialog::reject();
}

void OmniBarConfig::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// The bar being edited, across the top of both tabs: entries and looks
	// both show up here as they change.
	barPreview = new OmniBarPreview();
	connect(barPreview, &OmniBarPreview::entryClicked, this, [this](const QString &id) {
		tabs->setCurrentIndex(0);
		selectEntry(id);
	});
	mainLayout->addWidget(barPreview);

	tabs = new QTabWidget();
	tabs->addTab(createButtonsTab(), obs_module_text("OmniBar.Settings.Buttons"));
	tabs->addTab(createAppearanceTab(), obs_module_text("OmniBar.Settings.TabAppearance"));
	mainLayout->addWidget(tabs, 1);

	mainLayout->addWidget(createFooter());
}

QWidget *OmniBarConfig::createFooter()
{
	QWidget *footer = new QWidget();
	footer->setStyleSheet(QString::fromLatin1(kSegmentSheet));
	QHBoxLayout *layout = new QHBoxLayout(footer);
	layout->setContentsMargins(0, 0, 0, 0);

	// Where the bar docks, as a row of choices rather than a drop-down: there
	// are only five, and the current one should be visible at a glance.
	layout->addWidget(new QLabel(obs_module_text("OmniBar.Settings.DockPosition")));
	positionGroup = new QButtonGroup(this);
	positionGroup->setExclusive(true);
	const std::pair<DockPosition, const char *> positions[] = {{DockPosition::Top, "OmniBar.Position.Top"},
								   {DockPosition::Left, "OmniBar.Position.Left"},
								   {DockPosition::Bottom, "OmniBar.Position.Bottom"},
								   {DockPosition::Right, "OmniBar.Position.Right"},
								   {DockPosition::None, "OmniBar.Position.Hidden"}};
	for (const auto &position : positions) {
		QPushButton *button = new QPushButton(obs_module_text(position.second));
		button->setCheckable(true);
		button->setAutoDefault(false);
		positionGroup->addButton(button, static_cast<int>(position.first));
		layout->addWidget(button);
	}
	connect(positionGroup, &QButtonGroup::idClicked, this, [this](int id) {
		setSelectedPosition(static_cast<DockPosition>(id));
		markDirty();
	});

	layout->addStretch();

	unsavedLabel = new QLabel(obs_module_text("OmniBar.Settings.Unsaved"));
	unsavedLabel->setVisible(false);
	layout->addWidget(unsavedLabel);

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Ok |
							   QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
	buttonBox->button(QDialogButtonBox::Reset)->setText(obs_module_text("OmniBar.Settings.Revert"));
	connect(buttonBox, &QDialogButtonBox::accepted, this, &OmniBarConfig::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &OmniBarConfig::onCancel);
	connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OmniBarConfig::onApply);
	connect(buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &OmniBarConfig::onRevert);
	layout->addWidget(buttonBox);

	return footer;
}

QWidget *OmniBarConfig::createButtonsTab()
{
	QSplitter *splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);

	// Left: the entries, with the usual list tools underneath.
	QWidget *listPane = new QWidget();
	QVBoxLayout *listLayout = new QVBoxLayout(listPane);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(2);

	buttonTree = new ButtonTreeWidget();
	connect(buttonTree, &QTreeWidget::itemDoubleClicked, this, &OmniBarConfig::onEditButton);
	connect(buttonTree, &ButtonTreeWidget::itemsReordered, this, &OmniBarConfig::onTreeReordered);
	connect(buttonTree, &QTreeWidget::currentItemChanged, this, &OmniBarConfig::onSelectionChanged);
	listLayout->addWidget(buttonTree, 1);

	QHBoxLayout *tools = new QHBoxLayout();
	tools->setSpacing(2);
	addButton = listToolButton(this, "ui-add.svg", "OmniBar.Settings.AddButton");
	removeButton = listToolButton(this, "ui-remove.svg", "OmniBar.Settings.RemoveButton");
	duplicateButton = listToolButton(this, "ui-duplicate.svg", "OmniBar.Settings.Duplicate");
	moveUpButton = listToolButton(this, "ui-up.svg", "OmniBar.Settings.MoveUp");
	moveDownButton = listToolButton(this, "ui-down.svg", "OmniBar.Settings.MoveDown");
	connect(addButton, &QToolButton::clicked, this, &OmniBarConfig::onAddButton);
	connect(removeButton, &QToolButton::clicked, this, &OmniBarConfig::onRemoveButton);
	connect(duplicateButton, &QToolButton::clicked, this, &OmniBarConfig::onDuplicateButton);
	connect(moveUpButton, &QToolButton::clicked, this, &OmniBarConfig::onMoveUp);
	connect(moveDownButton, &QToolButton::clicked, this, &OmniBarConfig::onMoveDown);
	tools->addWidget(addButton);
	tools->addWidget(removeButton);
	tools->addWidget(duplicateButton);
	tools->addStretch();
	tools->addWidget(moveUpButton);
	tools->addWidget(moveDownButton);
	listLayout->addLayout(tools);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.TreeHint"));
	hint->setWordWrap(true);
	hint->setForegroundRole(QPalette::PlaceholderText);
	listLayout->addWidget(hint);

	// The usual list keys: Delete removes, F2 or Enter edits, Ctrl+D copies.
	auto addShortcut = [this](const QKeySequence &keys, void (OmniBarConfig::*slot)()) {
		QShortcut *shortcut = new QShortcut(keys, buttonTree);
		shortcut->setContext(Qt::WidgetShortcut);
		connect(shortcut, &QShortcut::activated, this, slot);
	};
	addShortcut(QKeySequence::Delete, &OmniBarConfig::onRemoveButton);
	addShortcut(QKeySequence(Qt::Key_F2), &OmniBarConfig::onEditButton);
	addShortcut(QKeySequence(Qt::Key_Return), &OmniBarConfig::onEditButton);
	addShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), &OmniBarConfig::onDuplicateButton);

	// Right: whatever is selected, edited in place.
	inspectorScroll = new QScrollArea();
	inspectorScroll->setWidgetResizable(true);
	inspectorScroll->setFrameShape(QFrame::NoFrame);

	splitter->addWidget(listPane);
	splitter->addWidget(inspectorScroll);
	splitter->setStretchFactor(0, 2);
	splitter->setStretchFactor(1, 3);
	splitter->setSizes({360, 560});

	return splitter;
}

QWidget *OmniBarConfig::createAppearanceTab()
{
	// Long enough to need scrolling on a small screen, so the page scrolls
	// rather than pushing the dialog off the bottom of it.
	QScrollArea *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);
	scroll->setWidget(page);

	// Themes, each card drawn in its own style so it can be chosen by eye.
	QGroupBox *themeGroup = new QGroupBox(obs_module_text("OmniBar.Settings.Preset"));
	QGridLayout *themeGrid = new QGridLayout(themeGroup);
	themeGrid->setSpacing(8);
	// The chosen theme is outlined in the highlight colour.
	themeGroup->setStyleSheet(
		QStringLiteral("QToolButton { border: 1px solid palette(mid); border-radius: 6px; padding: 6px; }"
			       "QToolButton:hover { border-color: palette(highlight); }"
			       "QToolButton:checked { border: 2px solid palette(highlight); }"));
	presetGroup = new QButtonGroup(this);
	presetGroup->setExclusive(true);

	const std::pair<StylePreset, const char *> presets[] = {
		{StylePreset::ObsNative, "OmniBar.Preset.ObsNative"},
		{StylePreset::Broadcast, "OmniBar.Preset.Broadcast"},
		{StylePreset::Capsule, "OmniBar.Preset.Capsule"},
		{StylePreset::MonoLine, "OmniBar.Preset.MonoLine"},
		{StylePreset::Compact, "OmniBar.Preset.Compact"},
		{StylePreset::ModernRounded, "OmniBar.Preset.ModernRounded"},
		{StylePreset::NeonAccent, "OmniBar.Preset.NeonAccent"},
		{StylePreset::Phosphor, "OmniBar.Preset.Phosphor"},
		{StylePreset::HighContrast, "OmniBar.Preset.HighContrast"},
		{StylePreset::Daylight, "OmniBar.Preset.Daylight"},
		{StylePreset::Custom, "OmniBar.Preset.Custom"}};

	const int columns = 3;
	int index = 0;
	for (const auto &preset : presets) {
		QToolButton *card = new QToolButton();
		card->setText(obs_module_text(preset.second));
		card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		card->setCheckable(true);
		card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		card->setAccessibleName(obs_module_text(preset.second));
		presetGroup->addButton(card, static_cast<int>(preset.first));
		presetCards.insert(static_cast<int>(preset.first), card);
		themeGrid->addWidget(card, index / columns, index % columns);
		index++;
	}
	connect(presetGroup, &QButtonGroup::idClicked, this, &OmniBarConfig::onPresetChanged);
	layout->addWidget(themeGroup);

	// Colour swatches. Theme colours turn custom colours on when picked,
	// since they are otherwise ignored; state and warning colours apply
	// either way.
	auto addColor = [this](QGridLayout *grid, QPushButton **target, const char *labelKey, QColor BarStyle::*member,
			       int row, int column, bool themeColor) {
		grid->addWidget(new QLabel(obs_module_text(labelKey)), row, column * 2);
		QPushButton *swatch = new QPushButton();
		connect(swatch, &QPushButton::clicked, this, [this, swatch, member, themeColor]() {
			QColor current = workingStyle.*member;
			QColor chosen = QColorDialog::getColor(current.isValid() ? current : QColor(Qt::white), this,
							       obs_module_text("OmniBar.Settings.Colors"),
							       QColorDialog::ShowAlphaChannel);
			if (!chosen.isValid())
				return;
			workingStyle.*member = chosen;
			setColorSwatch(swatch, chosen);
			if (themeColor && !useCustomColorsCheck->isChecked()) {
				updatingStyleWidgets = true;
				useCustomColorsCheck->setChecked(true);
				updatingStyleWidgets = false;
				workingStyle.useCustomColors = true;
				colorGrid->setEnabled(true);
			}
			onStyleValueChanged();
		});
		grid->addWidget(swatch, row, column * 2 + 1);
		if (target)
			*target = swatch;
		colorSwatches.append({swatch, member});
	};

	// Active state
	QGroupBox *activeGroup = new QGroupBox(obs_module_text("OmniBar.Settings.ActiveGroup"));
	QVBoxLayout *activeLayout = new QVBoxLayout(activeGroup);
	QFormLayout *activeForm = new QFormLayout();
	indicatorCombo = new QComboBox();
	indicatorCombo->addItem(obs_module_text("OmniBar.Indicator.Fill"), static_cast<int>(ActiveIndicator::Fill));
	indicatorCombo->addItem(obs_module_text("OmniBar.Indicator.Underline"),
				static_cast<int>(ActiveIndicator::Underline));
	indicatorCombo->addItem(obs_module_text("OmniBar.Indicator.Dot"), static_cast<int>(ActiveIndicator::Dot));
	indicatorCombo->addItem(obs_module_text("OmniBar.Indicator.Ring"), static_cast<int>(ActiveIndicator::Ring));
	connect(indicatorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&OmniBarConfig::onStyleValueChanged);
	activeForm->addRow(obs_module_text("OmniBar.Settings.Indicator"), indicatorCombo);
	activeLayout->addLayout(activeForm);

	useStateColorsCheck = new QCheckBox(obs_module_text("OmniBar.Settings.UseStateColors"));
	useStateColorsCheck->setToolTip(obs_module_text("OmniBar.Settings.UseStateColorsHint"));
	connect(useStateColorsCheck, &QCheckBox::toggled, this, [this](bool enabled) {
		stateColorGrid->setEnabled(enabled);
		onStyleValueChanged();
	});
	activeLayout->addWidget(useStateColorsCheck);

	stateColorGrid = new QWidget();
	QGridLayout *stateGrid = new QGridLayout(stateColorGrid);
	stateGrid->setContentsMargins(0, 0, 0, 0);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StateLive", &BarStyle::stateLive, 0, 0, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StateRecording", &BarStyle::stateRecording, 0, 1, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StatePaused", &BarStyle::statePaused, 1, 0, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StateReplay", &BarStyle::stateReplay, 1, 1, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StateVirtualCam", &BarStyle::stateVirtualCam, 2, 0, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.StateStudio", &BarStyle::stateStudio, 2, 1, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.WarningColor", &BarStyle::warningColor, 3, 0, false);
	addColor(stateGrid, nullptr, "OmniBar.Settings.CriticalColor", &BarStyle::criticalColor, 3, 1, false);
	activeLayout->addWidget(stateColorGrid);
	layout->addWidget(activeGroup);

	// Metrics
	QGroupBox *metricsGroup = new QGroupBox(obs_module_text("OmniBar.Settings.Metrics"));
	QFormLayout *metricsForm = new QFormLayout(metricsGroup);

	auto addSpin = [&](QSpinBox *&target, const char *labelKey, int min, int max) {
		target = new QSpinBox();
		target->setRange(min, max);
		target->setSuffix(" px");
		connect(target, QOverload<int>::of(&QSpinBox::valueChanged), this, &OmniBarConfig::onStyleValueChanged);
		metricsForm->addRow(obs_module_text(labelKey), target);
	};

	addSpin(iconSizeSpin, "OmniBar.Settings.IconSize", 12, 128);
	addSpin(spacingSpin, "OmniBar.Settings.Spacing", 0, 32);
	addSpin(buttonPaddingSpin, "OmniBar.Settings.ButtonPadding", 0, 32);
	addSpin(cornerRadiusSpin, "OmniBar.Settings.CornerRadius", 0, 32);
	addSpin(borderWidthSpin, "OmniBar.Settings.BorderWidth", 0, 6);
	layout->addWidget(metricsGroup);

	// Pulse
	QGroupBox *pulseGroup = new QGroupBox(obs_module_text("OmniBar.Settings.PulseGroup"));
	QFormLayout *pulseForm = new QFormLayout(pulseGroup);

	pulsePeriodSpin = new QSpinBox();
	pulsePeriodSpin->setRange(300, 5000);
	pulsePeriodSpin->setSingleStep(100);
	pulsePeriodSpin->setSuffix(" ms");
	connect(pulsePeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&OmniBarConfig::onStyleValueChanged);
	pulseForm->addRow(obs_module_text("OmniBar.Settings.PulsePeriod"), pulsePeriodSpin);

	pulseIntensitySpin = new QSpinBox();
	pulseIntensitySpin->setRange(10, 100);
	pulseIntensitySpin->setSingleStep(5);
	pulseIntensitySpin->setSuffix(" %");
	connect(pulseIntensitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&OmniBarConfig::onStyleValueChanged);
	pulseForm->addRow(obs_module_text("OmniBar.Settings.PulseIntensity"), pulseIntensitySpin);

	layout->addWidget(pulseGroup);

	// Colours
	QGroupBox *colorGroup = new QGroupBox(obs_module_text("OmniBar.Settings.Colors"));
	QVBoxLayout *colorLayout = new QVBoxLayout(colorGroup);

	useCustomColorsCheck = new QCheckBox(obs_module_text("OmniBar.Settings.UseCustomColors"));
	connect(useCustomColorsCheck, &QCheckBox::toggled, this, &OmniBarConfig::onUseCustomColorsToggled);
	colorLayout->addWidget(useCustomColorsCheck);

	colorGrid = new QWidget();
	QGridLayout *grid = new QGridLayout(colorGrid);
	grid->setContentsMargins(0, 0, 0, 0);

	addColor(grid, &barBackgroundButton, "OmniBar.Settings.BarBackground", &BarStyle::barBackground, 0, 0, true);
	addColor(grid, &buttonBackgroundButton, "OmniBar.Settings.ButtonBackground", &BarStyle::buttonBackground, 0, 1,
		 true);
	addColor(grid, &buttonHoverButton, "OmniBar.Settings.ButtonHover", &BarStyle::buttonHover, 1, 0, true);
	addColor(grid, &buttonCheckedButton, "OmniBar.Settings.ButtonChecked", &BarStyle::buttonChecked, 1, 1, true);
	addColor(grid, &buttonBorderButton, "OmniBar.Settings.ButtonBorder", &BarStyle::buttonBorder, 2, 0, true);
	addColor(grid, &textColorButton, "OmniBar.Settings.TextColor", &BarStyle::textColor, 2, 1, true);

	colorLayout->addWidget(colorGrid);
	layout->addWidget(colorGroup);
	layout->addStretch();

	return scroll;
}

void OmniBarConfig::refreshPresetCards()
{
	// The named themes only change with the OBS palette; Custom shows
	// whatever is being built.
	for (auto it = presetCards.constBegin(); it != presetCards.constEnd(); ++it) {
		auto preset = static_cast<StylePreset>(it.key());
		BarStyle cardStyle = preset == StylePreset::Custom ? workingStyle : BarStyle::fromPreset(preset);
		QPixmap sample = renderStyleSample(cardStyle);
		it.value()->setIcon(QIcon(sample));
		it.value()->setIconSize(sample.size() / sample.devicePixelRatio());
	}
}

void OmniBarConfig::loadSettings()
{
	workingButtons.clear();
	configRegistry.clear();
	for (const auto &config : SettingsManager::getButtons()) {
		if (config)
			workingButtons.append(config->clone());
	}
	populateButtonTree();

	setSelectedPosition(SettingsManager::getDockPosition());

	workingStyle = SettingsManager::getStyle();
	updateStyleWidgets();
	refreshPresetCards();

	barPreview->setBarStyle(workingStyle);
	refreshBarPreview();
	onSelectionChanged();
	setDirty(false);
}

void OmniBarConfig::saveSettings()
{
	rebuildModelFromTree();

	SettingsManager::setButtons(workingButtons);
	SettingsManager::setDockPosition(selectedPosition());
	SettingsManager::setStyle(workingStyle);
	SettingsManager::save();
	setDirty(false);

	if (OmniBar::getInstance()) {
		OmniBar::getInstance()->refreshFromConfiguration();
	}
}

void OmniBarConfig::updateTreeItem(QTreeWidgetItem *item, const std::shared_ptr<ButtonConfig> &config)
{
	if (!item || !config)
		return;

	item->setText(0, config->displayText());
	item->setText(1, config->summaryText());
	item->setIcon(0, omniBarIconForConfig(config, palette().color(QPalette::Text), 16));
	item->setData(0, kConfigIdRole, config->id);
	item->setData(0, kConfigIsGroupRole, config->isGroup);
	item->setData(0, kConfigIsDecorationRole, config->isDecoration());

	// A button whose source has gone stays in the list, saying so, rather
	// than looking fine and never appearing on the bar.
	if (config->targetMissing()) {
		item->setText(1, QString::fromUtf8(obs_module_text("OmniBar.Settings.TargetMissing"))
					 .arg(config->summaryText()));
		item->setForeground(1, QBrush(QColor(214, 138, 0)));
		item->setToolTip(1, obs_module_text("OmniBar.Settings.MissingHint"));
	} else {
		item->setForeground(1, QBrush());
		item->setToolTip(1, QString());
	}

	Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
	// Only groups accept a drop, which is what caps nesting at one level.
	if (config->isGroup)
		flags |= Qt::ItemIsDropEnabled;
	item->setFlags(flags);
}

QTreeWidgetItem *OmniBarConfig::createTreeItem(const std::shared_ptr<ButtonConfig> &config)
{
	QTreeWidgetItem *item = new QTreeWidgetItem();
	updateTreeItem(item, config);
	configRegistry.insert(config->id, config);
	return item;
}

void OmniBarConfig::populateButtonTree()
{
	QString selectedId = buttonTree->currentItem() ? buttonTree->currentItem()->data(0, kConfigIdRole).toString()
						       : QString();

	// Rebuilding the rows must not swap the inspector out from under
	// whatever is being typed into it.
	QSignalBlocker blocker(buttonTree);
	buttonTree->clear();
	configRegistry.clear();

	QTreeWidgetItem *toSelect = nullptr;

	for (const auto &config : workingButtons) {
		if (!config)
			continue;

		QTreeWidgetItem *item = createTreeItem(config);
		buttonTree->addTopLevelItem(item);
		if (config->id == selectedId)
			toSelect = item;

		if (!config->isGroup)
			continue;

		for (const auto &child : config->children) {
			if (!child)
				continue;
			QTreeWidgetItem *childItem = createTreeItem(child);
			item->addChild(childItem);
			if (child->id == selectedId)
				toSelect = childItem;
		}
		item->setExpanded(true);
	}

	if (toSelect)
		buttonTree->setCurrentItem(toSelect);
}

std::shared_ptr<ButtonConfig> OmniBarConfig::configForItem(QTreeWidgetItem *item) const
{
	if (!item)
		return nullptr;
	return configRegistry.value(item->data(0, kConfigIdRole).toString());
}

void OmniBarConfig::rebuildModelFromTree()
{
	QList<std::shared_ptr<ButtonConfig>> rebuilt;

	for (int i = 0; i < buttonTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = buttonTree->topLevelItem(i);
		auto config = configForItem(item);
		if (!config)
			continue;

		QList<std::shared_ptr<ButtonConfig>> children;
		for (int c = 0; c < item->childCount(); c++) {
			auto child = configForItem(item->child(c));
			if (!child)
				continue;
			// Nesting stays one level deep.
			child->isGroup = false;
			child->children.clear();
			children.append(child);
		}

		config->children.clear();
		rebuilt.append(config);

		if (config->isGroup) {
			config->children = children;
		} else {
			// A group turned back into a plain button lets go of what
			// was inside it: the buttons move out to sit after it,
			// rather than being thrown away.
			rebuilt.append(children);
		}
	}

	workingButtons = rebuilt;
}

void OmniBarConfig::refreshBarPreview()
{
	barPreview->setEntries(workingButtons);
	QTreeWidgetItem *current = buttonTree->currentItem();
	barPreview->setSelectedId(current ? current->data(0, kConfigIdRole).toString() : QString());
}

void OmniBarConfig::onSelectionChanged()
{
	QTreeWidgetItem *item = buttonTree->currentItem();
	auto config = configForItem(item);

	// A row nested under a group cannot itself become a group, and neither
	// can a piece of the bar.
	bool allowGroup = item && !item->parent() && config && !config->isDecoration();
	showInspector(config, allowGroup);

	bool hasSelection = config != nullptr;
	removeButton->setEnabled(hasSelection);
	duplicateButton->setEnabled(hasSelection);
	moveUpButton->setEnabled(hasSelection);
	moveDownButton->setEnabled(hasSelection);

	barPreview->setSelectedId(config ? config->id : QString());
}

void OmniBarConfig::showInspector(const std::shared_ptr<ButtonConfig> &config, bool allowGroup)
{
	if (QWidget *old = inspectorScroll->takeWidget())
		old->deleteLater();
	editor = nullptr;

	QWidget *container = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(container);
	layout->setContentsMargins(14, 8, 14, 8);

	if (!config) {
		QLabel *empty = new QLabel(obs_module_text("OmniBar.Inspector.Empty"));
		empty->setWordWrap(true);
		empty->setAlignment(Qt::AlignCenter);
		empty->setForegroundRole(QPalette::PlaceholderText);
		layout->addStretch();
		layout->addWidget(empty);
		layout->addStretch();
	} else {
		editor = new ButtonEditor(config, workingStyle, allowGroup);
		connect(editor, &ButtonEditor::changed, this, &OmniBarConfig::onEditorChanged);
		layout->addWidget(editor);
	}

	inspectorScroll->setWidget(container);
}

void OmniBarConfig::onEditorChanged()
{
	if (!editor)
		return;

	auto config = editor->getButtonConfig();
	QTreeWidgetItem *item = config ? itemForId(config->id) : nullptr;
	if (!item)
		return;

	// Turning a button into a group, or back, changes what the row may hold,
	// so the rows are rebuilt - which also moves the buttons out of a group
	// that has stopped being one. Anything else is a change to one row.
	bool wasGroup = item->data(0, kConfigIsGroupRole).toBool();
	if (wasGroup != config->isGroup) {
		rebuildModelFromTree();
		populateButtonTree();
	} else {
		updateTreeItem(item, config);
	}

	refreshBarPreview();
	markDirty();
}

void OmniBarConfig::onTreeReordered()
{
	rebuildModelFromTree();
	populateButtonTree();
	refreshBarPreview();
	markDirty();
}

void OmniBarConfig::addNewEntry(std::shared_ptr<ButtonConfig> config)
{
	rebuildModelFromTree();

	// Insert next to whatever is selected, staying inside a group when a
	// child of it is selected.
	QTreeWidgetItem *current = buttonTree->currentItem();
	auto currentConfig = configForItem(current);
	bool placed = false;

	if (current && current->parent() && !config->isGroup && !config->isDecoration()) {
		auto parentConfig = configForItem(current->parent());
		if (parentConfig) {
			int row = parentConfig->children.indexOf(currentConfig);
			parentConfig->children.insert(row >= 0 ? row + 1 : parentConfig->children.size(), config);
			placed = true;
		}
	}

	if (!placed) {
		// A selected child stands for its group at the top level.
		auto anchor = current && current->parent() ? configForItem(current->parent()) : currentConfig;
		int row = anchor ? workingButtons.indexOf(anchor) : -1;
		workingButtons.insert(row >= 0 ? row + 1 : workingButtons.size(), config);
	}

	populateButtonTree();
	refreshBarPreview();
	selectEntry(config->id);
	if (editor)
		editor->focusFirstField();
	markDirty();
}

void OmniBarConfig::onAddButton()
{
	QMenu menu(this);
	QAction *buttonAction = menu.addAction(obs_module_text("OmniBar.Settings.AddPlainButton"));
	QAction *groupAction = menu.addAction(obs_module_text("OmniBar.Settings.AddGroup"));
	QAction *displayAction = menu.addAction(obs_module_text("OmniBar.Settings.AddDisplay"));
	menu.addSeparator();
	QAction *spacerAction = menu.addAction(obs_module_text("OmniBar.Settings.AddSpacer"));
	QAction *flexibleAction = menu.addAction(obs_module_text("OmniBar.Settings.AddFlexibleSpace"));
	QAction *dividerAction = menu.addAction(obs_module_text("OmniBar.Settings.AddDivider"));

	QAction *chosen = menu.exec(addButton->mapToGlobal(QPoint(0, addButton->height())));
	if (!chosen)
		return;

	auto config = std::make_shared<ButtonConfig>();

	if (chosen == spacerAction || chosen == flexibleAction) {
		config->action = std::make_unique<SpacerAction>(10, chosen == flexibleAction);
	} else if (chosen == dividerAction) {
		config->action = std::make_unique<DividerAction>();
	} else if (chosen == displayAction) {
		config->action = std::make_unique<DisplayAction>(DisplayMetric::StreamTime);
		// A number wants its own room, not an icon's square.
		config->displayMode = ButtonDisplayMode::TextOnly;
	} else if (chosen == groupAction) {
		config->isGroup = true;
		config->action = std::make_unique<NoAction>();
		config->label = obs_module_text("OmniBar.Settings.NewGroup");
	} else if (chosen == buttonAction) {
		config->action = std::make_unique<FrontendAction>(FrontendActionType::ToggleStreaming);
	} else {
		return;
	}

	addNewEntry(config);
}

void OmniBarConfig::onEditButton()
{
	if (editor)
		editor->focusFirstField();
}

void OmniBarConfig::onDuplicateButton()
{
	auto config = configForItem(buttonTree->currentItem());
	if (!config)
		return;

	auto copy = config->clone();
	// A duplicate needs identities of its own, all the way down.
	copy->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	for (auto &child : copy->children)
		child->id = QUuid::createUuid().toString(QUuid::WithoutBraces);

	addNewEntry(copy);
}

void OmniBarConfig::onRemoveButton()
{
	QTreeWidgetItem *item = buttonTree->currentItem();
	auto config = configForItem(item);
	if (!config)
		return;

	if (config->isGroup && !config->children.isEmpty()) {
		QMessageBox::StandardButton answer =
			QMessageBox::question(this, obs_module_text("OmniBar.Settings.RemoveButton"),
					      obs_module_text("OmniBar.Settings.ConfirmRemoveGroup"));
		if (answer != QMessageBox::Yes)
			return;
	}

	// Whatever sat below the removed row takes its place in the selection,
	// so pressing Delete again keeps working down the list.
	QTreeWidgetItem *next = buttonTree->itemBelow(item);
	while (next && next->parent() == item)
		next = buttonTree->itemBelow(next);
	if (!next)
		next = buttonTree->itemAbove(item);
	QString nextId = next ? next->data(0, kConfigIdRole).toString() : QString();

	rebuildModelFromTree();

	if (item->parent()) {
		auto parentConfig = configForItem(item->parent());
		if (parentConfig)
			parentConfig->children.removeAll(config);
	} else {
		workingButtons.removeAll(config);
	}

	populateButtonTree();
	if (QTreeWidgetItem *nextItem = itemForId(nextId)) {
		QSignalBlocker blocker(buttonTree);
		buttonTree->setCurrentItem(nextItem);
	} else {
		QSignalBlocker blocker(buttonTree);
		buttonTree->setCurrentItem(nullptr);
	}
	onSelectionChanged();
	refreshBarPreview();
	markDirty();
}

void OmniBarConfig::onMoveUp()
{
	QTreeWidgetItem *item = buttonTree->currentItem();
	auto config = configForItem(item);
	if (!config)
		return;

	rebuildModelFromTree();

	QList<std::shared_ptr<ButtonConfig>> *list = &workingButtons;
	if (item->parent()) {
		auto parentConfig = configForItem(item->parent());
		if (!parentConfig)
			return;
		list = &parentConfig->children;
	}

	int row = list->indexOf(config);
	if (row > 0) {
		list->move(row, row - 1);
		markDirty();
	}

	populateButtonTree();
	refreshBarPreview();
}

void OmniBarConfig::onMoveDown()
{
	QTreeWidgetItem *item = buttonTree->currentItem();
	auto config = configForItem(item);
	if (!config)
		return;

	rebuildModelFromTree();

	QList<std::shared_ptr<ButtonConfig>> *list = &workingButtons;
	if (item->parent()) {
		auto parentConfig = configForItem(item->parent());
		if (!parentConfig)
			return;
		list = &parentConfig->children;
	}

	int row = list->indexOf(config);
	if (row >= 0 && row < list->size() - 1) {
		list->move(row, row + 1);
		markDirty();
	}

	populateButtonTree();
	refreshBarPreview();
}

void OmniBarConfig::onPresetChanged(int presetId)
{
	if (updatingStyleWidgets)
		return;

	auto preset = static_cast<StylePreset>(presetId);
	if (preset == StylePreset::Custom) {
		workingStyle.preset = StylePreset::Custom;
		markDirty();
		return;
	}

	// A preset fills in every value; the user can then edit any of them.
	workingStyle = BarStyle::fromPreset(preset);
	updateStyleWidgets();
	markDirty();
}

void OmniBarConfig::onStyleValueChanged()
{
	if (updatingStyleWidgets)
		return;

	workingStyle.iconSize = iconSizeSpin->value();
	workingStyle.spacing = spacingSpin->value();
	workingStyle.buttonPadding = buttonPaddingSpin->value();
	workingStyle.cornerRadius = cornerRadiusSpin->value();
	workingStyle.borderWidth = borderWidthSpin->value();
	workingStyle.pulsePeriod = pulsePeriodSpin->value();
	workingStyle.pulseIntensity = pulseIntensitySpin->value();
	workingStyle.useCustomColors = useCustomColorsCheck->isChecked();
	workingStyle.indicator = static_cast<ActiveIndicator>(indicatorCombo->currentData().toInt());
	workingStyle.useStateColors = useStateColorsCheck->isChecked();
	markDirty();

	// Editing any value means this is no longer one of the named looks.
	if (workingStyle != BarStyle::fromPreset(workingStyle.preset)) {
		workingStyle.preset = StylePreset::Custom;
		if (QToolButton *card = presetCards.value(static_cast<int>(StylePreset::Custom)))
			card->setChecked(true);
	}

	refreshStylePreview();
}

void OmniBarConfig::onUseCustomColorsToggled(bool enabled)
{
	colorGrid->setEnabled(enabled);
	if (updatingStyleWidgets)
		return;
	onStyleValueChanged();
}

void OmniBarConfig::updateStyleWidgets()
{
	updatingStyleWidgets = true;

	if (QToolButton *card = presetCards.value(static_cast<int>(workingStyle.preset)))
		card->setChecked(true);

	iconSizeSpin->setValue(workingStyle.iconSize);
	spacingSpin->setValue(workingStyle.spacing);
	buttonPaddingSpin->setValue(workingStyle.buttonPadding);
	cornerRadiusSpin->setValue(workingStyle.cornerRadius);
	borderWidthSpin->setValue(workingStyle.borderWidth);
	pulsePeriodSpin->setValue(workingStyle.pulsePeriod);
	pulseIntensitySpin->setValue(workingStyle.pulseIntensity);
	useCustomColorsCheck->setChecked(workingStyle.useCustomColors);
	colorGrid->setEnabled(workingStyle.useCustomColors);

	int indicatorIndex = indicatorCombo->findData(static_cast<int>(workingStyle.indicator));
	if (indicatorIndex >= 0)
		indicatorCombo->setCurrentIndex(indicatorIndex);
	useStateColorsCheck->setChecked(workingStyle.useStateColors);
	stateColorGrid->setEnabled(workingStyle.useStateColors);

	for (const auto &swatch : colorSwatches)
		setColorSwatch(swatch.first, workingStyle.*(swatch.second));

	updatingStyleWidgets = false;

	refreshStylePreview();
}

void OmniBarConfig::refreshStylePreview()
{
	if (!barPreview)
		return;

	barPreview->setBarStyle(workingStyle);

	// The Custom card shows the style being built.
	if (QToolButton *card = presetCards.value(static_cast<int>(StylePreset::Custom))) {
		QPixmap sample = renderStyleSample(workingStyle);
		card->setIcon(QIcon(sample));
		card->setIconSize(sample.size() / sample.devicePixelRatio());
	}

	// The inspector draws its icon previews in the style too.
	QTreeWidgetItem *current = buttonTree ? buttonTree->currentItem() : nullptr;
	if (editor && current)
		onSelectionChanged();
}

void OmniBarConfig::onApply()
{
	saveSettings();
}

void OmniBarConfig::onOk()
{
	saveSettings();
	accept();
}

void OmniBarConfig::onCancel()
{
	reject();
}

void OmniBarConfig::onRevert()
{
	loadSettings();
}
