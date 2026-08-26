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

#include "settings-manager.hpp"
#include "button-action.hpp"
#include "hotkeys.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <QFile>

// Static member initialization. The style is left default-constructed here
// rather than built from a preset: static initialisation runs before the plugin
// is loaded, and preset colours are read from the Qt palette.
DockPosition SettingsManager::dockPosition = DockPosition::Top;
DockPosition SettingsManager::lastVisiblePosition = DockPosition::Top;
BarStyle SettingsManager::style;
QList<std::shared_ptr<ButtonConfig>> SettingsManager::buttons;
bool SettingsManager::loaded = false;

QString SettingsManager::getConfigPath()
{
	char *path = obs_module_config_path("config.json");
	QString result = path ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

void SettingsManager::load()
{
	if (loaded)
		return;

	style = BarStyle::fromPreset(StylePreset::ObsNative);

	QString configPath = getConfigPath();
	if (configPath.isEmpty()) {
		loaded = true;
		return;
	}

	obs_data_t *data = obs_data_create_from_json_file(configPath.toUtf8().constData());
	if (!data) {
		loaded = true;
		return;
	}

	int storedPosition = static_cast<int>(obs_data_get_int(data, "dock_position"));
	if (storedPosition >= static_cast<int>(DockPosition::Top) &&
	    storedPosition <= static_cast<int>(DockPosition::None))
		dockPosition = static_cast<DockPosition>(storedPosition);

	// An edge, never None, so the show/hide hotkey always has a way back. A
	// configuration written before this was stored falls back to whatever
	// position it does have.
	int storedVisible = static_cast<int>(obs_data_get_int(data, "last_visible_position"));
	if (obs_data_has_user_value(data, "last_visible_position") &&
	    storedVisible >= static_cast<int>(DockPosition::Top) &&
	    storedVisible < static_cast<int>(DockPosition::None))
		lastVisiblePosition = static_cast<DockPosition>(storedVisible);
	else if (dockPosition != DockPosition::None)
		lastVisiblePosition = dockPosition;

	OmniBarHotkeys::loadBindings(data);

	obs_data_t *styleData = obs_data_get_obj(data, "style");
	if (styleData) {
		style = BarStyle::deserialize(styleData);
		obs_data_release(styleData);
	} else {
		// Pre-styling configurations only stored these two numbers.
		if (obs_data_has_user_value(data, "icon_size")) {
			int iconSize = static_cast<int>(obs_data_get_int(data, "icon_size"));
			if (iconSize > 0)
				style.iconSize = iconSize;
		}
		if (obs_data_has_user_value(data, "button_padding")) {
			int padding = static_cast<int>(obs_data_get_int(data, "button_padding"));
			if (padding >= 0)
				style.spacing = padding;
		}
	}

	buttons.clear();
	obs_data_array_t *buttonArray = obs_data_get_array(data, "buttons");
	if (buttonArray) {
		size_t count = obs_data_array_count(buttonArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *buttonData = obs_data_array_item(buttonArray, i);
			if (buttonData) {
				auto config = ButtonConfig::deserialize(buttonData);
				if (config) {
					buttons.append(config);
				}
				obs_data_release(buttonData);
			}
		}
		obs_data_array_release(buttonArray);
	}

	obs_data_release(data);
	loaded = true;
}

void SettingsManager::save()
{
	QString configPath = getConfigPath();
	if (configPath.isEmpty())
		return;

	// Ensure config directory exists
	char *configDir = obs_module_config_path("");
	if (configDir) {
		os_mkdirs(configDir);
		bfree(configDir);
	}

	obs_data_t *data = obs_data_create();

	obs_data_set_int(data, "dock_position", static_cast<int>(dockPosition));
	obs_data_set_int(data, "last_visible_position", static_cast<int>(lastVisiblePosition));

	OmniBarHotkeys::saveBindings(data);

	obs_data_t *styleData = style.serialize();
	obs_data_set_obj(data, "style", styleData);
	obs_data_release(styleData);

	obs_data_array_t *buttonArray = obs_data_array_create();
	for (const auto &button : buttons) {
		obs_data_t *buttonData = button->serialize();
		if (buttonData) {
			obs_data_array_push_back(buttonArray, buttonData);
			obs_data_release(buttonData);
		}
	}
	obs_data_set_array(data, "buttons", buttonArray);
	obs_data_array_release(buttonArray);

	obs_data_save_json_safe(data, configPath.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

DockPosition SettingsManager::getDockPosition()
{
	return dockPosition;
}

void SettingsManager::setDockPosition(DockPosition position)
{
	// Every route to a real edge comes through here, so this is the one place
	// that has to remember it for the show/hide hotkey.
	if (position != DockPosition::None)
		lastVisiblePosition = position;

	dockPosition = position;
}

DockPosition SettingsManager::getLastVisiblePosition()
{
	return lastVisiblePosition == DockPosition::None ? DockPosition::Top : lastVisiblePosition;
}

const BarStyle &SettingsManager::getStyle()
{
	return style;
}

void SettingsManager::setStyle(const BarStyle &newStyle)
{
	style = newStyle;
}

QList<std::shared_ptr<ButtonConfig>> SettingsManager::getButtons()
{
	return buttons;
}

void SettingsManager::setButtons(const QList<std::shared_ptr<ButtonConfig>> &newButtons)
{
	buttons = newButtons;
}

void SettingsManager::addButton(std::shared_ptr<ButtonConfig> button)
{
	buttons.append(button);
}

void SettingsManager::removeButton(int index)
{
	if (index >= 0 && index < buttons.size()) {
		buttons.removeAt(index);
	}
}

void SettingsManager::moveButton(int from, int to)
{
	if (from >= 0 && from < buttons.size() && to >= 0 && to < buttons.size()) {
		buttons.move(from, to);
	}
}
