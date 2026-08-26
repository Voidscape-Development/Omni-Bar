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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QMainWindow>
#include <QAction>

#include "hotkeys.hpp"
#include "omni-bar.hpp"
#include "omni-bar-config.hpp"
#include "settings-manager.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static OmniBar *omniBar = nullptr;

static void onFrontendEvent(enum obs_frontend_event event, void *data)
{
	UNUSED_PARAMETER(data);

	// Hotkey bindings live in this plugin's configuration file, and nothing
	// tells us when the user rebinds one in the OBS hotkey settings, so the
	// file is written once more on the way out.
	if (event == OBS_FRONTEND_EVENT_EXIT)
		SettingsManager::save();
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	// The hotkeys have to exist before the configuration that binds keys to
	// them is read.
	OmniBarHotkeys::registerAll();

	// Load settings
	SettingsManager::load();

	// Get main window
	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "Failed to get main window");
		return false;
	}

	// Create and attach toolbar
	obs_frontend_push_ui_translation(obs_module_get_string);

	omniBar = new OmniBar(mainWindow);
	omniBar->attachToMainWindow(mainWindow);

	// Add Tools menu entry
	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("OmniBar.Settings.Title")));
	if (action) {
		QObject::connect(action, &QAction::triggered, []() { OmniBarConfig::showDialog(); });
	}

	obs_frontend_pop_ui_translation();

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "OmniBar initialized");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	OmniBarHotkeys::unregisterAll();

	obs_log(LOG_INFO, "plugin unloaded");
}

const char *obs_module_name(void)
{
	return "Omni Bar";
}

const char *obs_module_description(void)
{
	return "A configurable toolbar with dynamic hotkey buttons for OBS Studio";
}
