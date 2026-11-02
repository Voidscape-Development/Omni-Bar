# OBS Omni Bar

A configurable toolbar for OBS Studio. It docks to any edge of the main window and holds buttons that
drive OBS or your sources — streaming, recording, replay buffer, virtual camera, studio mode, source
visibility, filters and source hotkeys — alongside live readouts of what OBS is doing, with the bar
and everything on it styled to taste.

Buttons light up while whatever they control is active, and hide themselves when their target no
longer exists, so a bar built for one scene collection does not leave dead buttons behind in another.

## Features

### Actions

Each button runs one of:

| Action | What it does |
| --- | --- |
| **Frontend action** | Any of 20 OBS operations: start / stop / toggle for streaming, recording, replay buffer, virtual camera and studio mode, plus pause, unpause and toggle pause for recording, save replay buffer, and transition to program |
| **Switch to scene** | Puts a scene on air, as clicking it in the Scenes dock does, and lights up while it is there |
| **Mute or unmute** | Toggles, mutes or unmutes an audio source, and lights up while it is muted |
| **Source visibility** | Toggles a source's visibility within a scene, or within whichever scene is on air |
| **Source filter** | Toggles a filter on a source |
| **Source hotkey** | Fires a hotkey registered by a specific source, picked by its readable description |
| **No action** | Nothing on its own — for a group that only opens and closes |

The action is chosen from a searchable list grouped by what it belongs to. Start, stop and toggle of the
same thing are one entry, with the variant picked underneath, so typing "stop stream" finds Stream with
Stop one click away.

Buttons that describe a state — toggle streaming, a filter, a scene item's visibility — show that
state, so the bar doubles as a status readout.

Stopping the stream or a recording from the bar asks first exactly when the OBS controls would, following
the confirmation settings under **Settings → General**. Any button can instead be set to always ask, or
never.

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

- On a **button, display, spacer or divider**, the condition decides whether it
  appears at all: a Save Replay button that shows up only once the buffer is
  running, a stream timer that appears only while you are live, or a pre-flight
  group that disappears the moment you go live.
- On a **group**, it also drives expansion. With *Expand on: Condition is met*
  the group opens and closes by itself — a recording group that unfolds its
  pause and stop buttons while recording, without the group having to be a
  recording button. With *Click* or *Hover* the condition instead gates whether
  the group can be opened at all.

### Displays

A **Display** entry is a live number rather than a button: something to glance at while you work.
Add one from the same menu as a button, and pick what it reads:

| | |
| --- | --- |
| **Streaming** | Stream time, stream bitrate, dropped frames (count and percentage) |
| **Recording** | Recording time (which stands still while the recording is paused), recording bitrate, file size so far, free space on the recording drive |
| **Replay buffer and virtual camera** | How long each has been running, and how long since the last replay was saved |
| **Status** | One chip that reads OFF AIR, or LIVE and REC with their timers, coloured by what is going out and amber while a recording is paused |
| **Performance** | CPU usage, frame rate, average frame time, render lag frames, encoding lag frames, memory usage, and the time of day |

A display takes the same label, icon, placement and condition settings as a button — so it can carry
a prefix such as `LIVE`, sit beside its own glyph, and appear only while it has something to say. Its
colour setting recolours the value itself, and a **minimum width** stops the bar shuffling as the
digits change, since digits are rarely all the same width. A readout whose feature is not running
shows a placeholder rather than a zero. Timers start from OBS's own start, stop and pause events, so they
agree with the OBS status bar.

Readouts with a level worth watching change colour when they cross it: dropped, render-lag and
encoding-lag frames above 1% (critical above 5%), CPU above 75% (90%), the frame rate more than 5% under
target (15%), and under 10 GB free on the recording drive (2 GB). This can be turned off per readout.

### Layout

- **Spacer** — blank space of a chosen size, or a **flexible space** that takes whatever room the bar has
  left, pushing everything after it to the far end
- **Divider** — a line across the bar, with configurable thickness, length (as a percentage of the
  bar's thickness, so it can be inset from the edges) and an optional colour

Both turn with the bar when it is docked left or right.

### Appearance

Each output lights up in its own colour while it is running — live, recording, paused, replay buffer,
virtual camera and studio mode — and a theme can mark an active button with a **fill**, an **underline**
on the side facing the window, a **corner dot** or a **ring**.

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
| **Broadcast** | Graphite bar with underlined active buttons and broadcast tally colours |
| **Capsule** | Round buttons marked with a corner dot |
| **Mono Line** | No fills: outlined buttons and a ring for active, in the OBS theme's colours |
| **Neon Accent** | Dark bar with a bright accent on hover and active states |
| **Phosphor** | A green-screen terminal, with red kept for going out |
| **High Contrast** | Black and white with thick borders and yellow active fills |
| **Daylight** | A white bar for the light OBS themes |

Every value a preset sets — icon size, spacing, button padding, corner radius, border width, pulse
speed and intensity, the active-state style, the bar, button, hover, active, border and text colours, and
the state and warning colours — stays individually editable, and changing one switches the preset to
Custom. Colours track the OBS theme until you turn
custom colours on.

### Position and hotkeys

The bar docks to the **top**, **left**, **bottom** or **right** edge of the main window, or to
**none** of them — which takes it off the window entirely, for anyone who would rather call it up
only when it is needed. The settings stay reachable from **Tools → Omni Bar Settings** while the bar
is hidden.

Six hotkeys under **Settings → Hotkeys** move the bar without opening the settings: one per edge, one
that hides it, and one that shows and hides it — taking the bar away and putting it back on whichever
edge it was last on. They start unbound. A hotkey changes the position for good, exactly as the
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
Right-clicking a button or readout offers to edit it, choose when it is shown, move it along the bar,
or remove it, as well as to add an entry or move the bar to another edge. **Lock bar** takes the editing
out of that menu, so a stray click during a show changes nothing; the settings window still works.

The settings window shows the bar you are building across the top, drawn with the bar's own widgets.
**Preview as** shows it as it would look idle, live, recording or paused, with the replay buffer,
virtual camera or studio mode running too, so conditions, state colours and pulses can be checked
without going live. **Now** follows what OBS is actually doing. The preview can be turned vertical to
see the bar as it would sit on a side edge, and clicking an entry in it selects that entry for editing.

The **Buttons** tab lists what is on the bar, with the selected entry's settings beside it. Changes show
in the preview as you make them and reach the real bar on **Apply** or **OK**; **Revert** goes back to
what was last applied. The **+** button adds a button, a group, a display, a spacer, a flexible space or
a divider. Drag a row to reorder it, or drop it onto a group row to put it inside that group; turning a
group back into a plain button moves its buttons out rather than removing them. **Delete** removes the
selected row, **F2** or **Enter** jumps to its settings and **Ctrl+D** duplicates it. A button whose
source, filter or hotkey cannot be found is marked in the list and drawn faded in the preview, and its
settings keep the stored name rather than switching it to another source.

The settings for an entry start with what it does, then how it looks, when it is shown and, for a
group, how it opens.

The **Appearance** tab shows every theme as a small strip of the bar drawn in that theme, then every
style value under it. The row along the bottom of the window sets which edge the bar docks to, or hides
it.

Settings are written to `config.json` in the plugin's configuration directory, alongside your other
OBS plugin settings, whenever OBS saves its own. Closing the settings window with changes that have not
been applied asks whether to keep them.

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
| `src/bar-preview.cpp` | The bar preview across the top of the settings window |
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
