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

#include <QDialog>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QCheckBox>
#include <QLabel>
#include <memory>

class ButtonConfig;

// Separate dialog for editing a single button
class ButtonEditDialog : public QDialog {
	Q_OBJECT

public:
	explicit ButtonEditDialog(std::shared_ptr<ButtonConfig> config, QWidget *parent = nullptr);
	~ButtonEditDialog();

	std::shared_ptr<ButtonConfig> getButtonConfig() const { return buttonConfig; }

private slots:
	void onActionTypeChanged(int index);
	void onIconChanged(int index);
	void onIconBrowse();
	void onSourceChanged(const QString &sourceName);
	void onSceneChanged(const QString &sceneName);
	void onOk();
	void onCancel();

private:
	void setupUI();
	void populateFromConfig();
	void updateConfigFromUI();
	void populateSources();
	void populateScenes();
	void populateFilters(const QString &sourceName);
	void populateSceneItems(const QString &sceneName);
	void populateHotkeys();
	void updateIconPreview();

	std::shared_ptr<ButtonConfig> buttonConfig;
	QString customIconPath;

	// Common settings
	QLineEdit *tooltipEdit;
	QComboBox *iconCombo;
	QPushButton *iconBrowseButton;
	QLabel *iconPreview;

	// Action type
	QComboBox *actionTypeCombo;
	QStackedWidget *actionStack;

	// Frontend action
	QWidget *frontendPage;
	QComboBox *frontendActionCombo;

	// Source hotkey
	QWidget *hotkeyPage;
	QComboBox *hotkeySourceCombo;
	QComboBox *hotkeyCombo;

	// Source filter
	QWidget *filterPage;
	QComboBox *filterSourceCombo;
	QComboBox *filterCombo;

	// Source visibility
	QWidget *visibilityPage;
	QComboBox *visibilitySceneCombo;
	QComboBox *visibilitySourceCombo;

	// Spacer
	QWidget *spacerPage;
	QSpinBox *spacerWidthSpin;

	// Expandable options
	QCheckBox *expandableCheck;
	QCheckBox *expandWhenActiveCheck;
};

class OmniBarConfig : public QDialog {
	Q_OBJECT

public:
	explicit OmniBarConfig(QWidget *parent = nullptr);
	~OmniBarConfig();

	static void showDialog();

private slots:
	void onAddButton();
	void onRemoveButton();
	void onEditButton();
	void onMoveUp();
	void onMoveDown();
	void onButtonDoubleClicked(QListWidgetItem *item);
	void onApply();
	void onOk();
	void onCancel();

private:
	void setupUI();
	void loadSettings();
	void saveSettings();
	void populateButtonList();

	static OmniBarConfig *instance;

	// Button list
	QListWidget *buttonList;
	QPushButton *addButton;
	QPushButton *removeButton;
	QPushButton *editButton;
	QPushButton *moveUpButton;
	QPushButton *moveDownButton;

	// Dock settings
	QComboBox *dockPositionCombo;
	QSpinBox *iconSizeSpin;
	QSpinBox *buttonPaddingSpin;

	// State
	QList<std::shared_ptr<ButtonConfig>> workingButtons;
};
