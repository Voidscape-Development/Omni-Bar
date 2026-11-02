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
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QCheckBox>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QHash>
#include <QPointer>
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
	// Always, while, or while not - then what is being watched, which is
	// only shown once there is something to watch.
	QComboBox *modeCombo;
	QComboBox *sourceCombo;
};

class QTreeWidgetItem;
class QButtonGroup;

// The searchable list an action is chosen from: everything a button can do,
// grouped by what it belongs to, with start, stop and toggle of the same thing
// offered as one entry. Typing narrows the list; arrow keys and Enter pick
// without leaving the search box.
class ActionPicker : public QWidget {
	Q_OBJECT

public:
	explicit ActionPicker(QWidget *parent = nullptr);

	// Opens under anchor with the current choice selected.
	void popup(QWidget *anchor, const QString &currentChoice);

	// Every choice the picker offers, by id, for code that needs a label or
	// icon without opening the picker.
	static QString choiceLabel(const QString &choice);
	static QString choiceIcon(const QString &choice);

signals:
	void chosen(const QString &choice);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void applyFilter(const QString &text);
	void choose(QTreeWidgetItem *item);
	void moveSelection(int step);

	QLineEdit *search;
	QTreeWidget *list;
};

// Inspector for a single button, group, spacer, divider or readout. Every
// change is written straight to the entry it was opened for and announced with
// changed(), so the list and the bar preview follow along as the user types;
// the settings window decides when any of it is saved.
class ButtonEditor : public QWidget {
	Q_OBJECT

public:
	ButtonEditor(std::shared_ptr<ButtonConfig> config, const BarStyle &style, bool allowGroup,
		     QWidget *parent = nullptr);

	std::shared_ptr<ButtonConfig> getButtonConfig() const { return buttonConfig; }

	// Puts the keyboard in the first field, for F2 and Enter in the list.
	void focusFirstField();

signals:
	void changed();

private slots:
	// A different kind of action was picked: show its settings.
	void setChoice(const QString &choice);
	void onIconChanged(int index);
	void onIconBrowse();
	void onFilterSourceChanged(const QString &sourceName);
	void onHotkeySourceChanged(const QString &sourceName);
	void onSceneChanged(const QString &sceneName);
	void onCustomColorClicked();
	void onDividerColorClicked();
	void onDividerValueChanged();
	void onGroupToggled(bool enabled);

private:
	void setupUI();
	void setupSpacerUI();
	void setupDividerUI();
	void buildIconCombo();
	void buildMetricCombo();
	QWidget *createAppearanceTab();
	QWidget *createActionTab();
	QWidget *createVisibilityTab();
	QWidget *createGroupTab();
	QLabel *createHeader();

	void populateFromConfig();
	// Writes the fields into target. A group's children are the list's to
	// manage and are never touched here.
	void updateConfigFromUI(ButtonConfig &target);
	void populateSources();
	void populateScenes();
	void populateFilters(const QString &sourceName);
	void populateSceneItems(const QString &sceneName);
	void populateHotkeys(const QString &sourceName);
	void populateAudioSources();
	void populateVariants();
	void updateIconPreview();
	void updatePickerButton();

	// The kind of action chosen, and for OBS's own actions the variant.
	ActionType currentActionType() const;
	FrontendActionType currentFrontendType() const;
	void updateColorButton();

	// Applies the fields to the entry and says so - unless the fields are
	// still being filled in from it.
	void refreshPreview();

	// The working values as a config of their own, for the icon preview.
	std::shared_ptr<ButtonConfig> previewConfig();

	std::shared_ptr<ButtonConfig> buttonConfig;
	BarStyle style;
	bool allowGroup;
	bool spacerMode;
	bool dividerMode;
	// A readout keeps the full editor - it has a label, an icon and a
	// condition like any other entry - with the action section replaced by
	// the choice of what it reads.
	bool displayEntryMode;
	bool populating = false;
	QLabel *headerLabel = nullptr;
	QString customIconPath;
	QColor customColor;
	QColor dividerColor;

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
	QString currentChoice = QStringLiteral("none");
	QPushButton *pickerButton = nullptr;
	ActionPicker *picker = nullptr;
	QStackedWidget *actionStack = nullptr;
	QWidget *variantRow = nullptr;
	QHBoxLayout *variantLayout = nullptr;
	QButtonGroup *variantGroup = nullptr;
	QLabel *frontendDescription = nullptr;
	QComboBox *sceneCombo = nullptr;
	QComboBox *audioSourceCombo = nullptr;
	QButtonGroup *muteModeGroup = nullptr;
	QComboBox *hotkeySourceCombo = nullptr;
	QComboBox *hotkeyCombo = nullptr;
	QComboBox *filterSourceCombo = nullptr;
	QComboBox *filterCombo = nullptr;
	QComboBox *visibilitySceneCombo = nullptr;
	QComboBox *visibilitySourceCombo = nullptr;
	QComboBox *confirmCombo = nullptr;
	QSpinBox *spacerWidthSpin = nullptr;
	QCheckBox *spacerExpandCheck = nullptr;

	// Display
	QComboBox *displayMetricCombo = nullptr;
	QComboBox *displayClockFormatCombo = nullptr;
	QSpinBox *displayWidthSpin = nullptr;
	QCheckBox *displayWarnCheck = nullptr;

	// Divider
	QSpinBox *dividerThicknessSpin = nullptr;
	QSpinBox *dividerLengthSpin = nullptr;
	QCheckBox *dividerColorCheck = nullptr;
	QPushButton *dividerColorButton = nullptr;

	// Group
	QCheckBox *groupCheck = nullptr;
	QWidget *groupSettings = nullptr;
	QComboBox *groupDisplayCombo = nullptr;
	QComboBox *groupExpandCombo = nullptr;
	ConditionEditor *groupConditionEditor = nullptr;
	QLabel *groupHint = nullptr;
};

class OmniBarPreview;
class QButtonGroup;
class QScrollArea;
class QSplitter;

// The settings window: the bar drawn across the top, the entries on the left
// with an inspector for the selected one beside them, and the bar's look on a
// second tab. Edits show up in the preview straight away and reach the real
// bar on Apply or OK.
class OmniBarConfig : public QDialog {
	Q_OBJECT

public:
	explicit OmniBarConfig(QWidget *parent = nullptr);
	~OmniBarConfig();

	// Opens the dialog, or brings it forward. With an entry id it also
	// selects that entry for editing, which is how the bar's right-click
	// menu edits the button under the pointer.
	static void showDialog(const QString &editId = QString());

	// A hotkey can move the bar while the dialog is open. Without this the
	// dialog would still be showing the old position and put it back on OK.
	static void dockPositionChanged(DockPosition position);

	// True while the dialog is open with edits not yet applied, which the
	// bar's own menu must not write over.
	static bool hasUnappliedChanges();

	// The saved entries were changed from the bar's menu: an open dialog
	// with nothing of its own to lose shows the new ones.
	static void reloadIfClean();

	// Opens the dialog with the Add menu showing, for "Add to bar...".
	static void showAddMenu();

private slots:
	void onAddButton();
	void onRemoveButton();
	void onEditButton();
	void onDuplicateButton();
	void onMoveUp();
	void onMoveDown();
	void onTreeReordered();
	void onSelectionChanged();
	void onEditorChanged();
	void onPresetChanged(int preset);
	void onStyleValueChanged();
	void onUseCustomColorsToggled(bool enabled);
	void onApply();
	void onOk();
	void onCancel();
	void onRevert();

public slots:
	// Closing with unsaved edits asks whether to keep them first, whether
	// the dialog is closed with Cancel, Escape or the window's own button.
	void reject() override;

private:
	void setupUI();
	QWidget *createButtonsTab();
	QWidget *createAppearanceTab();
	QWidget *createFooter();
	void loadSettings();
	void saveSettings();
	void populateButtonTree();
	void rebuildModelFromTree();
	void updateStyleWidgets();
	void refreshStylePreview();
	void refreshBarPreview();
	void refreshPresetCards();
	void addNewEntry(std::shared_ptr<ButtonConfig> config);
	void editEntry(const QString &id);
	void selectEntry(const QString &id);
	void showInspector(const std::shared_ptr<ButtonConfig> &config, bool allowGroup);
	void updateTreeItem(QTreeWidgetItem *item, const std::shared_ptr<ButtonConfig> &config);
	void markDirty();
	void setDirty(bool value);
	DockPosition selectedPosition() const;
	void setSelectedPosition(DockPosition position);

	// Config a tree row stands for, or nullptr for a row that has gone stale.
	std::shared_ptr<ButtonConfig> configForItem(QTreeWidgetItem *item) const;
	QTreeWidgetItem *createTreeItem(const std::shared_ptr<ButtonConfig> &config);
	QTreeWidgetItem *itemForId(const QString &id) const;

	static OmniBarConfig *instance;

	OmniBarPreview *barPreview = nullptr;
	QTabWidget *tabs = nullptr;

	// Entry list and inspector
	ButtonTreeWidget *buttonTree = nullptr;
	QToolButton *addButton = nullptr;
	QToolButton *removeButton = nullptr;
	QToolButton *duplicateButton = nullptr;
	QToolButton *moveUpButton = nullptr;
	QToolButton *moveDownButton = nullptr;
	QScrollArea *inspectorScroll = nullptr;
	QPointer<ButtonEditor> editor;

	// Footer
	QButtonGroup *positionGroup = nullptr;
	QLabel *unsavedLabel = nullptr;

	// Style
	QButtonGroup *presetGroup = nullptr;
	QHash<int, QToolButton *> presetCards;
	QSpinBox *iconSizeSpin = nullptr;
	QSpinBox *spacingSpin = nullptr;
	QSpinBox *buttonPaddingSpin = nullptr;
	QSpinBox *cornerRadiusSpin = nullptr;
	QSpinBox *borderWidthSpin = nullptr;
	QSpinBox *pulsePeriodSpin = nullptr;
	QSpinBox *pulseIntensitySpin = nullptr;
	QComboBox *indicatorCombo = nullptr;
	QCheckBox *useStateColorsCheck = nullptr;
	QWidget *stateColorGrid = nullptr;
	QList<QPair<QPushButton *, QColor BarStyle::*>> colorSwatches;
	QCheckBox *useCustomColorsCheck = nullptr;
	QWidget *colorGrid = nullptr;
	QPushButton *barBackgroundButton = nullptr;
	QPushButton *buttonBackgroundButton = nullptr;
	QPushButton *buttonHoverButton = nullptr;
	QPushButton *buttonCheckedButton = nullptr;
	QPushButton *buttonBorderButton = nullptr;
	QPushButton *textColorButton = nullptr;

	// State
	QList<std::shared_ptr<ButtonConfig>> workingButtons;
	QHash<QString, std::shared_ptr<ButtonConfig>> configRegistry;
	BarStyle workingStyle;
	bool updatingStyleWidgets = false;
	bool dirty = false;
};
