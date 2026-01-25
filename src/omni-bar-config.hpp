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
#include <memory>

class ButtonConfig;

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
	void onButtonSelected(int row);
	void onButtonDoubleClicked(QListWidgetItem *item);
	void onActionTypeChanged(int index);
	void onApply();
	void onOk();
	void onCancel();
	void onIconBrowse();

private:
	void setupUI();
	void loadSettings();
	void saveSettings();
	void populateButtonList();
	void populateActionEditor(std::shared_ptr<ButtonConfig> config);
	void clearActionEditor();
	void updateCurrentButtonFromEditor();
	void populateSources();
	void populateScenes();
	void populateFilters(const QString &sourceName);
	void populateSceneItems(const QString &sceneName);
	void populateHotkeys();

	std::shared_ptr<ButtonConfig> createButtonFromEditor();

	static OmniBarConfig *instance;

	// Button list
	QListWidget *buttonList;
	QPushButton *addButton;
	QPushButton *removeButton;
	QPushButton *editButton;
	QPushButton *moveUpButton;
	QPushButton *moveDownButton;

	// Button editor
	QWidget *editorWidget;
	QComboBox *actionTypeCombo;
	QStackedWidget *actionStack;

	// Common button settings
	QLineEdit *tooltipEdit;
	QComboBox *iconCombo;
	QPushButton *iconBrowseButton;
	QCheckBox *expandableCheck;
	QCheckBox *expandWhenActiveCheck;

	// Frontend action editor
	QWidget *frontendPage;
	QComboBox *frontendActionCombo;

	// Source hotkey editor
	QWidget *hotkeyPage;
	QComboBox *hotkeySourceCombo;
	QComboBox *hotkeyCombo;

	// Source filter editor
	QWidget *filterPage;
	QComboBox *filterSourceCombo;
	QComboBox *filterCombo;

	// Source visibility editor
	QWidget *visibilityPage;
	QComboBox *visibilitySceneCombo;
	QComboBox *visibilitySourceCombo;

	// Spacer editor
	QWidget *spacerPage;
	QSpinBox *spacerWidthSpin;

	// Dock settings
	QComboBox *dockPositionCombo;
	QSpinBox *iconSizeSpin;
	QSpinBox *buttonPaddingSpin;

	// State
	QList<std::shared_ptr<ButtonConfig>> workingButtons;
	int currentEditIndex = -1;
	bool isEditing = false;
};
