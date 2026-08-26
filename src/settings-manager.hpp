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
#include <QString>
#include <QList>
#include <memory>

#include "bar-style.hpp"

class ButtonConfig;

// Where the bar docks. None keeps the plugin loaded with the bar taken off the
// window entirely, for anyone who only wants it while a hotkey calls it up. The
// numbers are written to the configuration file, so existing ones must keep
// their values.
enum class DockPosition { Top = 0, Left = 1, Bottom = 2, Right = 3, None = 4 };

class SettingsManager {
public:
	static void load();
	static void save();

	// Dock settings
	static DockPosition getDockPosition();
	static void setDockPosition(DockPosition position);

	// The last edge the bar was docked to, which is where the show/hide
	// hotkey puts it back. Never None, so there is always somewhere to
	// return to - even for a bar that has been hidden since it was installed.
	static DockPosition getLastVisiblePosition();

	// Bar and button styling
	static const BarStyle &getStyle();
	static void setStyle(const BarStyle &style);

	// Button configuration
	static QList<std::shared_ptr<ButtonConfig>> getButtons();
	static void setButtons(const QList<std::shared_ptr<ButtonConfig>> &buttons);

	static void addButton(std::shared_ptr<ButtonConfig> button);
	static void removeButton(int index);
	static void moveButton(int from, int to);

private:
	static QString getConfigPath();

	static DockPosition dockPosition;
	static DockPosition lastVisiblePosition;
	static BarStyle style;
	static QList<std::shared_ptr<ButtonConfig>> buttons;
	static bool loaded;
};
