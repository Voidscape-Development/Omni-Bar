# OBS Omni Bar

A configurable toolbar for OBS Studio. It docks to any edge of the main window and holds buttons that
drive OBS or your sources — streaming, recording, replay buffer, virtual camera, studio mode, source
visibility, filters and source hotkeys — with the bar and its buttons styled to taste.

Buttons light up while whatever they control is active, and hide themselves when their target no
longer exists, so a bar built for one scene collection does not leave dead buttons behind in another.

## Features

### Actions

Each button runs one of:

| Action | What it does |
| --- | --- |
| **Frontend action** | Any of 20 OBS operations: start / stop / toggle for streaming, recording, replay buffer, virtual camera and studio mode, plus pause, unpause and toggle pause for recording, save replay buffer, and transition to program |
| **Source hotkey** | Fires a hotkey registered by a specific source, picked by its readable description |
| **Source filter** | Toggles a filter on a source |
| **Source visibility** | Toggles a source's visibility within a scene |
| **No action** | Nothing on its own — for a group that only opens and closes |

Buttons that describe a state — toggle streaming, a filter, a scene item's visibility — show that
state, so the bar doubles as a status readout.

### Groups

A button can be a group holding other buttons. Each group chooses independently:

- **How children appear** — a floating flyout panel, or inline in the bar
- **What expands it** — clicking it, hovering it, or its own action becoming active
- **Whether it acts** — a group may also run an action of its own; in click mode the chevron corner
  expands the group while the rest of the button runs that action

Groups hold buttons one level deep; a child is never itself a group.

### Conditions

Any entry can be tied to what OBS is currently doing. Each carries an optional
condition — **streaming**, **recording**, **recording paused**, **replay buffer
running**, **virtual camera running**, **studio mode on**, or the button's own
action being active — which can be inverted to mean *while it is not*.

- On a **button, spacer or divider**, the condition decides whether it appears
  at all: a Save Replay button that shows up only once the buffer is running, or
  a pre-flight group that disappears the moment you go live.
- On a **group**, it also drives expansion. With *Expand on: Condition is met*
  the group opens and closes by itself — a recording group that unfolds its
  pause and stop buttons while recording, without the group having to be a
  recording button. With *Click* or *Hover* the condition instead gates whether
  the group can be opened at all.

### Layout

- **Spacer** — blank space of a chosen size
- **Divider** — a line across the bar, with configurable thickness, length (as a percentage of the
  bar's thickness, so it can be inset from the edges) and an optional colour

Both turn with the bar when it is docked left or right.

### Appearance

Every button carries an optional label, placed **left of**, **right of**, **above** or **below** its
icon, or shown alone. Icons come from the bundled set — every action has its own, so start, stop and
toggle never look alike — or from your own image file. Bundled icons follow the bar's text colour
automatically; a custom icon is drawn as authored unless you ask for it to be tinted. Any button can
override the shared accent colour used for its hover and active states.

A button can also be set to **pulse while active**: its active colour breathes in and out for as long
as its action is running, so a live button is noticeable out of the corner of your eye. How fast the
breath runs and how far the colour fades are set once for the whole bar, so every pulsing button
breathes in step.

The bar itself starts from a preset:

| Preset | Look |
| --- | --- |
| **OBS Native** | Flat and transparent, following the current OBS theme |
| **Compact** | Small icons and tight spacing, for many buttons in a small space |
| **Modern Rounded** | Rounded, raised buttons with a soft border and generous padding |
| **Neon Accent** | Dark bar with a bright accent on hover and active states |

Every value a preset sets — icon size, spacing, button padding, corner radius, border width, pulse
speed and intensity, and the bar, button, hover, active, border and text colours — stays individually
editable, and changing one switches the preset to Custom. Colours track the OBS theme until you turn
custom colours on.

### Position and hotkeys

The bar docks to the **top**, **left**, **bottom** or **right** edge of the main window, or to
**none** of them — which takes it off the window entirely, for anyone who would rather call it up
only when it is needed. The settings stay reachable from **Tools → Omni Bar Settings** while the bar
is hidden.

Five hotkeys under **Settings → Hotkeys** move the bar without opening the settings: one per edge,
plus one that hides it. They start unbound. A hotkey changes the position for good, exactly as the
settings dialog does, so the bar comes back where you left it after a restart.

## Installation

Download the package for your platform from the
[Releases](https://github.com/Voidscape-Development/Omni-Bar/releases) page.

- **Windows** — run the installer, or use the portable `.zip` if you run OBS portable
- **macOS** — open the `.pkg`
- **Linux** — install the `.deb`, or extract the `.tar.xz` into your OBS plugin directory

Requires OBS Studio 31.1.1 or newer.

## Usage

Open **Tools → Omni Bar Settings**, or right-click the bar itself and choose **Configure Omni Bar…**

The **Buttons** tab lists what is on the bar. **Add** offers a button, a group, a spacer or a divider.
Drag a row to reorder it, or drop it onto a group row to move it into that group. The same tab sets
which edge of the window the bar docks to, or hides it with **None**.

Double-click a row to edit it. The editor previews the button as the bar will actually draw it while
you change its label, icon, colour and action.

The **Appearance** tab holds the presets and every style value, with a live preview of the bar.

Settings are written to `config.json` in the plugin's configuration directory, alongside your other
OBS plugin settings.

## Building

The project uses the standard OBS plugin build system and CMake presets.

| Platform | Requirements |
| --- | --- |
| Windows | Visual Studio 17 2022, CMake 3.30.5 |
| macOS | Xcode 16.0, CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential` |

```sh
cmake --preset windows-x64      # or macos, or ubuntu-x86_64
cmake --build --preset windows-x64
```

Dependencies — the OBS sources, `obs-deps` and Qt6 — are pinned in `buildspec.json` and fetched by the
configure step.

### Source layout

| File | Responsibility |
| --- | --- |
| `src/plugin-main.cpp` | Module entry point; creates the bar and the Tools menu entry |
| `src/omni-bar.cpp` | The toolbar, its buttons, group expansion, flyouts and dividers |
| `src/omni-bar-config.cpp` | Settings dialog and the per-button editor |
| `src/button-action.cpp` | Action types and the button configuration model |
| `src/bar-style.cpp` | Style presets and the stylesheets built from them |
| `src/hotkeys.cpp` | The position hotkeys and their bindings |
| `src/settings-manager.cpp` | Loading and saving `config.json` |

### Formatting

CI checks both C++ and CMake formatting, and both are pinned to specific versions — a different
version will disagree.

```sh
clang-format-19 -i src/*.cpp src/*.hpp
gersemi -i CMakeLists.txt          # gersemi 0.21.0
```

## Contributing

Issues and pull requests are welcome. Please run the formatters above before opening a pull request,
since CI fails on any formatting difference in a file you changed.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
