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
#include <obs-module.h>
#include <util/platform.h>
#include <QFile>

// Static member initialization
DockPosition SettingsManager::dockPosition = DockPosition::Top;
int SettingsManager::iconSize = 32;
int SettingsManager::buttonPadding = 4;
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

	QString configPath = getConfigPath();
	if (configPath.isEmpty())
		return;

	obs_data_t *data = obs_data_create_from_json_file(configPath.toUtf8().constData());
	if (!data) {
		loaded = true;
		return;
	}

	dockPosition = static_cast<DockPosition>(obs_data_get_int(data, "dock_position"));
	iconSize = static_cast<int>(obs_data_get_int(data, "icon_size"));
	if (iconSize <= 0)
		iconSize = 32;
	buttonPadding = static_cast<int>(obs_data_get_int(data, "button_padding"));
	if (buttonPadding < 0)
		buttonPadding = 4;

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
	obs_data_set_int(data, "icon_size", iconSize);
	obs_data_set_int(data, "button_padding", buttonPadding);

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
	dockPosition = position;
}

int SettingsManager::getIconSize()
{
	return iconSize;
}

void SettingsManager::setIconSize(int size)
{
	iconSize = size;
}

int SettingsManager::getButtonPadding()
{
	return buttonPadding;
}

void SettingsManager::setButtonPadding(int padding)
{
	buttonPadding = padding;
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
