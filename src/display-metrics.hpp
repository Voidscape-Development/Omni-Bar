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

#include <QString>

// A single number a Display entry can show on the bar. The values are written
// to the configuration file only through the names below, so the numbers
// themselves are free to move; the names are not.
enum class DisplayMetric {
	StreamTime,
	StreamBitrate,
	StreamDropped,
	RecordTime,
	RecordBitrate,
	RecordSize,
	DiskFree,
	ReplayTime,
	ReplayLastSaved,
	VirtualCamTime,
	CpuUsage,
	Fps,
	FrameTime,
	RenderLag,
	EncodingLag,
	Memory,
	Clock
};

// How the Time of day readout writes the hour. Stored by name, like the metrics
// above, so the numbers are free to move.
enum class ClockFormat { Hours24 = 0, Hours12 = 1 };

// Reads the numbers behind the Display entries. One sampler serves the whole
// plugin: the readouts are cheap to format but not to collect, and several
// buttons showing the same metric must agree with one another.
namespace OmniBarMetrics {

// Sets up the process counters. Safe to call more than once.
void start();
void stop();

// Collects everything that has to be measured over time - how long each output
// has been running, and the byte and CPU deltas the rates are derived from.
// Called on the bar's tick; the rates update themselves about once a second
// however often this runs.
void sample();

// Current reading, formatted for the bar. Metrics whose output is not running
// read as a placeholder rather than as a zero. The clock format is ignored by
// every metric but Time of day.
QString value(DisplayMetric metric, ClockFormat clockFormat = ClockFormat::Hours24);

// A representative reading, for the settings dialog's preview: a stream timer
// should look like a stream timer even when nothing is streaming.
QString previewValue(DisplayMetric metric, ClockFormat clockFormat = ClockFormat::Hours24);

// True while the thing this metric describes is running, which is what a
// Display entry's "this button's action is active" condition tests.
bool isLive(DisplayMetric metric);

// Translated name shown to the user.
QString label(DisplayMetric metric);

// Stable identifier written to the configuration file.
QString name(DisplayMetric metric);
DisplayMetric fromName(const QString &name);

// Translated name, stable identifier and a sample reading for a clock format,
// so the settings dialog can offer the choice without formatting a time itself.
QString clockFormatLabel(ClockFormat format);
QString clockFormatName(ClockFormat format);
ClockFormat clockFormatFromName(const QString &name);

} // namespace OmniBarMetrics
