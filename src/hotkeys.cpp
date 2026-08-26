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

#include "hotkeys.hpp"
#include "omni-bar.hpp"
#include "omni-bar-config.hpp"
#include "settings-manager.hpp"

#include <obs-module.h>
#include <obs-hotkey.h>
#include <QCoreApplication>
#include <QMetaObject>
#include <array>
#include <cstdint>
#include <optional>

namespace {

struct PositionHotkey {
	// Where this hotkey puts the bar, or nothing for the one that toggles
	// between hidden and the edge the bar was last on.
	std::optional<DockPosition> target;
	// Written into the OBS hotkey configuration, so it must not change.
	const char *name;
	const char *textKey;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

std::array<PositionHotkey, 6> hotkeys = {{
	{DockPosition::Top, "OmniBar.MoveTop", "OmniBar.Hotkey.MoveTop"},
	{DockPosition::Left, "OmniBar.MoveLeft", "OmniBar.Hotkey.MoveLeft"},
	{DockPosition::Bottom, "OmniBar.MoveBottom", "OmniBar.Hotkey.MoveBottom"},
	{DockPosition::Right, "OmniBar.MoveRight", "OmniBar.Hotkey.MoveRight"},
	{DockPosition::None, "OmniBar.Hide", "OmniBar.Hotkey.Hide"},
	{std::nullopt, "OmniBar.ToggleVisible", "OmniBar.Hotkey.Toggle"},
}};

// Runs on the UI thread: moving the bar touches widgets, and the settings file
// is written from here too. An empty target means the toggle, which takes the
// bar away and brings it back to where it was.
void applyPosition(std::optional<DockPosition> target)
{
	DockPosition position;
	if (target)
		position = *target;
	else if (SettingsManager::getDockPosition() == DockPosition::None)
		position = SettingsManager::getLastVisiblePosition();
	else
		position = DockPosition::None;

	if (SettingsManager::getDockPosition() == position)
		return;

	// A hotkey changes the position for good, exactly as the settings dialog
	// does, so the bar comes back where it was left after a restart.
	SettingsManager::setDockPosition(position);
	SettingsManager::save();

	if (OmniBar *bar = OmniBar::getInstance())
		bar->repositionToolbar();

	// An open settings dialog would otherwise still be showing - and on OK,
	// write back - the position the bar has just been moved away from.
	OmniBarConfig::dockPositionChanged(position);
}

void onPositionHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	if (!pressed)
		return;

	// The entry rather than the position, since the toggle has none.
	auto index = static_cast<size_t>(reinterpret_cast<intptr_t>(data));
	if (index >= hotkeys.size())
		return;

	std::optional<DockPosition> target = hotkeys[index].target;

	// Hotkeys do not necessarily arrive on the UI thread, so hand the work
	// over to it rather than moving widgets from underneath OBS.
	if (QCoreApplication *app = QCoreApplication::instance())
		QMetaObject::invokeMethod(app, [target]() { applyPosition(target); }, Qt::QueuedConnection);
}

} // namespace

void OmniBarHotkeys::registerAll()
{
	for (size_t i = 0; i < hotkeys.size(); i++) {
		PositionHotkey &entry = hotkeys[i];
		if (entry.id != OBS_INVALID_HOTKEY_ID)
			continue;

		entry.id = obs_hotkey_register_frontend(entry.name, obs_module_text(entry.textKey), onPositionHotkey,
							reinterpret_cast<void *>(static_cast<intptr_t>(i)));
	}
}

void OmniBarHotkeys::unregisterAll()
{
	for (auto &entry : hotkeys) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;

		obs_hotkey_unregister(entry.id);
		entry.id = OBS_INVALID_HOTKEY_ID;
	}
}

void OmniBarHotkeys::loadBindings(obs_data_t *config)
{
	if (!config)
		return;

	obs_data_t *stored = obs_data_get_obj(config, "hotkeys");
	if (!stored)
		return;

	for (auto &entry : hotkeys) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;

		obs_data_array_t *bindings = obs_data_get_array(stored, entry.name);
		if (!bindings)
			continue;

		obs_hotkey_load(entry.id, bindings);
		obs_data_array_release(bindings);
	}

	obs_data_release(stored);
}

void OmniBarHotkeys::saveBindings(obs_data_t *config)
{
	if (!config)
		return;

	obs_data_t *stored = obs_data_create();

	for (const auto &entry : hotkeys) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;

		obs_data_array_t *bindings = obs_hotkey_save(entry.id);
		if (!bindings)
			continue;

		obs_data_set_array(stored, entry.name, bindings);
		obs_data_array_release(bindings);
	}

	obs_data_set_obj(config, "hotkeys", stored);
	obs_data_release(stored);
}
