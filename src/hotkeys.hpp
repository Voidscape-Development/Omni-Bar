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

// Hotkeys that move the bar to a given edge or take it off the window, so the
// position can be changed without opening the settings. They appear in the OBS
// hotkey settings unbound; the bindings are kept in the plugin's own
// configuration file, since OBS only persists hotkeys it owns itself.
namespace OmniBarHotkeys {

// Registers every hotkey. Call before the bindings are loaded, since a hotkey
// has to exist before OBS can be told what triggers it.
void registerAll();
void unregisterAll();

// Read from and written to the "hotkeys" object of the plugin configuration.
void loadBindings(obs_data_t *config);
void saveBindings(obs_data_t *config);

} // namespace OmniBarHotkeys
