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

#include <obs-data.h>
#include <QColor>
#include <QString>

// Named looks the user can pick from. Selecting a preset fills every value in
// BarStyle with that preset's numbers; editing any value afterwards switches
// the stored preset to Custom so the choice is not silently overwritten.
enum class StylePreset { ObsNative = 0, Compact = 1, ModernRounded = 2, NeonAccent = 3, Custom = 4 };

struct BarStyle {
	StylePreset preset = StylePreset::ObsNative;

	int iconSize = 32;
	int spacing = 4;
	int buttonPadding = 4;
	int cornerRadius = 4;
	int borderWidth = 0;

	// When false the colours below are ignored at render time and derived
	// from the running OBS palette instead, so the bar tracks theme changes.
	bool useCustomColors = false;

	QColor barBackground;
	QColor buttonBackground;
	QColor buttonHover;
	QColor buttonChecked;
	QColor buttonBorder;
	QColor textColor;

	static BarStyle fromPreset(StylePreset preset);
	static QString presetName(StylePreset preset);

	// The colours actually used for rendering: the stored ones when
	// useCustomColors is set, otherwise values derived from the OBS palette.
	QColor effectiveBarBackground() const;
	QColor effectiveButtonBackground() const;
	QColor effectiveButtonHover() const;
	QColor effectiveButtonChecked() const;
	QColor effectiveButtonBorder() const;
	QColor effectiveTextColor() const;

	// Stylesheet for the toolbar itself, including the rules that style every
	// button inside it.
	QString barStyleSheet() const;

	// Stylesheet for a single button that overrides the shared accent with
	// its own colour. Applied on the button widget, which outranks the
	// toolbar's sheet for the same properties.
	QString buttonAccentStyleSheet(const QColor &accent) const;

	// Stylesheet for a panel that renders buttons on an opaque backdrop: a
	// group's flyout, and the settings dialog's previews.
	QString panelStyleSheet(const QString &objectName) const;
	QString flyoutStyleSheet() const;

	// Outer size of an icon-only button at this style.
	int buttonExtent() const;

	obs_data_t *serialize() const;
	static BarStyle deserialize(obs_data_t *data);

	bool operator==(const BarStyle &other) const;
	bool operator!=(const BarStyle &other) const { return !(*this == other); }
};

// True when OBS is currently running a dark theme.
bool omniBarIsDarkTheme();
