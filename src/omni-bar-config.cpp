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
#include "settings-manager.hpp"
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

static const int kConfigIdRole = Qt::UserRole;
static const int kConfigIsGroupRole = Qt::UserRole + 1;

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

// ============================================================================
// ConditionEditor
// ============================================================================

ConditionEditor::ConditionEditor(bool allowOwnAction, QWidget *parent) : QWidget(parent)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	sourceCombo = new QComboBox();
	sourceCombo->addItem(obs_module_text("OmniBar.Condition.Always"), static_cast<int>(ConditionSource::Always));
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

	invertCheck = new QCheckBox(obs_module_text("OmniBar.Settings.Invert"));
	layout->addWidget(invertCheck);

	connect(sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
		// Inverting "always" would mean never, which is never useful.
		invertCheck->setEnabled(sourceCombo->currentData().toInt() !=
					static_cast<int>(ConditionSource::Always));
		emit changed();
	});
	connect(invertCheck, &QCheckBox::toggled, this, &ConditionEditor::changed);

	invertCheck->setEnabled(false);
}

void ConditionEditor::setCondition(const ActivationCondition &condition)
{
	int index = sourceCombo->findData(static_cast<int>(condition.source));
	sourceCombo->setCurrentIndex(index >= 0 ? index : 0);
	invertCheck->setChecked(condition.inverted);
	invertCheck->setEnabled(condition.isSet());
}

ActivationCondition ConditionEditor::condition() const
{
	ActivationCondition result;
	result.source = static_cast<ConditionSource>(sourceCombo->currentData().toInt());
	result.inverted = result.isSet() && invertCheck->isChecked();
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
}

bool ButtonTreeWidget::isDropAllowed(QTreeWidgetItem *source, QTreeWidgetItem *target,
				     DropIndicatorPosition position) const
{
	if (!source)
		return false;

	bool sourceIsGroup = source->data(0, kConfigIsGroupRole).toBool();

	if (position == OnItem) {
		if (!target || target == source)
			return false;
		if (!(target->flags() & Qt::ItemIsDropEnabled))
			return false;
		// Groups only nest one level deep, so a group can never be
		// dropped inside another group.
		return !sourceIsGroup && target->parent() == nullptr;
	}

	if (position == OnViewport)
		return true;

	// Dropping between a group's children lands the item in that group.
	if (target && target->parent() != nullptr)
		return !sourceIsGroup;

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
// ButtonPreview
// ============================================================================

ButtonPreview::ButtonPreview(QWidget *parent) : QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	backdrop = new QWidget(this);
	backdrop->setObjectName("OmniBarPreviewBackdrop");
	backdrop->setMinimumHeight(96);

	backdropLayout = new QVBoxLayout(backdrop);
	backdropLayout->setContentsMargins(16, 16, 16, 16);

	layout->addWidget(backdrop);
}

void ButtonPreview::refresh(const BarStyle &style, const std::shared_ptr<ButtonConfig> &config, bool active)
{
	if (!config)
		return;

	backdrop->setStyleSheet(style.panelStyleSheet(QStringLiteral("OmniBarPreviewBackdrop")));

	// Rebuilt rather than restyled: a bar button takes its config in the
	// constructor, and using the real class is what keeps the preview honest.
	delete previewButton;
	previewButton = new OmniBarButton(config, backdrop);
	previewButton->setPreviewMode(true);
	// A picture of a button, not a working one.
	previewButton->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	previewButton->applyStyle(style);
	previewButton->setGroupIndicator(config->isGroup, false, false);
	previewButton->setChecked(active);

	backdropLayout->addWidget(previewButton, 0, Qt::AlignCenter);
}

// ============================================================================
// ButtonEditDialog
// ============================================================================

ButtonEditDialog::ButtonEditDialog(std::shared_ptr<ButtonConfig> config, const BarStyle &barStyle, bool groupAllowed,
				   QWidget *parent)
	: QDialog(parent),
	  buttonConfig(config),
	  style(barStyle),
	  allowGroup(groupAllowed),
	  spacerMode(config && config->isSpacer()),
	  dividerMode(config && config->isDivider())
{
	if (spacerMode)
		setWindowTitle(obs_module_text("OmniBar.Settings.EditSpacer"));
	else if (dividerMode)
		setWindowTitle(obs_module_text("OmniBar.Settings.EditDivider"));
	else
		setWindowTitle(obs_module_text("OmniBar.Settings.EditButton"));

	if (spacerMode) {
		setupSpacerUI();
	} else if (dividerMode) {
		setupDividerUI();
	} else {
		setupUI();
		populateSources();
		populateScenes();
	}

	populateFromConfig();

	// Populating the action combo may not have changed its index, so make
	// sure the group hint reflects the final state either way.
	if (allowGroup && groupCheck)
		onGroupToggled(groupCheck->isChecked());

	refreshPreview();

	// Open at the size the content asks for; the minimum above only governs
	// how far the user can shrink it.
	adjustSize();
}

void ButtonEditDialog::setupDividerUI()
{
	setMinimumWidth(360);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// A divider is small enough that seeing it beats describing it.
	QWidget *backdrop = new QWidget();
	backdrop->setObjectName("OmniBarPreviewBackdrop");
	backdrop->setStyleSheet(style.panelStyleSheet(QStringLiteral("OmniBarPreviewBackdrop")));
	QHBoxLayout *backdropLayout = new QHBoxLayout(backdrop);
	backdropLayout->setContentsMargins(12, 12, 12, 12);
	backdropLayout->setAlignment(Qt::AlignCenter);

	dividerPreviewConfig = std::make_shared<ButtonConfig>();
	dividerPreviewConfig->action = std::make_unique<DividerAction>();
	dividerPreview = new OmniBarDivider(dividerPreviewConfig, Qt::Horizontal, style, backdrop);
	backdropLayout->addWidget(dividerPreview);
	mainLayout->addWidget(backdrop);

	QFormLayout *form = new QFormLayout();

	dividerThicknessSpin = new QSpinBox();
	dividerThicknessSpin->setRange(1, 6);
	dividerThicknessSpin->setValue(1);
	dividerThicknessSpin->setSuffix(" px");
	connect(dividerThicknessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&ButtonEditDialog::onDividerValueChanged);
	form->addRow(obs_module_text("OmniBar.Settings.DividerThickness"), dividerThicknessSpin);

	dividerLengthSpin = new QSpinBox();
	dividerLengthSpin->setRange(10, 100);
	dividerLengthSpin->setValue(70);
	dividerLengthSpin->setSuffix(" %");
	connect(dividerLengthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
		&ButtonEditDialog::onDividerValueChanged);
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
	connect(dividerColorButton, &QPushButton::clicked, this, &ButtonEditDialog::onDividerColorClicked);
	colorLayout->addWidget(dividerColorButton, 1);
	form->addRow(obs_module_text("OmniBar.Settings.DividerColor"), colorLayout);

	showConditionEditor = new ConditionEditor(false);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);

	mainLayout->addLayout(form);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.DividerHint"));
	hint->setWordWrap(true);
	mainLayout->addWidget(hint);
	mainLayout->addStretch();

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ButtonEditDialog::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &ButtonEditDialog::onCancel);
	mainLayout->addWidget(buttonBox);
}

void ButtonEditDialog::onDividerValueChanged()
{
	if (!dividerPreview || !dividerPreviewConfig)
		return;

	auto *divider = dynamic_cast<DividerAction *>(dividerPreviewConfig->action.get());
	if (!divider)
		return;

	divider->setThickness(dividerThicknessSpin->value());
	divider->setLengthPercent(dividerLengthSpin->value());
	if (dividerColorCheck->isChecked() && dividerColor.isValid())
		divider->setCustomColor(dividerColor);
	else
		divider->clearCustomColor();

	dividerPreview->applyStyle(Qt::Horizontal, style);
}

void ButtonEditDialog::onDividerColorClicked()
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

ButtonEditDialog::~ButtonEditDialog() {}

void ButtonEditDialog::setupSpacerUI()
{
	setMinimumWidth(340);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QFormLayout *form = new QFormLayout();
	spacerWidthSpin = new QSpinBox();
	spacerWidthSpin->setRange(1, 400);
	spacerWidthSpin->setValue(10);
	spacerWidthSpin->setSuffix(" px");
	form->addRow(obs_module_text("OmniBar.Settings.SpacerWidth"), spacerWidthSpin);

	showConditionEditor = new ConditionEditor(false);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);
	mainLayout->addLayout(form);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.SpacerHint"));
	hint->setWordWrap(true);
	mainLayout->addWidget(hint);
	mainLayout->addStretch();

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ButtonEditDialog::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &ButtonEditDialog::onCancel);
	mainLayout->addWidget(buttonBox);
}

void ButtonEditDialog::setupUI()
{
	// Sized from its content rather than a fixed floor, so the tabs are not
	// padded out with empty space.
	setMinimumSize(600, 400);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	QHBoxLayout *panes = new QHBoxLayout();

	// Left pane: a live picture of the button as the bar will draw it.
	QVBoxLayout *leftPane = new QVBoxLayout();
	QLabel *previewLabel = new QLabel(obs_module_text("OmniBar.Settings.Preview"));
	leftPane->addWidget(previewLabel);

	preview = new ButtonPreview();
	preview->setFixedWidth(190);
	leftPane->addWidget(preview);

	previewActiveCheck = new QCheckBox(obs_module_text("OmniBar.Settings.PreviewActive"));
	connect(previewActiveCheck, &QCheckBox::toggled, this, &ButtonEditDialog::refreshPreview);
	leftPane->addWidget(previewActiveCheck);
	leftPane->addStretch();

	panes->addLayout(leftPane);

	// Right pane: everything that can be configured.
	tabs = new QTabWidget();
	tabs->addTab(createAppearanceTab(), obs_module_text("OmniBar.Settings.TabAppearance"));
	tabs->addTab(createActionTab(), obs_module_text("OmniBar.Settings.TabAction"));
	if (allowGroup) {
		groupTabIndex = tabs->addTab(createGroupTab(), obs_module_text("OmniBar.Settings.TabGroup"));
	}
	panes->addWidget(tabs, 1);

	mainLayout->addLayout(panes, 1);

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ButtonEditDialog::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &ButtonEditDialog::onCancel);
	mainLayout->addWidget(buttonBox);
}

// The built-in set is large enough that a flat list is hard to scan, so it is
// broken up by unselectable section headers with each entry showing its glyph.
void ButtonEditDialog::buildIconCombo()
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

	addHeader("OmniBar.IconGroup.Sources");
	addEntry("OmniBar.Icon.Visibility", "visibility.svg", true);
	addEntry("OmniBar.Icon.Filter", "filter.svg", true);
	addEntry("OmniBar.Icon.Hotkey", "hotkey.svg", true);

	addHeader("OmniBar.IconGroup.Other");
	addEntry("OmniBar.Icon.Group", "group.svg", true);
	addEntry("OmniBar.Icon.None", "none.svg", true);
	addEntry("OmniBar.Icon.Divider", "divider.svg", true);
	addEntry("OmniBar.Icon.Spacer", "spacer.svg", true);
	addEntry("OmniBar.Icon.Custom", "custom", false);

	iconCombo->setModel(model);
	iconCombo->setIconSize(QSize(16, 16));
	// The popup would otherwise inherit the combo's width and elide the
	// longer entries.
	iconCombo->view()->setMinimumWidth(260);
}

QWidget *ButtonEditDialog::createAppearanceTab()
{
	QWidget *page = new QWidget();
	QFormLayout *form = new QFormLayout(page);

	labelEdit = new QLineEdit();
	labelEdit->setPlaceholderText(obs_module_text("OmniBar.Settings.LabelPlaceholder"));
	connect(labelEdit, &QLineEdit::textChanged, this, &ButtonEditDialog::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.Label"), labelEdit);

	displayModeCombo = new QComboBox();
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
		&ButtonEditDialog::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.DisplayMode"), displayModeCombo);

	tooltipEdit = new QLineEdit();
	connect(tooltipEdit, &QLineEdit::textChanged, this, &ButtonEditDialog::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.Tooltip"), tooltipEdit);

	// Icon selection
	QHBoxLayout *iconLayout = new QHBoxLayout();
	iconCombo = new QComboBox();
	buildIconCombo();
	connect(iconCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditDialog::onIconChanged);
	iconLayout->addWidget(iconCombo, 1);

	iconBrowseButton = new QPushButton("...");
	iconBrowseButton->setFixedWidth(32);
	connect(iconBrowseButton, &QPushButton::clicked, this, &ButtonEditDialog::onIconBrowse);
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
	connect(tintIconCheck, &QCheckBox::toggled, this, &ButtonEditDialog::refreshPreview);
	connect(tintIconCheck, &QCheckBox::toggled, this, &ButtonEditDialog::updateIconPreview);
	form->addRow(QString(), tintIconCheck);

	showConditionEditor = new ConditionEditor(true);
	connect(showConditionEditor, &ConditionEditor::changed, this, &ButtonEditDialog::refreshPreview);
	form->addRow(obs_module_text("OmniBar.Settings.ShowCondition"), showConditionEditor);

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
	connect(customColorButton, &QPushButton::clicked, this, &ButtonEditDialog::onCustomColorClicked);
	colorLayout->addWidget(customColorButton, 1);

	form->addRow(obs_module_text("OmniBar.Settings.AccentColor"), colorLayout);

	QLabel *colorHint = new QLabel(obs_module_text("OmniBar.Settings.AccentColorHint"));
	colorHint->setWordWrap(true);
	form->addRow(QString(), colorHint);

	return page;
}

QWidget *ButtonEditDialog::createActionTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	QFormLayout *typeForm = new QFormLayout();
	actionTypeCombo = new QComboBox();
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.None"), static_cast<int>(ActionType::None));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.Frontend"),
				 static_cast<int>(ActionType::Frontend));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceHotkey"),
				 static_cast<int>(ActionType::SourceHotkey));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceFilter"),
				 static_cast<int>(ActionType::SourceFilter));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceVisibility"),
				 static_cast<int>(ActionType::SourceVisibility));
	connect(actionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditDialog::onActionTypeChanged);
	typeForm->addRow(obs_module_text("OmniBar.Settings.ActionType"), actionTypeCombo);
	layout->addLayout(typeForm);

	actionStack = new QStackedWidget();

	// No action
	QWidget *nonePage = new QWidget();
	QVBoxLayout *noneLayout = new QVBoxLayout(nonePage);
	QLabel *noneHint = new QLabel(obs_module_text("OmniBar.Settings.NoActionHint"));
	noneHint->setWordWrap(true);
	noneLayout->addWidget(noneHint);
	noneLayout->addStretch();
	actionStack->addWidget(nonePage);

	// Frontend action
	QWidget *frontendPage = new QWidget();
	QFormLayout *frontendForm = new QFormLayout(frontendPage);
	frontendActionCombo = new QComboBox();
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.ToggleStream"), "toggle_streaming");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StartStream"), "start_streaming");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StopStream"), "stop_streaming");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.ToggleRecord"), "toggle_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StartRecord"), "start_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StopRecord"), "stop_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.TogglePause"), "toggle_pause_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.Pause"), "pause_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.Unpause"), "unpause_recording");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.ToggleReplay"), "toggle_replay_buffer");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StartReplay"), "start_replay_buffer");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StopReplay"), "stop_replay_buffer");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.SaveReplay"), "save_replay_buffer");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.ToggleVirtualCam"), "toggle_virtual_cam");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StartVirtualCam"), "start_virtual_cam");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.StopVirtualCam"), "stop_virtual_cam");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.ToggleStudioMode"), "toggle_studio_mode");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.EnableStudioMode"), "enable_studio_mode");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.DisableStudioMode"), "disable_studio_mode");
	frontendActionCombo->addItem(obs_module_text("OmniBar.Action.Transition"), "transition_to_program");
	connect(frontendActionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&ButtonEditDialog::refreshPreview);
	frontendForm->addRow(obs_module_text("OmniBar.Settings.Action"), frontendActionCombo);
	actionStack->addWidget(frontendPage);

	// Source hotkey
	QWidget *hotkeyPage = new QWidget();
	QFormLayout *hotkeyForm = new QFormLayout(hotkeyPage);
	hotkeySourceCombo = new QComboBox();
	hotkeyCombo = new QComboBox();
	connect(hotkeySourceCombo, &QComboBox::currentTextChanged, this, &ButtonEditDialog::onHotkeySourceChanged);
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
	connect(filterSourceCombo, &QComboBox::currentTextChanged, this, &ButtonEditDialog::onFilterSourceChanged);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Source"), filterSourceCombo);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Filter"), filterCombo);
	actionStack->addWidget(filterPage);

	// Source visibility
	QWidget *visibilityPage = new QWidget();
	QFormLayout *visibilityForm = new QFormLayout(visibilityPage);
	visibilitySceneCombo = new QComboBox();
	visibilitySourceCombo = new QComboBox();
	connect(visibilitySceneCombo, &QComboBox::currentTextChanged, this, &ButtonEditDialog::onSceneChanged);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Scene"), visibilitySceneCombo);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Source"), visibilitySourceCombo);
	actionStack->addWidget(visibilityPage);

	layout->addWidget(actionStack, 1);
	return page;
}

QWidget *ButtonEditDialog::createGroupTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	groupCheck = new QCheckBox(obs_module_text("OmniBar.Settings.IsGroup"));
	connect(groupCheck, &QCheckBox::toggled, this, &ButtonEditDialog::onGroupToggled);
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
	form->addRow(obs_module_text("OmniBar.Settings.GroupExpand"), groupExpandCombo);

	groupConditionEditor = new ConditionEditor(true);
	connect(groupConditionEditor, &ConditionEditor::changed, this,
		[this]() { onGroupToggled(groupCheck->isChecked()); });
	form->addRow(obs_module_text("OmniBar.Settings.GroupCondition"), groupConditionEditor);
	settingsLayout->addLayout(form);

	groupHint = new QLabel();
	groupHint->setWordWrap(true);
	settingsLayout->addWidget(groupHint);

	settingsLayout->addWidget(new QLabel(obs_module_text("OmniBar.Settings.GroupChildren")));

	childList = new QListWidget();
	childList->setSelectionMode(QAbstractItemView::SingleSelection);
	childList->setMinimumHeight(120);
	connect(childList, &QListWidget::itemDoubleClicked, this, &ButtonEditDialog::onChildDoubleClicked);
	settingsLayout->addWidget(childList, 1);

	QHBoxLayout *childButtons = new QHBoxLayout();
	addChildButton = new QPushButton(obs_module_text("OmniBar.Settings.AddButton"));
	editChildButton = new QPushButton(obs_module_text("OmniBar.Settings.EditButton"));
	removeChildButton = new QPushButton(obs_module_text("OmniBar.Settings.RemoveButton"));
	childUpButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveUp"));
	childDownButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveDown"));

	connect(addChildButton, &QPushButton::clicked, this, &ButtonEditDialog::onAddChild);
	connect(editChildButton, &QPushButton::clicked, this, &ButtonEditDialog::onEditChild);
	connect(removeChildButton, &QPushButton::clicked, this, &ButtonEditDialog::onRemoveChild);
	connect(childUpButton, &QPushButton::clicked, this, &ButtonEditDialog::onChildMoveUp);
	connect(childDownButton, &QPushButton::clicked, this, &ButtonEditDialog::onChildMoveDown);

	childButtons->addWidget(addChildButton);
	childButtons->addWidget(editChildButton);
	childButtons->addWidget(removeChildButton);
	childButtons->addWidget(childUpButton);
	childButtons->addWidget(childDownButton);
	settingsLayout->addLayout(childButtons);

	layout->addWidget(groupSettings, 1);
	return page;
}

void ButtonEditDialog::populateFromConfig()
{
	if (!buttonConfig)
		return;

	showConditionEditor->setCondition(buttonConfig->showCondition);

	if (spacerMode) {
		auto *spacerAction = dynamic_cast<SpacerAction *>(buttonConfig->action.get());
		spacerWidthSpin->setValue(spacerAction ? spacerAction->getWidth() : 10);
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

		workingChildren.clear();
		for (const auto &child : buttonConfig->children) {
			if (child)
				workingChildren.append(child->clone());
		}
		populateChildList();
		onGroupToggled(groupCheck->isChecked());
	}

	if (!buttonConfig->action)
		return;

	ActionType type = buttonConfig->action->getType();
	int typeIndex = actionTypeCombo->findData(static_cast<int>(type));
	if (typeIndex >= 0) {
		actionTypeCombo->setCurrentIndex(typeIndex);
		actionStack->setCurrentIndex(typeIndex);
	}

	switch (type) {
	case ActionType::Frontend: {
		auto *frontendAction = dynamic_cast<FrontendAction *>(buttonConfig->action.get());
		if (frontendAction) {
			QString actionName = FrontendAction::getFrontendActionName(frontendAction->getActionType());
			int actionIndex = frontendActionCombo->findData(actionName);
			if (actionIndex >= 0)
				frontendActionCombo->setCurrentIndex(actionIndex);
		}
		break;
	}
	case ActionType::SourceHotkey: {
		auto *hotkeyAction = dynamic_cast<SourceHotkeyAction *>(buttonConfig->action.get());
		if (hotkeyAction) {
			int sourceIndex = hotkeySourceCombo->findData(hotkeyAction->getSourceName());
			if (sourceIndex >= 0)
				hotkeySourceCombo->setCurrentIndex(sourceIndex);
			populateHotkeys(hotkeyAction->getSourceName());
			int hotkeyIndex = hotkeyCombo->findData(hotkeyAction->getHotkeyName());
			if (hotkeyIndex >= 0)
				hotkeyCombo->setCurrentIndex(hotkeyIndex);
		}
		break;
	}
	case ActionType::SourceFilter: {
		auto *filterAction = dynamic_cast<SourceFilterAction *>(buttonConfig->action.get());
		if (filterAction) {
			int sourceIndex = filterSourceCombo->findText(filterAction->getSourceName());
			if (sourceIndex >= 0) {
				filterSourceCombo->setCurrentIndex(sourceIndex);
				populateFilters(filterAction->getSourceName());
			}
			int filterIndex = filterCombo->findText(filterAction->getFilterName());
			if (filterIndex >= 0)
				filterCombo->setCurrentIndex(filterIndex);
		}
		break;
	}
	case ActionType::SourceVisibility: {
		auto *visAction = dynamic_cast<SourceVisibilityAction *>(buttonConfig->action.get());
		if (visAction) {
			int sceneIndex = visibilitySceneCombo->findText(visAction->getSceneName());
			if (sceneIndex >= 0) {
				visibilitySceneCombo->setCurrentIndex(sceneIndex);
				populateSceneItems(visAction->getSceneName());
			}
			int sourceIndex = visibilitySourceCombo->findText(visAction->getSourceName());
			if (sourceIndex >= 0)
				visibilitySourceCombo->setCurrentIndex(sourceIndex);
		}
		break;
	}
	case ActionType::None:
	case ActionType::Spacer:
	case ActionType::Divider:
		break;
	}
}

void ButtonEditDialog::updateConfigFromUI(ButtonConfig &target)
{
	target.showCondition = showConditionEditor->condition();

	if (spacerMode) {
		target.action = std::make_unique<SpacerAction>(spacerWidthSpin->value());
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

	QString iconData = iconCombo->currentData().toString();
	target.iconPath = iconData == "custom" ? customIconPath : iconData;

	if (allowGroup) {
		target.isGroup = groupCheck->isChecked();
		target.groupDisplay = static_cast<GroupDisplayMode>(groupDisplayCombo->currentData().toInt());
		target.groupExpand = static_cast<GroupExpandMode>(groupExpandCombo->currentData().toInt());
		target.expandCondition = groupConditionEditor->condition();
		target.children.clear();
		if (target.isGroup) {
			for (const auto &child : workingChildren) {
				if (child)
					target.children.append(child);
			}
		}
	}

	ActionType type = static_cast<ActionType>(actionTypeCombo->currentData().toInt());
	switch (type) {
	case ActionType::None:
		target.action = std::make_unique<NoAction>();
		break;
	case ActionType::Frontend: {
		QString actionName = frontendActionCombo->currentData().toString();
		target.action = std::make_unique<FrontendAction>(FrontendAction::getFrontendActionFromName(actionName));
		break;
	}
	case ActionType::SourceHotkey:
		target.action = std::make_unique<SourceHotkeyAction>(hotkeySourceCombo->currentData().toString(),
								     hotkeyCombo->currentData().toString());
		break;
	case ActionType::SourceFilter:
		target.action = std::make_unique<SourceFilterAction>(filterSourceCombo->currentText(),
								     filterCombo->currentText());
		break;
	case ActionType::SourceVisibility:
		target.action = std::make_unique<SourceVisibilityAction>(visibilitySceneCombo->currentText(),
									 visibilitySourceCombo->currentText());
		break;
	case ActionType::Spacer:
		target.action = std::make_unique<SpacerAction>(10);
		break;
	case ActionType::Divider:
		target.action = std::make_unique<DividerAction>();
		break;
	}
}

std::shared_ptr<ButtonConfig> ButtonEditDialog::previewConfig()
{
	auto config = std::make_shared<ButtonConfig>();
	config->id = buttonConfig ? buttonConfig->id : config->id;
	updateConfigFromUI(*config);
	return config;
}

void ButtonEditDialog::refreshPreview()
{
	if (!preview)
		return;
	preview->refresh(style, previewConfig(), previewActiveCheck && previewActiveCheck->isChecked());
}

void ButtonEditDialog::onActionTypeChanged(int index)
{
	actionStack->setCurrentIndex(index);
	if (allowGroup && groupCheck)
		onGroupToggled(groupCheck->isChecked());
	refreshPreview();
}

void ButtonEditDialog::onIconChanged(int index)
{
	Q_UNUSED(index);

	// Tinting only means anything for a file the plugin did not draw.
	tintIconCheck->setEnabled(iconCombo->currentData().toString() == "custom");

	updateIconPreview();
	refreshPreview();
}

void ButtonEditDialog::updateIconPreview()
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

void ButtonEditDialog::onIconBrowse()
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

void ButtonEditDialog::updateColorButton()
{
	setColorSwatch(customColorButton, customColorCheck && customColorCheck->isChecked() ? customColor : QColor());
}

void ButtonEditDialog::onCustomColorClicked()
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

void ButtonEditDialog::onGroupToggled(bool enabled)
{
	if (!groupSettings)
		return;

	groupSettings->setEnabled(enabled);

	if (!enabled) {
		groupHint->clear();
		return;
	}

	auto expandMode = static_cast<GroupExpandMode>(groupExpandCombo->currentData().toInt());
	auto actionType = static_cast<ActionType>(actionTypeCombo->currentData().toInt());
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

void ButtonEditDialog::populateChildList()
{
	childList->clear();
	for (const auto &child : workingChildren) {
		if (!child)
			continue;
		QListWidgetItem *item = new QListWidgetItem(child->displayText());
		item->setIcon(omniBarIconForConfig(child, palette().color(QPalette::Text), 16));
		childList->addItem(item);
	}
}

void ButtonEditDialog::onAddChild()
{
	auto config = std::make_shared<ButtonConfig>();
	config->action = std::make_unique<FrontendAction>(FrontendActionType::ToggleRecording);

	// Children never nest further, so the group tab is left out.
	ButtonEditDialog dialog(config, style, false, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	workingChildren.append(config);
	populateChildList();
	childList->setCurrentRow(workingChildren.size() - 1);
}

void ButtonEditDialog::onEditChild()
{
	int row = childList->currentRow();
	if (row < 0 || row >= workingChildren.size())
		return;

	ButtonEditDialog dialog(workingChildren[row], style, false, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	populateChildList();
	childList->setCurrentRow(row);
}

void ButtonEditDialog::onRemoveChild()
{
	int row = childList->currentRow();
	if (row < 0 || row >= workingChildren.size())
		return;

	workingChildren.removeAt(row);
	populateChildList();
}

void ButtonEditDialog::onChildMoveUp()
{
	int row = childList->currentRow();
	if (row <= 0)
		return;

	workingChildren.move(row, row - 1);
	populateChildList();
	childList->setCurrentRow(row - 1);
}

void ButtonEditDialog::onChildMoveDown()
{
	int row = childList->currentRow();
	if (row < 0 || row >= workingChildren.size() - 1)
		return;

	workingChildren.move(row, row + 1);
	populateChildList();
	childList->setCurrentRow(row + 1);
}

void ButtonEditDialog::onChildDoubleClicked(QListWidgetItem *item)
{
	Q_UNUSED(item);
	onEditChild();
}

void ButtonEditDialog::onFilterSourceChanged(const QString &sourceName)
{
	populateFilters(sourceName);
	refreshPreview();
}

void ButtonEditDialog::onHotkeySourceChanged(const QString &sourceName)
{
	Q_UNUSED(sourceName);
	populateHotkeys(hotkeySourceCombo->currentData().toString());
	refreshPreview();
}

void ButtonEditDialog::onSceneChanged(const QString &sceneName)
{
	populateSceneItems(sceneName);
	refreshPreview();
}

void ButtonEditDialog::onOk()
{
	// Turning a group back into a plain button drops what was inside it, so
	// say so rather than discarding the buttons quietly.
	if (allowGroup && groupCheck && !groupCheck->isChecked() && !workingChildren.isEmpty()) {
		QMessageBox::StandardButton answer =
			QMessageBox::question(this, obs_module_text("OmniBar.Settings.TabGroup"),
					      obs_module_text("OmniBar.Settings.ConfirmUngroup"));
		if (answer != QMessageBox::Yes)
			return;
	}

	updateConfigFromUI(*buttonConfig);
	accept();
}

void ButtonEditDialog::onCancel()
{
	reject();
}

void ButtonEditDialog::populateSources()
{
	hotkeySourceCombo->clear();
	filterSourceCombo->clear();

	// Hotkeys that belong to OBS rather than to a source.
	hotkeySourceCombo->addItem(obs_module_text("OmniBar.Settings.GlobalHotkeys"), QString());

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			ButtonEditDialog *dialog = static_cast<ButtonEditDialog *>(data);
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				dialog->hotkeySourceCombo->addItem(sourceName, sourceName);
				dialog->filterSourceCombo->addItem(sourceName);
			}
			return true;
		},
		this);

	// Scenes accept filters and register hotkeys too.
	obs_enum_scenes(
		[](void *data, obs_source_t *source) {
			ButtonEditDialog *dialog = static_cast<ButtonEditDialog *>(data);
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				dialog->hotkeySourceCombo->addItem(sourceName, sourceName);
				dialog->filterSourceCombo->addItem(sourceName);
			}
			return true;
		},
		this);

	populateHotkeys(QString());
}

void ButtonEditDialog::populateScenes()
{
	visibilitySceneCombo->clear();

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *name = obs_source_get_name(scenes.sources.array[i]);
		if (name) {
			visibilitySceneCombo->addItem(QString::fromUtf8(name));
		}
	}
	obs_frontend_source_list_free(&scenes);
}

void ButtonEditDialog::populateFilters(const QString &sourceName)
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
				combo->addItem(QString::fromUtf8(name));
			}
		},
		filterCombo);

	obs_source_release(source);
}

void ButtonEditDialog::populateSceneItems(const QString &sceneName)
{
	visibilitySourceCombo->clear();
	if (sceneName.isEmpty())
		return;

	obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
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
				combo->addItem(QString::fromUtf8(name));
			}
			return true;
		},
		visibilitySourceCombo);

	obs_source_release(sceneSource);
}

void ButtonEditDialog::populateHotkeys(const QString &sourceName)
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

OmniBarConfig::OmniBarConfig(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("OmniBar.Settings.Title"));
	setMinimumSize(560, 520);
	setupUI();
	loadSettings();
	adjustSize();
}

OmniBarConfig::~OmniBarConfig()
{
	if (instance == this)
		instance = nullptr;
}

void OmniBarConfig::showDialog()
{
	if (!instance) {
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		instance = new OmniBarConfig(mainWindow);
		instance->setAttribute(Qt::WA_DeleteOnClose);
	}
	instance->show();
	instance->raise();
	instance->activateWindow();
}

void OmniBarConfig::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QTabWidget *tabs = new QTabWidget();
	tabs->addTab(createButtonsTab(), obs_module_text("OmniBar.Settings.Buttons"));
	tabs->addTab(createAppearanceTab(), obs_module_text("OmniBar.Settings.TabAppearance"));
	mainLayout->addWidget(tabs, 1);

	QDialogButtonBox *buttonBox =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &OmniBarConfig::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &OmniBarConfig::onCancel);
	connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OmniBarConfig::onApply);
	mainLayout->addWidget(buttonBox);
}

QWidget *OmniBarConfig::createButtonsTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	buttonTree = new ButtonTreeWidget();
	connect(buttonTree, &QTreeWidget::itemDoubleClicked, this, &OmniBarConfig::onItemDoubleClicked);
	connect(buttonTree, &ButtonTreeWidget::itemsReordered, this, &OmniBarConfig::onTreeReordered);
	layout->addWidget(buttonTree, 1);

	QLabel *hint = new QLabel(obs_module_text("OmniBar.Settings.TreeHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	QHBoxLayout *listButtonLayout = new QHBoxLayout();
	addButton = new QPushButton(obs_module_text("OmniBar.Settings.AddButton"));
	editButton = new QPushButton(obs_module_text("OmniBar.Settings.EditButton"));
	duplicateButton = new QPushButton(obs_module_text("OmniBar.Settings.Duplicate"));
	removeButton = new QPushButton(obs_module_text("OmniBar.Settings.RemoveButton"));
	moveUpButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveUp"));
	moveDownButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveDown"));

	connect(addButton, &QPushButton::clicked, this, &OmniBarConfig::onAddButton);
	connect(editButton, &QPushButton::clicked, this, &OmniBarConfig::onEditButton);
	connect(duplicateButton, &QPushButton::clicked, this, &OmniBarConfig::onDuplicateButton);
	connect(removeButton, &QPushButton::clicked, this, &OmniBarConfig::onRemoveButton);
	connect(moveUpButton, &QPushButton::clicked, this, &OmniBarConfig::onMoveUp);
	connect(moveDownButton, &QPushButton::clicked, this, &OmniBarConfig::onMoveDown);

	listButtonLayout->addWidget(addButton);
	listButtonLayout->addWidget(editButton);
	listButtonLayout->addWidget(duplicateButton);
	listButtonLayout->addWidget(removeButton);
	listButtonLayout->addStretch();
	listButtonLayout->addWidget(moveUpButton);
	listButtonLayout->addWidget(moveDownButton);
	layout->addLayout(listButtonLayout);

	QGroupBox *dockGroup = new QGroupBox(obs_module_text("OmniBar.Settings.DockSettings"));
	QFormLayout *dockForm = new QFormLayout(dockGroup);
	dockPositionCombo = new QComboBox();
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Top"), static_cast<int>(DockPosition::Top));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Left"), static_cast<int>(DockPosition::Left));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Bottom"), static_cast<int>(DockPosition::Bottom));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Right"), static_cast<int>(DockPosition::Right));
	dockForm->addRow(obs_module_text("OmniBar.Settings.DockPosition"), dockPositionCombo);
	layout->addWidget(dockGroup);

	return page;
}

QWidget *OmniBarConfig::createAppearanceTab()
{
	QWidget *page = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(page);

	QFormLayout *presetForm = new QFormLayout();
	presetCombo = new QComboBox();
	presetCombo->addItem(obs_module_text("OmniBar.Preset.ObsNative"), static_cast<int>(StylePreset::ObsNative));
	presetCombo->addItem(obs_module_text("OmniBar.Preset.Compact"), static_cast<int>(StylePreset::Compact));
	presetCombo->addItem(obs_module_text("OmniBar.Preset.ModernRounded"),
			     static_cast<int>(StylePreset::ModernRounded));
	presetCombo->addItem(obs_module_text("OmniBar.Preset.NeonAccent"), static_cast<int>(StylePreset::NeonAccent));
	presetCombo->addItem(obs_module_text("OmniBar.Preset.Custom"), static_cast<int>(StylePreset::Custom));
	connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&OmniBarConfig::onPresetChanged);
	presetForm->addRow(obs_module_text("OmniBar.Settings.Preset"), presetCombo);
	layout->addLayout(presetForm);

	// Live preview of the bar itself.
	stylePreviewBar = new QWidget();
	stylePreviewBar->setObjectName("OmniBarPreviewBackdrop");
	QHBoxLayout *previewLayout = new QHBoxLayout(stylePreviewBar);
	previewLayout->setContentsMargins(8, 8, 8, 8);
	previewLayout->setAlignment(Qt::AlignLeft);

	struct SampleSpec {
		FrontendActionType action;
		bool checked;
	};
	const SampleSpec samples[] = {{FrontendActionType::ToggleStreaming, false},
				      {FrontendActionType::ToggleRecording, true},
				      {FrontendActionType::ToggleStudioMode, false}};

	for (const auto &sample : samples) {
		auto config = std::make_shared<ButtonConfig>();
		config->action = std::make_unique<FrontendAction>(sample.action);
		stylePreviewConfigs.append(config);

		QToolButton *button = new QToolButton(stylePreviewBar);
		button->setCheckable(true);
		button->setChecked(sample.checked);
		button->setFocusPolicy(Qt::NoFocus);
		button->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		previewLayout->addWidget(button);
		stylePreviewButtons.append(button);
	}
	layout->addWidget(stylePreviewBar);

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

	// Colours
	QGroupBox *colorGroup = new QGroupBox(obs_module_text("OmniBar.Settings.Colors"));
	QVBoxLayout *colorLayout = new QVBoxLayout(colorGroup);

	useCustomColorsCheck = new QCheckBox(obs_module_text("OmniBar.Settings.UseCustomColors"));
	connect(useCustomColorsCheck, &QCheckBox::toggled, this, &OmniBarConfig::onUseCustomColorsToggled);
	colorLayout->addWidget(useCustomColorsCheck);

	colorGrid = new QWidget();
	QGridLayout *grid = new QGridLayout(colorGrid);
	grid->setContentsMargins(0, 0, 0, 0);

	auto addColor = [&](QPushButton *&target, const char *labelKey, QColor BarStyle::*member, int row, int column) {
		grid->addWidget(new QLabel(obs_module_text(labelKey)), row, column * 2);
		QPushButton *swatch = new QPushButton();
		connect(swatch, &QPushButton::clicked, this,
			[this, swatch, member]() { pickColor(workingStyle.*member, swatch); });
		grid->addWidget(swatch, row, column * 2 + 1);
		target = swatch;
	};

	addColor(barBackgroundButton, "OmniBar.Settings.BarBackground", &BarStyle::barBackground, 0, 0);
	addColor(buttonBackgroundButton, "OmniBar.Settings.ButtonBackground", &BarStyle::buttonBackground, 0, 1);
	addColor(buttonHoverButton, "OmniBar.Settings.ButtonHover", &BarStyle::buttonHover, 1, 0);
	addColor(buttonCheckedButton, "OmniBar.Settings.ButtonChecked", &BarStyle::buttonChecked, 1, 1);
	addColor(buttonBorderButton, "OmniBar.Settings.ButtonBorder", &BarStyle::buttonBorder, 2, 0);
	addColor(textColorButton, "OmniBar.Settings.TextColor", &BarStyle::textColor, 2, 1);

	colorLayout->addWidget(colorGrid);
	layout->addWidget(colorGroup);
	layout->addStretch();

	return page;
}

void OmniBarConfig::pickColor(QColor &target, QPushButton *swatch)
{
	QColor chosen = QColorDialog::getColor(target.isValid() ? target : QColor(Qt::white), this,
					       obs_module_text("OmniBar.Settings.Colors"),
					       QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;

	target = chosen;
	setColorSwatch(swatch, chosen);

	// Hand-picked colours are only used when the bar is not tracking the
	// OBS theme, so turn that on rather than silently ignoring the choice.
	if (!useCustomColorsCheck->isChecked()) {
		updatingStyleWidgets = true;
		useCustomColorsCheck->setChecked(true);
		updatingStyleWidgets = false;
		workingStyle.useCustomColors = true;
		colorGrid->setEnabled(true);
	}

	onStyleValueChanged();
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

	int posIndex = dockPositionCombo->findData(static_cast<int>(SettingsManager::getDockPosition()));
	if (posIndex >= 0)
		dockPositionCombo->setCurrentIndex(posIndex);

	workingStyle = SettingsManager::getStyle();
	updateStyleWidgets();
}

void OmniBarConfig::saveSettings()
{
	rebuildModelFromTree();

	SettingsManager::setButtons(workingButtons);
	SettingsManager::setDockPosition(static_cast<DockPosition>(dockPositionCombo->currentData().toInt()));
	SettingsManager::setStyle(workingStyle);
	SettingsManager::save();

	if (OmniBar::getInstance()) {
		OmniBar::getInstance()->refreshFromConfiguration();
	}
}

QTreeWidgetItem *OmniBarConfig::createTreeItem(const std::shared_ptr<ButtonConfig> &config)
{
	QTreeWidgetItem *item = new QTreeWidgetItem();
	item->setText(0, config->displayText());
	item->setText(1, config->summaryText());
	item->setIcon(0, omniBarIconForConfig(config, palette().color(QPalette::Text), 16));
	item->setData(0, kConfigIdRole, config->id);
	item->setData(0, kConfigIsGroupRole, config->isGroup);

	Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
	// Only groups accept a drop, which is what caps nesting at one level.
	if (config->isGroup)
		flags |= Qt::ItemIsDropEnabled;
	item->setFlags(flags);

	configRegistry.insert(config->id, config);
	return item;
}

void OmniBarConfig::populateButtonTree()
{
	QString selectedId = buttonTree->currentItem() ? buttonTree->currentItem()->data(0, kConfigIdRole).toString()
						       : QString();

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
			// Should not happen - only groups accept drops - but
			// never drop buttons on the floor if it does.
			rebuilt.append(children);
		}
	}

	workingButtons = rebuilt;
}

void OmniBarConfig::onTreeReordered()
{
	rebuildModelFromTree();
	populateButtonTree();
}

void OmniBarConfig::addNewEntry(std::shared_ptr<ButtonConfig> config)
{
	rebuildModelFromTree();

	// Insert next to whatever is selected, staying inside a group when a
	// child of it is selected.
	QTreeWidgetItem *current = buttonTree->currentItem();
	auto currentConfig = configForItem(current);

	if (current && current->parent() && !config->isGroup && !config->isDecoration()) {
		auto parentConfig = configForItem(current->parent());
		if (parentConfig) {
			int row = parentConfig->children.indexOf(currentConfig);
			parentConfig->children.insert(row >= 0 ? row + 1 : parentConfig->children.size(), config);
			populateButtonTree();
			return;
		}
	}

	int row = currentConfig ? workingButtons.indexOf(currentConfig) : -1;
	workingButtons.insert(row >= 0 ? row + 1 : workingButtons.size(), config);
	populateButtonTree();
}

void OmniBarConfig::onAddButton()
{
	QMenu menu(this);
	QAction *buttonAction = menu.addAction(obs_module_text("OmniBar.Settings.AddPlainButton"));
	QAction *groupAction = menu.addAction(obs_module_text("OmniBar.Settings.AddGroup"));
	QAction *spacerAction = menu.addAction(obs_module_text("OmniBar.Settings.AddSpacer"));
	QAction *dividerAction = menu.addAction(obs_module_text("OmniBar.Settings.AddDivider"));

	QAction *chosen = menu.exec(addButton->mapToGlobal(QPoint(0, addButton->height())));
	if (!chosen)
		return;

	auto config = std::make_shared<ButtonConfig>();

	if (chosen == spacerAction || chosen == dividerAction) {
		if (chosen == spacerAction)
			config->action = std::make_unique<SpacerAction>(10);
		else
			config->action = std::make_unique<DividerAction>();

		ButtonEditDialog dialog(config, workingStyle, false, this);
		if (dialog.exec() == QDialog::Accepted)
			addNewEntry(config);
		return;
	}

	if (chosen == groupAction) {
		config->isGroup = true;
		config->action = std::make_unique<NoAction>();
		config->label = obs_module_text("OmniBar.Settings.NewGroup");
	} else if (chosen == buttonAction) {
		config->action = std::make_unique<FrontendAction>(FrontendActionType::ToggleStreaming);
		config->tooltip = obs_module_text("OmniBar.Action.ToggleStream");
		config->iconPath = "stream.svg";
	} else {
		return;
	}

	ButtonEditDialog dialog(config, workingStyle, true, this);
	if (dialog.exec() == QDialog::Accepted)
		addNewEntry(config);
}

void OmniBarConfig::onEditButton()
{
	QTreeWidgetItem *item = buttonTree->currentItem();
	auto config = configForItem(item);
	if (!config)
		return;

	rebuildModelFromTree();

	// A row nested under a group cannot itself become a group.
	bool allowGroup = item->parent() == nullptr;

	ButtonEditDialog dialog(config, workingStyle, allowGroup, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	populateButtonTree();
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

	rebuildModelFromTree();

	if (item->parent()) {
		auto parentConfig = configForItem(item->parent());
		if (parentConfig)
			parentConfig->children.removeAll(config);
	} else {
		workingButtons.removeAll(config);
	}

	populateButtonTree();
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
	if (row > 0)
		list->move(row, row - 1);

	populateButtonTree();
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
	if (row >= 0 && row < list->size() - 1)
		list->move(row, row + 1);

	populateButtonTree();
}

void OmniBarConfig::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
	Q_UNUSED(item);
	Q_UNUSED(column);
	onEditButton();
}

void OmniBarConfig::onPresetChanged(int index)
{
	Q_UNUSED(index);
	if (updatingStyleWidgets)
		return;

	auto preset = static_cast<StylePreset>(presetCombo->currentData().toInt());
	if (preset == StylePreset::Custom) {
		workingStyle.preset = StylePreset::Custom;
		return;
	}

	// A preset fills in every value; the user can then edit any of them.
	workingStyle = BarStyle::fromPreset(preset);
	updateStyleWidgets();
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
	workingStyle.useCustomColors = useCustomColorsCheck->isChecked();

	// Editing any value means this is no longer one of the named looks.
	if (workingStyle != BarStyle::fromPreset(workingStyle.preset)) {
		workingStyle.preset = StylePreset::Custom;
		updatingStyleWidgets = true;
		int index = presetCombo->findData(static_cast<int>(StylePreset::Custom));
		if (index >= 0)
			presetCombo->setCurrentIndex(index);
		updatingStyleWidgets = false;
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

	int presetIndex = presetCombo->findData(static_cast<int>(workingStyle.preset));
	if (presetIndex >= 0)
		presetCombo->setCurrentIndex(presetIndex);

	iconSizeSpin->setValue(workingStyle.iconSize);
	spacingSpin->setValue(workingStyle.spacing);
	buttonPaddingSpin->setValue(workingStyle.buttonPadding);
	cornerRadiusSpin->setValue(workingStyle.cornerRadius);
	borderWidthSpin->setValue(workingStyle.borderWidth);
	useCustomColorsCheck->setChecked(workingStyle.useCustomColors);
	colorGrid->setEnabled(workingStyle.useCustomColors);

	setColorSwatch(barBackgroundButton, workingStyle.barBackground);
	setColorSwatch(buttonBackgroundButton, workingStyle.buttonBackground);
	setColorSwatch(buttonHoverButton, workingStyle.buttonHover);
	setColorSwatch(buttonCheckedButton, workingStyle.buttonChecked);
	setColorSwatch(buttonBorderButton, workingStyle.buttonBorder);
	setColorSwatch(textColorButton, workingStyle.textColor);

	updatingStyleWidgets = false;

	refreshStylePreview();
}

void OmniBarConfig::refreshStylePreview()
{
	if (!stylePreviewBar)
		return;

	stylePreviewBar->setStyleSheet(workingStyle.panelStyleSheet(QStringLiteral("OmniBarPreviewBackdrop")));

	QHBoxLayout *layout = qobject_cast<QHBoxLayout *>(stylePreviewBar->layout());
	if (layout)
		layout->setSpacing(workingStyle.spacing);

	for (int i = 0; i < stylePreviewButtons.size() && i < stylePreviewConfigs.size(); i++) {
		bool checked = stylePreviewButtons[i]->isChecked();
		omniBarApplyButtonAppearance(stylePreviewButtons[i], stylePreviewConfigs[i], workingStyle);
		stylePreviewButtons[i]->setChecked(checked);
	}
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
