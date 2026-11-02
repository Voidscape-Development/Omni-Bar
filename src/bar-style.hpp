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
// the stored preset to Custom so the choice is not silently overwritten. The
// presets are stored by name, but the numbers are kept stable all the same.
enum class StylePreset {
	ObsNative = 0,
	Compact = 1,
	ModernRounded = 2,
	NeonAccent = 3,
	Custom = 4,
	Broadcast = 5,
	Capsule = 6,
	MonoLine = 7,
	HighContrast = 8,
	Phosphor = 9,
	Daylight = 10
};

// How a button shows that it is active. Fill paints the whole button in the
// active colour; the others leave the button's resting look alone and mark it
// with a line along the edge facing the window, a dot in the corner or a ring,
// tinting the icon to match. Stored as numbers, so existing values must keep
// theirs.
enum class ActiveIndicator { Fill = 0, Underline = 1, Dot = 2, Ring = 3 };

// What a button or readout is reporting on, which decides the colour it lights
// up in when state colours are on.
enum class BarState { None, Live, Recording, Paused, Replay, VirtualCam, Studio };

// What OBS is doing, as one value: either read from OBS right now, or made up
// by the settings dialog so the bar can be previewed as it would look live.
struct ObsStateSnapshot {
	bool streaming = false;
	bool recording = false;
	bool recordingPaused = false;
	bool replayBuffer = false;
	bool virtualCam = false;
	bool studioMode = false;

	static ObsStateSnapshot current();

	// Whether the thing a state colour stands for is running.
	bool isActive(BarState state) const;

	bool operator==(const ObsStateSnapshot &other) const;
	bool operator!=(const ObsStateSnapshot &other) const { return !(*this == other); }
};

// How urgently a readout wants looking at.
enum class Severity { Normal, Warning, Critical };

struct BarStyle {
	StylePreset preset = StylePreset::ObsNative;

	int iconSize = 32;
	int spacing = 4;
	int buttonPadding = 4;
	int cornerRadius = 4;
	int borderWidth = 0;

	// How a button set to pulse breathes while its action is active: one full
	// fade out and back in every pulsePeriod, dipping to pulseIntensity per
	// cent away from the solid active colour. Shared by every pulsing button
	// so they stay in step with one another.
	int pulsePeriod = 1400;
	int pulseIntensity = 60;

	ActiveIndicator indicator = ActiveIndicator::Fill;

	// When false the colours below are ignored at render time and derived
	// from the running OBS palette instead, so the bar tracks theme changes.
	bool useCustomColors = false;

	QColor barBackground;
	QColor buttonBackground;
	QColor buttonHover;
	QColor buttonChecked;
	QColor buttonBorder;
	QColor textColor;

	// Each output lights up in its own colour, so a live stream and a running
	// replay buffer never look alike. These are meanings rather than theme
	// colours, so they apply whether or not the bar follows the OBS theme.
	// When off, everything active uses buttonChecked as it always did.
	bool useStateColors = true;
	QColor stateLive;
	QColor stateRecording;
	QColor statePaused;
	QColor stateReplay;
	QColor stateVirtualCam;
	QColor stateStudio;

	// Readouts that have crossed a threshold - dropped frames climbing, the
	// recording drive filling up - are drawn in these.
	QColor warningColor;
	QColor criticalColor;

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

	// Colour something reporting on state lights up in: its state colour when
	// those are on and it has one, the shared active colour otherwise.
	QColor stateColor(BarState state) const;
	QColor severityColor(Severity severity) const;

	// Corner radius a button of the given size can actually take: a radius
	// larger than half the button would bend its edges inside out.
	int effectiveCornerRadius(int width, int height) const;

	// Stylesheet for the toolbar itself, including the rules that style every
	// button inside it.
	QString barStyleSheet() const;

	// Stylesheet for a single button that overrides the shared hover colour
	// with a tint of its own accent. Applied on the button widget, which
	// outranks the toolbar's sheet for the same properties. The active state
	// itself is painted by the button.
	QString buttonAccentStyleSheet(const QColor &accent) const;

	// Stylesheet for a readout. A readout has no hover or active state, so
	// every state looks like the resting one and the colour goes on the text,
	// which is the whole of what it draws. An invalid colour keeps the bar's.
	QString displayStyleSheet(const QColor &text) const;

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

// Black or white, whichever reads better on top of fill.
QColor omniBarContrastingColor(const QColor &fill);

// How much of the active colour a pulsing button should be showing right now,
// from 1 at the top of the breath down to 1 - intensity at the bottom. Driven
// by one clock shared across the whole plugin, so buttons that start pulsing at
// different moments still breathe together.
qreal omniBarPulseFactor(const BarStyle &style);
