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
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QPalette>
#include <QFile>

// Helper function to check if dark theme is active
static bool isDarkTheme()
{
	QPalette palette = QApplication::palette();
	QColor windowColor = palette.color(QPalette::Window);
	int luminance = (windowColor.red() * 299 + windowColor.green() * 587 + windowColor.blue() * 114) / 1000;
	return luminance < 128;
}

// Helper function to get themed icon path
static QString getThemedIconPath(const QString &basePath)
{
	if (basePath.isEmpty() || basePath == "custom")
		return QString();

	QString suffix = isDarkTheme() ? "_dark" : "_light";

	// Try themed variant first
	int dotIndex = basePath.lastIndexOf('.');
	if (dotIndex > 0) {
		QString themedPath = basePath.left(dotIndex) + suffix + basePath.mid(dotIndex);
		char *fullPath = obs_module_file(themedPath.toUtf8().constData());
		if (fullPath) {
			QString result = QString::fromUtf8(fullPath);
			bfree(fullPath);
			return result;
		}
	}

	// Fall back to base path
	char *fullPath = obs_module_file(("icons/" + basePath).toUtf8().constData());
	if (fullPath) {
		QString result = QString::fromUtf8(fullPath);
		bfree(fullPath);
		return result;
	}

	return QString();
}

// ============================================================================
// ButtonEditDialog Implementation
// ============================================================================

ButtonEditDialog::ButtonEditDialog(std::shared_ptr<ButtonConfig> config, QWidget *parent)
	: QDialog(parent), buttonConfig(config)
{
	setWindowTitle(obs_module_text("OmniBar.Settings.EditButton"));
	setMinimumSize(450, 400);
	setupUI();
	populateSources();
	populateScenes();
	populateHotkeys();
	populateFromConfig();
}

ButtonEditDialog::~ButtonEditDialog() {}

void ButtonEditDialog::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QFormLayout *formLayout = new QFormLayout();

	// Tooltip
	tooltipEdit = new QLineEdit();
	formLayout->addRow(obs_module_text("OmniBar.Settings.Tooltip"), tooltipEdit);

	// Icon selection
	QHBoxLayout *iconLayout = new QHBoxLayout();
	iconCombo = new QComboBox();
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Stream"), "stream.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Record"), "record.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Pause"), "pause.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Replay"), "replay.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.SaveReplay"), "save-replay.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.VirtualCam"), "virtual-cam.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.StudioMode"), "studio-mode.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Visibility"), "visibility.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Filter"), "filter.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Hotkey"), "hotkey.svg");
	iconCombo->addItem(obs_module_text("OmniBar.Icon.Custom"), "custom");
	connect(iconCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditDialog::onIconChanged);
	iconLayout->addWidget(iconCombo, 1);

	iconBrowseButton = new QPushButton("...");
	iconBrowseButton->setFixedWidth(30);
	connect(iconBrowseButton, &QPushButton::clicked, this, &ButtonEditDialog::onIconBrowse);
	iconLayout->addWidget(iconBrowseButton);

	iconPreview = new QLabel();
	iconPreview->setFixedSize(32, 32);
	iconPreview->setScaledContents(true);
	iconPreview->setStyleSheet("QLabel { border: 1px solid #555; background-color: #2a2a2a; }");
	iconLayout->addWidget(iconPreview);

	formLayout->addRow(obs_module_text("OmniBar.Settings.Icon"), iconLayout);

	// Action type
	actionTypeCombo = new QComboBox();
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.Frontend"), static_cast<int>(ActionType::Frontend));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceHotkey"), static_cast<int>(ActionType::SourceHotkey));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceFilter"), static_cast<int>(ActionType::SourceFilter));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.SourceVisibility"), static_cast<int>(ActionType::SourceVisibility));
	actionTypeCombo->addItem(obs_module_text("OmniBar.ActionType.Spacer"), static_cast<int>(ActionType::Spacer));
	connect(actionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonEditDialog::onActionTypeChanged);
	formLayout->addRow(obs_module_text("OmniBar.Settings.ActionType"), actionTypeCombo);

	mainLayout->addLayout(formLayout);

	// Action-specific settings
	actionStack = new QStackedWidget();

	// Frontend action page
	frontendPage = new QWidget();
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
	frontendForm->addRow(obs_module_text("OmniBar.Settings.Action"), frontendActionCombo);
	actionStack->addWidget(frontendPage);

	// Hotkey page
	hotkeyPage = new QWidget();
	QFormLayout *hotkeyForm = new QFormLayout(hotkeyPage);
	hotkeySourceCombo = new QComboBox();
	hotkeyCombo = new QComboBox();
	hotkeyForm->addRow(obs_module_text("OmniBar.Settings.Source"), hotkeySourceCombo);
	hotkeyForm->addRow(obs_module_text("OmniBar.Settings.Hotkey"), hotkeyCombo);
	actionStack->addWidget(hotkeyPage);

	// Filter page
	filterPage = new QWidget();
	QFormLayout *filterForm = new QFormLayout(filterPage);
	filterSourceCombo = new QComboBox();
	filterCombo = new QComboBox();
	connect(filterSourceCombo, &QComboBox::currentTextChanged, this, &ButtonEditDialog::onSourceChanged);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Source"), filterSourceCombo);
	filterForm->addRow(obs_module_text("OmniBar.Settings.Filter"), filterCombo);
	actionStack->addWidget(filterPage);

	// Visibility page
	visibilityPage = new QWidget();
	QFormLayout *visibilityForm = new QFormLayout(visibilityPage);
	visibilitySceneCombo = new QComboBox();
	visibilitySourceCombo = new QComboBox();
	connect(visibilitySceneCombo, &QComboBox::currentTextChanged, this, &ButtonEditDialog::onSceneChanged);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Scene"), visibilitySceneCombo);
	visibilityForm->addRow(obs_module_text("OmniBar.Settings.Source"), visibilitySourceCombo);
	actionStack->addWidget(visibilityPage);

	// Spacer page
	spacerPage = new QWidget();
	QFormLayout *spacerForm = new QFormLayout(spacerPage);
	spacerWidthSpin = new QSpinBox();
	spacerWidthSpin->setRange(1, 100);
	spacerWidthSpin->setValue(10);
	spacerForm->addRow(obs_module_text("OmniBar.Settings.SpacerWidth"), spacerWidthSpin);
	actionStack->addWidget(spacerPage);

	mainLayout->addWidget(actionStack);

	// Expandable options
	QGroupBox *expandGroup = new QGroupBox(obs_module_text("OmniBar.Settings.ExpandableGroup"));
	QVBoxLayout *expandLayout = new QVBoxLayout(expandGroup);
	expandableCheck = new QCheckBox(obs_module_text("OmniBar.Settings.Expandable"));
	expandWhenActiveCheck = new QCheckBox(obs_module_text("OmniBar.Settings.ExpandWhenActive"));
	expandLayout->addWidget(expandableCheck);
	expandLayout->addWidget(expandWhenActiveCheck);
	mainLayout->addWidget(expandGroup);

	mainLayout->addStretch();

	// Dialog buttons
	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ButtonEditDialog::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &ButtonEditDialog::onCancel);
	mainLayout->addWidget(buttonBox);
}

void ButtonEditDialog::populateFromConfig()
{
	if (!buttonConfig)
		return;

	tooltipEdit->setText(buttonConfig->tooltip);
	expandableCheck->setChecked(buttonConfig->expandable);
	expandWhenActiveCheck->setChecked(buttonConfig->expandWhenActive);

	// Icon
	customIconPath = buttonConfig->iconPath;
	int iconIndex = iconCombo->findData(buttonConfig->iconPath);
	if (iconIndex >= 0) {
		iconCombo->setCurrentIndex(iconIndex);
	} else {
		iconCombo->setCurrentIndex(iconCombo->count() - 1); // Custom
	}
	updateIconPreview();

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
			if (actionIndex >= 0) {
				frontendActionCombo->setCurrentIndex(actionIndex);
			}
		}
		break;
	}
	case ActionType::SourceHotkey: {
		auto *hotkeyAction = dynamic_cast<SourceHotkeyAction *>(buttonConfig->action.get());
		if (hotkeyAction) {
			int sourceIndex = hotkeySourceCombo->findText(hotkeyAction->getSourceName());
			if (sourceIndex >= 0)
				hotkeySourceCombo->setCurrentIndex(sourceIndex);
			int hotkeyIndex = hotkeyCombo->findText(hotkeyAction->getHotkeyName());
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
	case ActionType::Spacer: {
		auto *spacerAction = dynamic_cast<SpacerAction *>(buttonConfig->action.get());
		if (spacerAction) {
			spacerWidthSpin->setValue(spacerAction->getWidth());
		}
		break;
	}
	}
}

void ButtonEditDialog::updateConfigFromUI()
{
	buttonConfig->tooltip = tooltipEdit->text();
	buttonConfig->expandable = expandableCheck->isChecked();
	buttonConfig->expandWhenActive = expandWhenActiveCheck->isChecked();

	// Icon
	if (iconCombo->currentData().toString() != "custom") {
		buttonConfig->iconPath = iconCombo->currentData().toString();
	} else {
		buttonConfig->iconPath = customIconPath;
	}

	// Action
	ActionType type = static_cast<ActionType>(actionTypeCombo->currentData().toInt());
	switch (type) {
	case ActionType::Frontend: {
		QString actionName = frontendActionCombo->currentData().toString();
		FrontendActionType actionType = FrontendAction::getFrontendActionFromName(actionName);
		buttonConfig->action = std::make_unique<FrontendAction>(actionType);
		break;
	}
	case ActionType::SourceHotkey: {
		buttonConfig->action = std::make_unique<SourceHotkeyAction>(hotkeySourceCombo->currentText(), hotkeyCombo->currentText());
		break;
	}
	case ActionType::SourceFilter: {
		buttonConfig->action = std::make_unique<SourceFilterAction>(filterSourceCombo->currentText(), filterCombo->currentText());
		break;
	}
	case ActionType::SourceVisibility: {
		buttonConfig->action = std::make_unique<SourceVisibilityAction>(visibilitySceneCombo->currentText(), visibilitySourceCombo->currentText());
		break;
	}
	case ActionType::Spacer: {
		buttonConfig->action = std::make_unique<SpacerAction>(spacerWidthSpin->value());
		break;
	}
	}
}

void ButtonEditDialog::onActionTypeChanged(int index)
{
	actionStack->setCurrentIndex(index);
}

void ButtonEditDialog::onIconChanged(int index)
{
	Q_UNUSED(index);
	updateIconPreview();
}

void ButtonEditDialog::updateIconPreview()
{
	QString iconPath;
	QString iconData = iconCombo->currentData().toString();

	if (iconData == "custom") {
		// Use custom icon path
		iconPath = customIconPath;
	} else {
		// Get themed icon path for built-in icons
		iconPath = getThemedIconPath("icons/" + iconData);
	}

	if (!iconPath.isEmpty() && QFile::exists(iconPath)) {
		QPixmap pixmap(iconPath);
		if (!pixmap.isNull()) {
			iconPreview->setPixmap(pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		} else {
			iconPreview->clear();
		}
	} else {
		iconPreview->clear();
	}
}

void ButtonEditDialog::onIconBrowse()
{
	QString file = QFileDialog::getOpenFileName(this, obs_module_text("OmniBar.Settings.SelectIcon"), QString(),
						    "Images (*.png *.svg *.ico *.jpg *.jpeg);;All files (*.*)");
	if (!file.isEmpty()) {
		customIconPath = file;
		iconCombo->setCurrentIndex(iconCombo->count() - 1); // Custom
		updateIconPreview();
	}
}

void ButtonEditDialog::onSourceChanged(const QString &sourceName)
{
	populateFilters(sourceName);
}

void ButtonEditDialog::onSceneChanged(const QString &sceneName)
{
	populateSceneItems(sceneName);
}

void ButtonEditDialog::onOk()
{
	updateConfigFromUI();
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

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			ButtonEditDialog *dialog = static_cast<ButtonEditDialog *>(data);
			const char *name = obs_source_get_name(source);
			if (name) {
				dialog->hotkeySourceCombo->addItem(QString::fromUtf8(name));
				dialog->filterSourceCombo->addItem(QString::fromUtf8(name));
			}
			return true;
		},
		this);
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

void ButtonEditDialog::populateHotkeys()
{
	hotkeyCombo->clear();

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			Q_UNUSED(id);
			QComboBox *combo = static_cast<QComboBox *>(data);
			const char *name = obs_hotkey_get_name(key);
			if (name) {
				combo->addItem(QString::fromUtf8(name));
			}
			return true;
		},
		hotkeyCombo);
}

// ============================================================================
// OmniBarConfig Implementation
// ============================================================================

OmniBarConfig *OmniBarConfig::instance = nullptr;

OmniBarConfig::OmniBarConfig(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("OmniBar.Settings.Title"));
	setMinimumSize(500, 400);
	setupUI();
	loadSettings();
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

	// Button list section
	QGroupBox *listGroup = new QGroupBox(obs_module_text("OmniBar.Settings.Buttons"));
	QVBoxLayout *listLayout = new QVBoxLayout(listGroup);

	buttonList = new QListWidget();
	buttonList->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(buttonList, &QListWidget::itemDoubleClicked, this, &OmniBarConfig::onButtonDoubleClicked);
	listLayout->addWidget(buttonList);

	QHBoxLayout *listButtonLayout = new QHBoxLayout();
	addButton = new QPushButton(obs_module_text("OmniBar.Settings.AddButton"));
	removeButton = new QPushButton(obs_module_text("OmniBar.Settings.RemoveButton"));
	editButton = new QPushButton(obs_module_text("OmniBar.Settings.EditButton"));
	moveUpButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveUp"));
	moveDownButton = new QPushButton(obs_module_text("OmniBar.Settings.MoveDown"));

	connect(addButton, &QPushButton::clicked, this, &OmniBarConfig::onAddButton);
	connect(removeButton, &QPushButton::clicked, this, &OmniBarConfig::onRemoveButton);
	connect(editButton, &QPushButton::clicked, this, &OmniBarConfig::onEditButton);
	connect(moveUpButton, &QPushButton::clicked, this, &OmniBarConfig::onMoveUp);
	connect(moveDownButton, &QPushButton::clicked, this, &OmniBarConfig::onMoveDown);

	listButtonLayout->addWidget(addButton);
	listButtonLayout->addWidget(removeButton);
	listButtonLayout->addWidget(editButton);
	listButtonLayout->addWidget(moveUpButton);
	listButtonLayout->addWidget(moveDownButton);
	listLayout->addLayout(listButtonLayout);

	mainLayout->addWidget(listGroup);

	// Dock settings section
	QGroupBox *dockGroup = new QGroupBox(obs_module_text("OmniBar.Settings.DockSettings"));
	QFormLayout *dockForm = new QFormLayout(dockGroup);

	dockPositionCombo = new QComboBox();
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Top"), static_cast<int>(DockPosition::Top));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Left"), static_cast<int>(DockPosition::Left));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Bottom"), static_cast<int>(DockPosition::Bottom));
	dockPositionCombo->addItem(obs_module_text("OmniBar.Position.Right"), static_cast<int>(DockPosition::Right));
	dockForm->addRow(obs_module_text("OmniBar.Settings.DockPosition"), dockPositionCombo);

	iconSizeSpin = new QSpinBox();
	iconSizeSpin->setRange(16, 128);
	iconSizeSpin->setValue(32);
	dockForm->addRow(obs_module_text("OmniBar.Settings.IconSize"), iconSizeSpin);

	buttonPaddingSpin = new QSpinBox();
	buttonPaddingSpin->setRange(0, 32);
	buttonPaddingSpin->setValue(4);
	dockForm->addRow(obs_module_text("OmniBar.Settings.ButtonPadding"), buttonPaddingSpin);

	mainLayout->addWidget(dockGroup);

	// Dialog buttons
	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &OmniBarConfig::onOk);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &OmniBarConfig::onCancel);
	connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OmniBarConfig::onApply);
	mainLayout->addWidget(buttonBox);
}

void OmniBarConfig::loadSettings()
{
	workingButtons = SettingsManager::getButtons();
	populateButtonList();

	int posIndex = dockPositionCombo->findData(static_cast<int>(SettingsManager::getDockPosition()));
	if (posIndex >= 0)
		dockPositionCombo->setCurrentIndex(posIndex);

	iconSizeSpin->setValue(SettingsManager::getIconSize());
	buttonPaddingSpin->setValue(SettingsManager::getButtonPadding());
}

void OmniBarConfig::saveSettings()
{
	SettingsManager::setButtons(workingButtons);
	SettingsManager::setDockPosition(static_cast<DockPosition>(dockPositionCombo->currentData().toInt()));
	SettingsManager::setIconSize(iconSizeSpin->value());
	SettingsManager::setButtonPadding(buttonPaddingSpin->value());
	SettingsManager::save();

	if (OmniBar::getInstance()) {
		OmniBar::getInstance()->refreshFromConfiguration();
	}
}

void OmniBarConfig::populateButtonList()
{
	buttonList->clear();
	for (const auto &config : workingButtons) {
		QString displayText = config->tooltip;
		if (displayText.isEmpty() && config->action) {
			displayText = config->action->getDisplayName();
		}
		if (displayText.isEmpty()) {
			displayText = config->id;
		}
		buttonList->addItem(displayText);
	}
}

void OmniBarConfig::onAddButton()
{
	auto config = std::make_shared<ButtonConfig>();
	config->action = std::make_unique<FrontendAction>(FrontendActionType::ToggleStreaming);
	config->tooltip = obs_module_text("OmniBar.Action.ToggleStream");
	config->iconPath = "stream.svg";

	ButtonEditDialog dialog(config, this);
	if (dialog.exec() == QDialog::Accepted) {
		workingButtons.append(config);
		populateButtonList();
		buttonList->setCurrentRow(workingButtons.size() - 1);
	}
}

void OmniBarConfig::onRemoveButton()
{
	int row = buttonList->currentRow();
	if (row >= 0 && row < workingButtons.size()) {
		workingButtons.removeAt(row);
		populateButtonList();
	}
}

void OmniBarConfig::onEditButton()
{
	int row = buttonList->currentRow();
	if (row >= 0 && row < workingButtons.size()) {
		ButtonEditDialog dialog(workingButtons[row], this);
		if (dialog.exec() == QDialog::Accepted) {
			populateButtonList();
			buttonList->setCurrentRow(row);
		}
	}
}

void OmniBarConfig::onMoveUp()
{
	int row = buttonList->currentRow();
	if (row > 0) {
		workingButtons.move(row, row - 1);
		populateButtonList();
		buttonList->setCurrentRow(row - 1);
	}
}

void OmniBarConfig::onMoveDown()
{
	int row = buttonList->currentRow();
	if (row >= 0 && row < workingButtons.size() - 1) {
		workingButtons.move(row, row + 1);
		populateButtonList();
		buttonList->setCurrentRow(row + 1);
	}
}

void OmniBarConfig::onButtonDoubleClicked(QListWidgetItem *item)
{
	Q_UNUSED(item);
	onEditButton();
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
