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
#include <QTreeWidget>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QCheckBox>
#include <QLabel>
#include <QToolButton>
#include <QHash>
#include <memory>

#include "bar-style.hpp"
#include "omni-bar.hpp"
#include "button-action.hpp"
#include "settings-manager.hpp"

class ButtonConfig;

// Tree of top-level buttons with a group's children nested under it. Rows can
// be dragged to reorder or to move a button in and out of a group; nesting is
// capped at one level.
class ButtonTreeWidget : public QTreeWidget {
	Q_OBJECT

public:
	explicit ButtonTreeWidget(QWidget *parent = nullptr);

signals:
	void itemsReordered();

protected:
	void dropEvent(QDropEvent *event) override;

private:
	bool isDropAllowed(QTreeWidgetItem *source, QTreeWidgetItem *target, DropIndicatorPosition position) const;
};

// Picks an OBS state to test against, with an option to invert it. Shared by
// the button, group and decoration editors so a condition is set the same way
// wherever it appears.
class ConditionEditor : public QWidget {
	Q_OBJECT

public:
	// allowOwnAction offers "this button's own action", which is meaningless
	// for a spacer or divider.
	explicit ConditionEditor(bool allowOwnAction, QWidget *parent = nullptr);

	void setCondition(const ActivationCondition &condition);
	ActivationCondition condition() const;

signals:
	void changed();

private:
	QComboBox *sourceCombo;
	QCheckBox *invertCheck;
};

// Renders one button exactly as the bar would, over the bar's own backdrop.
class ButtonPreview : public QWidget {
	Q_OBJECT

public:
	explicit ButtonPreview(QWidget *parent = nullptr);

	void refresh(const BarStyle &style, const std::shared_ptr<ButtonConfig> &config, bool active);

private:
	QWidget *backdrop;
	QBoxLayout *backdropLayout;
	// A real bar widget, so placements the bar draws itself - such as a label
	// above the icon - show up here too.
	QWidget *previewWidget = nullptr;
};

// Editor for a single button, group, spacer, divider or readout.
class ButtonEditDialog : public QDialog {
	Q_OBJECT

public:
	ButtonEditDialog(std::shared_ptr<ButtonConfig> config, const BarStyle &style, bool allowGroup,
			 QWidget *parent = nullptr);
	~ButtonEditDialog();

	std::shared_ptr<ButtonConfig> getButtonConfig() const { return buttonConfig; }

private slots:
	void onActionTypeChanged(int index);
	void onIconChanged(int index);
	void onIconBrowse();
	void onFilterSourceChanged(const QString &sourceName);
	void onHotkeySourceChanged(const QString &sourceName);
	void onSceneChanged(const QString &sceneName);
	void onCustomColorClicked();
	void onDividerColorClicked();
	void onDividerValueChanged();
	void onGroupToggled(bool enabled);
	void onAddChild();
	void onEditChild();
	void onRemoveChild();
	void onChildMoveUp();
	void onChildMoveDown();
	void onChildDoubleClicked(QListWidgetItem *item);
	void onOk();
	void onCancel();

private:
	void setupUI();
	void setupSpacerUI();
	void setupDividerUI();
	void buildIconCombo();
	void buildMetricCombo();
	QWidget *createAppearanceTab();
	QWidget *createActionTab();
	QWidget *createGroupTab();

	void populateFromConfig();
	void updateConfigFromUI(ButtonConfig &target);
	void populateSources();
	void populateScenes();
	void populateFilters(const QString &sourceName);
	void populateSceneItems(const QString &sceneName);
	void populateHotkeys(const QString &sourceName);
	void populateChildList();
	void updateIconPreview();
	void updateColorButton();
	void refreshPreview();

	// The config the preview renders: the working values, not the saved ones.
	std::shared_ptr<ButtonConfig> previewConfig();

	std::shared_ptr<ButtonConfig> buttonConfig;
	BarStyle style;
	bool allowGroup;
	bool spacerMode;
	bool dividerMode;
	// A readout keeps the full editor - it has a label, an icon and a
	// condition like any other entry - with the action tab replaced by the
	// choice of what it reads.
	bool displayEntryMode;
	QString customIconPath;
	QColor customColor;
	QColor dividerColor;
	QList<std::shared_ptr<ButtonConfig>> workingChildren;

	// Preview
	ButtonPreview *preview = nullptr;
	QCheckBox *previewActiveCheck = nullptr;

	// Appearance
	QLineEdit *labelEdit = nullptr;
	QComboBox *displayModeCombo = nullptr;
	QLineEdit *tooltipEdit = nullptr;
	QComboBox *iconCombo = nullptr;
	QPushButton *iconBrowseButton = nullptr;
	QLabel *iconPreview = nullptr;
	QCheckBox *tintIconCheck = nullptr;
	ConditionEditor *showConditionEditor = nullptr;
	QCheckBox *customColorCheck = nullptr;
	QPushButton *customColorButton = nullptr;
	QCheckBox *pulseCheck = nullptr;

	// Action
	QComboBox *actionTypeCombo = nullptr;
	QStackedWidget *actionStack = nullptr;
	QComboBox *frontendActionCombo = nullptr;
	QComboBox *hotkeySourceCombo = nullptr;
	QComboBox *hotkeyCombo = nullptr;
	QComboBox *filterSourceCombo = nullptr;
	QComboBox *filterCombo = nullptr;
	QComboBox *visibilitySceneCombo = nullptr;
	QComboBox *visibilitySourceCombo = nullptr;
	QSpinBox *spacerWidthSpin = nullptr;

	// Display
	QComboBox *displayMetricCombo = nullptr;
	QSpinBox *displayWidthSpin = nullptr;

	// Divider
	QSpinBox *dividerThicknessSpin = nullptr;
	QSpinBox *dividerLengthSpin = nullptr;
	QCheckBox *dividerColorCheck = nullptr;
	QPushButton *dividerColorButton = nullptr;
	OmniBarDivider *dividerPreview = nullptr;
	std::shared_ptr<ButtonConfig> dividerPreviewConfig;

	// Group
	QCheckBox *groupCheck = nullptr;
	QWidget *groupSettings = nullptr;
	QComboBox *groupDisplayCombo = nullptr;
	QComboBox *groupExpandCombo = nullptr;
	ConditionEditor *groupConditionEditor = nullptr;
	QLabel *groupHint = nullptr;
	QListWidget *childList = nullptr;
	QPushButton *addChildButton = nullptr;
	QPushButton *editChildButton = nullptr;
	QPushButton *removeChildButton = nullptr;
	QPushButton *childUpButton = nullptr;
	QPushButton *childDownButton = nullptr;

	QTabWidget *tabs = nullptr;
	int groupTabIndex = -1;
};

class OmniBarConfig : public QDialog {
	Q_OBJECT

public:
	explicit OmniBarConfig(QWidget *parent = nullptr);
	~OmniBarConfig();

	static void showDialog();

	// A hotkey can move the bar while the dialog is open. Without this the
	// dialog would still be showing the old position and put it back on OK.
	static void dockPositionChanged(DockPosition position);

private slots:
	void onAddButton();
	void onRemoveButton();
	void onEditButton();
	void onDuplicateButton();
	void onMoveUp();
	void onMoveDown();
	void onItemDoubleClicked(QTreeWidgetItem *item, int column);
	void onTreeReordered();
	void onPresetChanged(int index);
	void onStyleValueChanged();
	void onUseCustomColorsToggled(bool enabled);
	void onApply();
	void onOk();
	void onCancel();

private:
	void setupUI();
	QWidget *createButtonsTab();
	QWidget *createAppearanceTab();
	void loadSettings();
	void saveSettings();
	void populateButtonTree();
	void rebuildModelFromTree();
	void updateStyleWidgets();
	void refreshStylePreview();
	void addNewEntry(std::shared_ptr<ButtonConfig> config);
	void pickColor(QColor &target, QPushButton *swatch);

	// Config a tree row stands for, or nullptr for a row that has gone stale.
	std::shared_ptr<ButtonConfig> configForItem(QTreeWidgetItem *item) const;
	QTreeWidgetItem *createTreeItem(const std::shared_ptr<ButtonConfig> &config);

	static OmniBarConfig *instance;

	// Button list
	ButtonTreeWidget *buttonTree = nullptr;
	QPushButton *addButton = nullptr;
	QPushButton *removeButton = nullptr;
	QPushButton *editButton = nullptr;
	QPushButton *duplicateButton = nullptr;
	QPushButton *moveUpButton = nullptr;
	QPushButton *moveDownButton = nullptr;
	QComboBox *dockPositionCombo = nullptr;

	// Style
	QComboBox *presetCombo = nullptr;
	QSpinBox *iconSizeSpin = nullptr;
	QSpinBox *spacingSpin = nullptr;
	QSpinBox *buttonPaddingSpin = nullptr;
	QSpinBox *cornerRadiusSpin = nullptr;
	QSpinBox *borderWidthSpin = nullptr;
	QSpinBox *pulsePeriodSpin = nullptr;
	QSpinBox *pulseIntensitySpin = nullptr;
	QCheckBox *useCustomColorsCheck = nullptr;
	QWidget *colorGrid = nullptr;
	QPushButton *barBackgroundButton = nullptr;
	QPushButton *buttonBackgroundButton = nullptr;
	QPushButton *buttonHoverButton = nullptr;
	QPushButton *buttonCheckedButton = nullptr;
	QPushButton *buttonBorderButton = nullptr;
	QPushButton *textColorButton = nullptr;
	QWidget *stylePreviewBar = nullptr;
	QList<OmniBarButton *> stylePreviewButtons;

	// State
	QList<std::shared_ptr<ButtonConfig>> workingButtons;
	QHash<QString, std::shared_ptr<ButtonConfig>> configRegistry;
	BarStyle workingStyle;
	bool updatingStyleWidgets = false;
};
