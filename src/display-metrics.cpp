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

#include "display-metrics.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <media-io/video-io.h>

#include <QTime>

namespace {

// Nothing to report yet, drawn so the entry keeps its shape on the bar rather
// than collapsing to nothing.
const char *kNoDuration = "--:--:--";
const char *kNoValue = "--";

// Rates are averaged over at least this long: a byte delta measured over a
// couple of frames reads as noise rather than as a bitrate.
constexpr uint64_t kRateIntervalNs = 1000000000ULL;

// One output's byte counter, kept between samples so a rate can be taken from
// the difference.
struct OutputRate {
	uint64_t bytes = 0;
	uint64_t at = 0;
	double kbps = 0.0;
	bool primed = false;

	void reset() { *this = OutputRate(); }
};

struct MetricState {
	// When each feature started running, or 0 while it is not. Taken from
	// the sampler's own clock rather than from frontend events, so a bar
	// built while something is already running still counts from somewhere.
	uint64_t streamStart = 0;
	uint64_t recordStart = 0;
	uint64_t recordPaused = 0;
	uint64_t replayStart = 0;
	uint64_t virtualCamStart = 0;
	uint64_t lastReplaySaved = 0;

	uint64_t lastSample = 0;
	uint64_t lastRateSample = 0;

	OutputRate stream;
	OutputRate record;
	uint64_t recordBytes = 0;

	// The replay buffer says nothing when it saves, but the path of the last
	// replay changes, which amounts to the same thing.
	QString lastReplayPath;
	bool replayPathKnown = false;

	os_cpu_usage_info_t *cpu = nullptr;
	double cpuUsage = 0.0;

	uint64_t diskFree = 0;
	bool diskKnown = false;

	bool started = false;
};

MetricState state;

QString translated(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Elapsed nanoseconds as HH:MM:SS. Fixed width, so a readout does not resize
// the bar every time it rolls over an hour.
QString formatDuration(uint64_t elapsedNs)
{
	uint64_t seconds = elapsedNs / 1000000000ULL;
	uint64_t hours = seconds / 3600;
	uint64_t minutes = (seconds / 60) % 60;
	return QString("%1:%2:%3")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// Time since start, or the placeholder while start is unset.
QString sinceOrPlaceholder(uint64_t start)
{
	if (!start)
		return QString::fromUtf8(kNoDuration);

	uint64_t now = os_gettime_ns();
	return formatDuration(now > start ? now - start : 0);
}

QString formatBytes(uint64_t bytes)
{
	const double kb = 1024.0;
	double value = static_cast<double>(bytes);

	if (value < kb)
		return QString("%1 B").arg(bytes);
	value /= kb;
	if (value < kb)
		return QString("%1 KB").arg(value, 0, 'f', 0);
	value /= kb;
	if (value < kb)
		return QString("%1 MB").arg(value, 0, 'f', 1);
	value /= kb;
	return QString("%1 GB").arg(value, 0, 'f', 2);
}

QString formatKbps(double kbps)
{
	return QString("%1 kb/s").arg(kbps, 0, 'f', 0);
}

// Count plus the share of the total it represents, which is the part that says
// whether a number is a problem.
QString formatFrameLoss(uint64_t lost, uint64_t total)
{
	if (!total)
		return QString("%1 (0.0%)").arg(lost);
	double percent = (static_cast<double>(lost) / static_cast<double>(total)) * 100.0;
	return QString("%1 (%2%)").arg(lost).arg(percent, 0, 'f', 1);
}

// The caller takes ownership of the reference; every use here releases it.
obs_output_t *streamingOutput()
{
	return obs_frontend_streaming_active() ? obs_frontend_get_streaming_output() : nullptr;
}

obs_output_t *recordingOutput()
{
	return obs_frontend_recording_active() ? obs_frontend_get_recording_output() : nullptr;
}

void sampleRate(obs_output_t *output, OutputRate &rate, uint64_t now)
{
	if (!output) {
		rate.reset();
		return;
	}

	uint64_t bytes = obs_output_get_total_bytes(output);

	if (!rate.primed) {
		rate.bytes = bytes;
		rate.at = now;
		rate.kbps = 0.0;
		rate.primed = true;
		return;
	}

	if (now <= rate.at || now - rate.at < kRateIntervalNs)
		return;

	uint64_t span = now - rate.at;
	uint64_t delta = bytes >= rate.bytes ? bytes - rate.bytes : 0;
	rate.kbps = (static_cast<double>(delta) * 8.0) / (static_cast<double>(span) / 1e9) / 1000.0;
	rate.bytes = bytes;
	rate.at = now;
}

// Path recordings are being written to, so the free space shown is for the
// drive that actually matters.
QString recordingDirectory()
{
	char *path = obs_frontend_get_current_record_output_path();
	QString result = path ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

QString lastReplayPath()
{
	char *path = obs_frontend_get_last_replay();
	QString result = path ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

struct MetricNames {
	DisplayMetric metric;
	const char *id;
	const char *labelKey;
};

const MetricNames kMetricNames[] = {
	{DisplayMetric::StreamTime, "stream_time", "OmniBar.Metric.StreamTime"},
	{DisplayMetric::StreamBitrate, "stream_bitrate", "OmniBar.Metric.StreamBitrate"},
	{DisplayMetric::StreamDropped, "stream_dropped", "OmniBar.Metric.StreamDropped"},
	{DisplayMetric::RecordTime, "record_time", "OmniBar.Metric.RecordTime"},
	{DisplayMetric::RecordBitrate, "record_bitrate", "OmniBar.Metric.RecordBitrate"},
	{DisplayMetric::RecordSize, "record_size", "OmniBar.Metric.RecordSize"},
	{DisplayMetric::DiskFree, "disk_free", "OmniBar.Metric.DiskFree"},
	{DisplayMetric::ReplayTime, "replay_time", "OmniBar.Metric.ReplayTime"},
	{DisplayMetric::ReplayLastSaved, "replay_last_saved", "OmniBar.Metric.ReplayLastSaved"},
	{DisplayMetric::VirtualCamTime, "virtual_cam_time", "OmniBar.Metric.VirtualCamTime"},
	{DisplayMetric::CpuUsage, "cpu_usage", "OmniBar.Metric.CpuUsage"},
	{DisplayMetric::Fps, "fps", "OmniBar.Metric.Fps"},
	{DisplayMetric::FrameTime, "frame_time", "OmniBar.Metric.FrameTime"},
	{DisplayMetric::RenderLag, "render_lag", "OmniBar.Metric.RenderLag"},
	{DisplayMetric::EncodingLag, "encoding_lag", "OmniBar.Metric.EncodingLag"},
	{DisplayMetric::Memory, "memory", "OmniBar.Metric.Memory"},
	{DisplayMetric::Clock, "clock", "OmniBar.Metric.Clock"}};

} // namespace

void OmniBarMetrics::start()
{
	if (state.started)
		return;

	state.cpu = os_cpu_usage_info_start();
	state.started = true;

	// A first reading, so a bar built straight afterwards has something to
	// show rather than a row of placeholders.
	sample();
}

void OmniBarMetrics::stop()
{
	if (state.cpu) {
		os_cpu_usage_info_destroy(state.cpu);
		state.cpu = nullptr;
	}
	state = MetricState();
}

void OmniBarMetrics::sample()
{
	uint64_t now = os_gettime_ns();
	uint64_t elapsed = state.lastSample && now > state.lastSample ? now - state.lastSample : 0;
	state.lastSample = now;

	// Run times. Each one starts counting from the first sample that finds
	// its output running and is forgotten as soon as it stops.
	auto track = [now](bool active, uint64_t &start) {
		if (!active)
			start = 0;
		else if (!start)
			start = now;
	};

	track(obs_frontend_streaming_active(), state.streamStart);
	track(obs_frontend_replay_buffer_active(), state.replayStart);
	track(obs_frontend_virtualcam_active(), state.virtualCamStart);

	if (obs_frontend_recording_active()) {
		if (!state.recordStart) {
			state.recordStart = now;
			state.recordPaused = 0;
		}
		// Paused time is held back rather than subtracted at the end, so
		// the readout stands still while the recording does.
		if (obs_frontend_recording_paused())
			state.recordPaused += elapsed;
	} else {
		state.recordStart = 0;
		state.recordPaused = 0;
	}

	if (state.replayStart) {
		QString path = lastReplayPath();
		if (!state.replayPathKnown) {
			// Whatever is there at startup belongs to an earlier
			// session, so it does not count as a save.
			state.lastReplayPath = path;
			state.replayPathKnown = true;
		} else if (path != state.lastReplayPath) {
			state.lastReplayPath = path;
			state.lastReplaySaved = now;
		}
	} else {
		state.replayPathKnown = false;
		state.lastReplaySaved = 0;
	}

	obs_output_t *stream = streamingOutput();
	sampleRate(stream, state.stream, now);
	if (stream)
		obs_output_release(stream);

	obs_output_t *record = recordingOutput();
	sampleRate(record, state.record, now);
	state.recordBytes = record ? obs_output_get_total_bytes(record) : 0;
	if (record)
		obs_output_release(record);

	// The rest is measured against the clock rather than read outright, so
	// it is only worth redoing about once a second.
	if (state.lastRateSample && now - state.lastRateSample < kRateIntervalNs)
		return;
	state.lastRateSample = now;

	if (state.cpu)
		state.cpuUsage = os_cpu_usage_info_query(state.cpu);

	QString directory = recordingDirectory();
	if (directory.isEmpty()) {
		state.diskKnown = false;
	} else {
		state.diskFree = os_get_free_disk_space(directory.toUtf8().constData());
		state.diskKnown = true;
	}
}

QString OmniBarMetrics::value(DisplayMetric metric)
{
	switch (metric) {
	case DisplayMetric::StreamTime:
		return sinceOrPlaceholder(state.streamStart);

	case DisplayMetric::StreamBitrate:
		return state.streamStart ? formatKbps(state.stream.kbps) : QString::fromUtf8(kNoValue);

	case DisplayMetric::StreamDropped: {
		obs_output_t *output = streamingOutput();
		if (!output)
			return QString::fromUtf8(kNoValue);
		int dropped = obs_output_get_frames_dropped(output);
		int total = obs_output_get_total_frames(output);
		obs_output_release(output);
		return formatFrameLoss(dropped > 0 ? static_cast<uint64_t>(dropped) : 0,
				       total > 0 ? static_cast<uint64_t>(total) : 0);
	}

	case DisplayMetric::RecordTime: {
		if (!state.recordStart)
			return QString::fromUtf8(kNoDuration);
		uint64_t now = os_gettime_ns();
		uint64_t elapsed = now > state.recordStart ? now - state.recordStart : 0;
		return formatDuration(elapsed > state.recordPaused ? elapsed - state.recordPaused : 0);
	}

	case DisplayMetric::RecordBitrate:
		return state.recordStart ? formatKbps(state.record.kbps) : QString::fromUtf8(kNoValue);

	case DisplayMetric::RecordSize:
		return state.recordStart ? formatBytes(state.recordBytes) : QString::fromUtf8(kNoValue);

	case DisplayMetric::DiskFree:
		return state.diskKnown ? formatBytes(state.diskFree) : QString::fromUtf8(kNoValue);

	case DisplayMetric::ReplayTime:
		return sinceOrPlaceholder(state.replayStart);

	case DisplayMetric::ReplayLastSaved:
		return sinceOrPlaceholder(state.lastReplaySaved);

	case DisplayMetric::VirtualCamTime:
		return sinceOrPlaceholder(state.virtualCamStart);

	case DisplayMetric::CpuUsage:
		return QString("%1%").arg(state.cpuUsage, 0, 'f', 1);

	case DisplayMetric::Fps:
		return QString("%1 fps").arg(obs_get_active_fps(), 0, 'f', 1);

	case DisplayMetric::FrameTime:
		return QString("%1 ms").arg(static_cast<double>(obs_get_average_frame_time_ns()) / 1000000.0, 0, 'f',
					    1);

	case DisplayMetric::RenderLag:
		return formatFrameLoss(obs_get_lagged_frames(), obs_get_total_frames());

	case DisplayMetric::EncodingLag: {
		video_t *video = obs_get_video();
		if (!video)
			return QString::fromUtf8(kNoValue);
		return formatFrameLoss(video_output_get_skipped_frames(video), video_output_get_total_frames(video));
	}

	case DisplayMetric::Memory:
		return formatBytes(os_get_proc_resident_size());

	case DisplayMetric::Clock:
		return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
	}

	return QString::fromUtf8(kNoValue);
}

QString OmniBarMetrics::previewValue(DisplayMetric metric)
{
	switch (metric) {
	case DisplayMetric::StreamTime:
	case DisplayMetric::RecordTime:
	case DisplayMetric::ReplayTime:
	case DisplayMetric::ReplayLastSaved:
	case DisplayMetric::VirtualCamTime:
		return QStringLiteral("01:23:45");
	case DisplayMetric::StreamBitrate:
		return formatKbps(6000);
	case DisplayMetric::RecordBitrate:
		return formatKbps(12000);
	case DisplayMetric::StreamDropped:
	case DisplayMetric::RenderLag:
	case DisplayMetric::EncodingLag:
		return formatFrameLoss(12, 20000);
	case DisplayMetric::RecordSize:
		return formatBytes(1288490188ULL);
	case DisplayMetric::DiskFree:
		return formatBytes(348966258278ULL);
	case DisplayMetric::CpuUsage:
		return QStringLiteral("12.4%");
	case DisplayMetric::Fps:
		return QStringLiteral("60.0 fps");
	case DisplayMetric::FrameTime:
		return QStringLiteral("4.2 ms");
	case DisplayMetric::Memory:
		return formatBytes(1503238553ULL);
	case DisplayMetric::Clock:
		return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
	}

	return QString::fromUtf8(kNoValue);
}

bool OmniBarMetrics::isLive(DisplayMetric metric)
{
	switch (metric) {
	case DisplayMetric::StreamTime:
	case DisplayMetric::StreamBitrate:
	case DisplayMetric::StreamDropped:
		return obs_frontend_streaming_active();

	case DisplayMetric::RecordTime:
	case DisplayMetric::RecordBitrate:
	case DisplayMetric::RecordSize:
		return obs_frontend_recording_active();

	case DisplayMetric::ReplayTime:
	case DisplayMetric::ReplayLastSaved:
		return obs_frontend_replay_buffer_active();

	case DisplayMetric::VirtualCamTime:
		return obs_frontend_virtualcam_active();

	case DisplayMetric::DiskFree:
	case DisplayMetric::CpuUsage:
	case DisplayMetric::Fps:
	case DisplayMetric::FrameTime:
	case DisplayMetric::RenderLag:
	case DisplayMetric::EncodingLag:
	case DisplayMetric::Memory:
	case DisplayMetric::Clock:
		// Always something to read, so a condition on the entry's own
		// action never hides it.
		return true;
	}

	return false;
}

QString OmniBarMetrics::label(DisplayMetric metric)
{
	for (const auto &entry : kMetricNames) {
		if (entry.metric == metric)
			return translated(entry.labelKey);
	}
	return translated(kMetricNames[0].labelKey);
}

QString OmniBarMetrics::name(DisplayMetric metric)
{
	for (const auto &entry : kMetricNames) {
		if (entry.metric == metric)
			return QString::fromUtf8(entry.id);
	}
	return QString::fromUtf8(kMetricNames[0].id);
}

DisplayMetric OmniBarMetrics::fromName(const QString &name)
{
	for (const auto &entry : kMetricNames) {
		if (name == QLatin1String(entry.id))
			return entry.metric;
	}
	return DisplayMetric::StreamTime;
}
