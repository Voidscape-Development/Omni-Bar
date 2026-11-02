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

#include <QStringList>
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
	// When each feature started running, or 0 while it is not. Set from the
	// frontend events as they happen, so the timers agree with OBS's own;
	// the sampler only fills in for something that was already running
	// before the plugin could hear about it.
	uint64_t streamStart = 0;
	uint64_t recordStart = 0;
	uint64_t replayStart = 0;
	uint64_t virtualCamStart = 0;
	uint64_t lastReplaySaved = 0;

	// Time the recording has spent paused: whole pauses already over, and
	// the start of the one in progress, if any.
	uint64_t recordPausedTotal = 0;
	uint64_t recordPausedSince = 0;

	uint64_t lastRateSample = 0;

	OutputRate stream;
	OutputRate record;
	uint64_t recordBytes = 0;

	os_cpu_usage_info_t *cpu = nullptr;
	double cpuUsage = 0.0;

	uint64_t diskFree = 0;
	bool diskKnown = false;

	bool started = false;
};

MetricState metricState;

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
	{DisplayMetric::Clock, "clock", "OmniBar.Metric.Clock"},
	{DisplayMetric::Status, "status", "OmniBar.Metric.Status"}};

// Both formats carry seconds, matching the run timers beside them on the bar,
// and the 12-hour one carries a suffix so morning and afternoon cannot be
// mistaken for one another.
QString formatClock(ClockFormat format)
{
	return QTime::currentTime().toString(format == ClockFormat::Hours12 ? QStringLiteral("h:mm:ss AP")
									    : QStringLiteral("HH:mm:ss"));
}

uint64_t recordingElapsed(uint64_t now)
{
	if (!metricState.recordStart)
		return 0;
	uint64_t elapsed = now > metricState.recordStart ? now - metricState.recordStart : 0;
	uint64_t paused = metricState.recordPausedTotal;
	if (metricState.recordPausedSince && now > metricState.recordPausedSince)
		paused += now - metricState.recordPausedSince;
	return elapsed > paused ? elapsed - paused : 0;
}

double percentOf(uint64_t part, uint64_t total)
{
	return total ? (static_cast<double>(part) / static_cast<double>(total)) * 100.0 : 0.0;
}

// Share of frames lost for the metrics that count them, or a negative value
// when there is nothing to count.
double lossPercent(DisplayMetric metric)
{
	switch (metric) {
	case DisplayMetric::StreamDropped: {
		obs_output_t *output = streamingOutput();
		if (!output)
			return -1.0;
		int dropped = obs_output_get_frames_dropped(output);
		int total = obs_output_get_total_frames(output);
		obs_output_release(output);
		return percentOf(dropped > 0 ? static_cast<uint64_t>(dropped) : 0,
				 total > 0 ? static_cast<uint64_t>(total) : 0);
	}
	case DisplayMetric::RenderLag:
		return percentOf(obs_get_lagged_frames(), obs_get_total_frames());
	case DisplayMetric::EncodingLag: {
		video_t *video = obs_get_video();
		if (!video)
			return -1.0;
		return percentOf(video_output_get_skipped_frames(video), video_output_get_total_frames(video));
	}
	default:
		return -1.0;
	}
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	uint64_t now = os_gettime_ns();

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		metricState.streamStart = now;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		metricState.streamStart = 0;
		metricState.stream.reset();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		metricState.recordStart = now;
		metricState.recordPausedTotal = 0;
		metricState.recordPausedSince = 0;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		metricState.recordStart = 0;
		metricState.recordPausedTotal = 0;
		metricState.recordPausedSince = 0;
		metricState.record.reset();
		metricState.recordBytes = 0;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		if (!metricState.recordPausedSince)
			metricState.recordPausedSince = now;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		if (metricState.recordPausedSince) {
			metricState.recordPausedTotal +=
				now > metricState.recordPausedSince ? now - metricState.recordPausedSince : 0;
			metricState.recordPausedSince = 0;
		}
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		metricState.replayStart = now;
		metricState.lastReplaySaved = 0;
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		metricState.replayStart = 0;
		metricState.lastReplaySaved = 0;
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		metricState.lastReplaySaved = now;
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
		metricState.virtualCamStart = now;
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		metricState.virtualCamStart = 0;
		break;
	default:
		break;
	}
}

} // namespace

void OmniBarMetrics::start()
{
	if (metricState.started)
		return;

	metricState.cpu = os_cpu_usage_info_start();
	metricState.started = true;
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	// A first reading, so a bar built straight afterwards has something to
	// show rather than a row of placeholders.
	sample();
}

void OmniBarMetrics::stop()
{
	if (metricState.started)
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	if (metricState.cpu) {
		os_cpu_usage_info_destroy(metricState.cpu);
		metricState.cpu = nullptr;
	}
	metricState = MetricState();
}

void OmniBarMetrics::sample()
{
	uint64_t now = os_gettime_ns();

	// The events above keep the timers exact. This only catches what they
	// cannot: an output already running when the plugin loaded, or one that
	// stopped without its event reaching us.
	auto reconcile = [now](bool active, uint64_t &start) {
		if (!active)
			start = 0;
		else if (!start)
			start = now;
	};

	reconcile(obs_frontend_streaming_active(), metricState.streamStart);
	reconcile(obs_frontend_replay_buffer_active(), metricState.replayStart);
	reconcile(obs_frontend_virtualcam_active(), metricState.virtualCamStart);

	if (obs_frontend_recording_active()) {
		if (!metricState.recordStart)
			metricState.recordStart = now;
		bool paused = obs_frontend_recording_paused();
		if (paused && !metricState.recordPausedSince)
			metricState.recordPausedSince = now;
		else if (!paused && metricState.recordPausedSince) {
			metricState.recordPausedTotal +=
				now > metricState.recordPausedSince ? now - metricState.recordPausedSince : 0;
			metricState.recordPausedSince = 0;
		}
	} else {
		metricState.recordStart = 0;
		metricState.recordPausedTotal = 0;
		metricState.recordPausedSince = 0;
	}

	if (!metricState.replayStart)
		metricState.lastReplaySaved = 0;

	obs_output_t *stream = streamingOutput();
	sampleRate(stream, metricState.stream, now);
	if (stream)
		obs_output_release(stream);

	obs_output_t *record = recordingOutput();
	sampleRate(record, metricState.record, now);
	metricState.recordBytes = record ? obs_output_get_total_bytes(record) : 0;
	if (record)
		obs_output_release(record);

	// The rest is measured against the clock rather than read outright, so
	// it is only worth redoing about once a second.
	if (metricState.lastRateSample && now - metricState.lastRateSample < kRateIntervalNs)
		return;
	metricState.lastRateSample = now;

	if (metricState.cpu)
		metricState.cpuUsage = os_cpu_usage_info_query(metricState.cpu);

	QString directory = recordingDirectory();
	if (directory.isEmpty()) {
		metricState.diskKnown = false;
	} else {
		metricState.diskFree = os_get_free_disk_space(directory.toUtf8().constData());
		metricState.diskKnown = true;
	}
}

QString OmniBarMetrics::value(DisplayMetric metric, ClockFormat clockFormat)
{
	switch (metric) {
	case DisplayMetric::StreamTime:
		return sinceOrPlaceholder(metricState.streamStart);

	case DisplayMetric::StreamBitrate:
		return metricState.streamStart ? formatKbps(metricState.stream.kbps) : QString::fromUtf8(kNoValue);

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

	case DisplayMetric::RecordTime:
		if (!metricState.recordStart)
			return QString::fromUtf8(kNoDuration);
		return formatDuration(recordingElapsed(os_gettime_ns()));

	case DisplayMetric::RecordBitrate:
		return metricState.recordStart ? formatKbps(metricState.record.kbps) : QString::fromUtf8(kNoValue);

	case DisplayMetric::RecordSize:
		return metricState.recordStart ? formatBytes(metricState.recordBytes) : QString::fromUtf8(kNoValue);

	case DisplayMetric::DiskFree:
		return metricState.diskKnown ? formatBytes(metricState.diskFree) : QString::fromUtf8(kNoValue);

	case DisplayMetric::ReplayTime:
		return sinceOrPlaceholder(metricState.replayStart);

	case DisplayMetric::ReplayLastSaved:
		return sinceOrPlaceholder(metricState.lastReplaySaved);

	case DisplayMetric::VirtualCamTime:
		return sinceOrPlaceholder(metricState.virtualCamStart);

	case DisplayMetric::CpuUsage:
		return QString("%1%").arg(metricState.cpuUsage, 0, 'f', 1);

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
		return formatClock(clockFormat);

	case DisplayMetric::Status: {
		uint64_t now = os_gettime_ns();
		QStringList parts;
		if (metricState.streamStart)
			parts << translated("OmniBar.Status.Live").arg(formatDuration(now - metricState.streamStart));
		if (metricState.recordStart)
			parts << translated(metricState.recordPausedSince ? "OmniBar.Status.Paused"
									  : "OmniBar.Status.Recording")
					 .arg(formatDuration(recordingElapsed(now)));
		if (parts.isEmpty())
			return translated("OmniBar.Status.OffAir");
		return parts.join(QStringLiteral(" \u00b7 "));
	}
	}

	return QString::fromUtf8(kNoValue);
}

QString OmniBarMetrics::previewValue(DisplayMetric metric, ClockFormat clockFormat, const ObsStateSnapshot *simulated)
{
	if (simulated && metric == DisplayMetric::Status) {
		QStringList parts;
		if (simulated->streaming)
			parts << translated("OmniBar.Status.Live").arg(QStringLiteral("01:23:45"));
		if (simulated->recording)
			parts << translated(simulated->recordingPaused ? "OmniBar.Status.Paused"
								       : "OmniBar.Status.Recording")
					 .arg(QStringLiteral("00:41:07"));
		return parts.isEmpty() ? translated("OmniBar.Status.OffAir") : parts.join(QStringLiteral(" \u00b7 "));
	}

	// Something not running in the simulated state reads as it would on the
	// bar: a placeholder rather than a number.
	if (simulated && !isLiveIn(metric, *simulated)) {
		switch (metric) {
		case DisplayMetric::StreamTime:
		case DisplayMetric::RecordTime:
		case DisplayMetric::ReplayTime:
		case DisplayMetric::ReplayLastSaved:
		case DisplayMetric::VirtualCamTime:
			return QString::fromUtf8(kNoDuration);
		default:
			return QString::fromUtf8(kNoValue);
		}
	}

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
		return formatClock(clockFormat);
	case DisplayMetric::Status:
		return translated("OmniBar.Status.Live").arg(QStringLiteral("01:23:45")) + QStringLiteral(" \u00b7 ") +
		       translated("OmniBar.Status.Recording").arg(QStringLiteral("00:41:07"));
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

	case DisplayMetric::Status:
		return obs_frontend_streaming_active() || obs_frontend_recording_active();

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

bool OmniBarMetrics::hasThresholds(DisplayMetric metric)
{
	switch (metric) {
	case DisplayMetric::StreamDropped:
	case DisplayMetric::RenderLag:
	case DisplayMetric::EncodingLag:
	case DisplayMetric::CpuUsage:
	case DisplayMetric::DiskFree:
	case DisplayMetric::Fps:
		return true;
	default:
		return false;
	}
}

Severity OmniBarMetrics::severity(DisplayMetric metric)
{
	// Fixed thresholds, chosen to match when OBS itself starts to complain:
	// a percent of frames lost is visible, five percent is a broken stream.
	auto grade = [](double value, double warning, double critical) {
		if (value >= critical)
			return Severity::Critical;
		if (value >= warning)
			return Severity::Warning;
		return Severity::Normal;
	};

	switch (metric) {
	case DisplayMetric::StreamDropped:
	case DisplayMetric::RenderLag:
	case DisplayMetric::EncodingLag: {
		double loss = lossPercent(metric);
		return loss < 0.0 ? Severity::Normal : grade(loss, 1.0, 5.0);
	}
	case DisplayMetric::CpuUsage:
		return grade(metricState.cpuUsage, 75.0, 90.0);
	case DisplayMetric::DiskFree: {
		if (!metricState.diskKnown)
			return Severity::Normal;
		const double gib = 1024.0 * 1024.0 * 1024.0;
		double free = static_cast<double>(metricState.diskFree) / gib;
		if (free < 2.0)
			return Severity::Critical;
		if (free < 10.0)
			return Severity::Warning;
		return Severity::Normal;
	}
	case DisplayMetric::Fps: {
		struct obs_video_info info = {};
		if (!obs_get_video_info(&info) || !info.fps_den)
			return Severity::Normal;
		double target = static_cast<double>(info.fps_num) / static_cast<double>(info.fps_den);
		double shortfall = target > 0.0 ? (1.0 - (obs_get_active_fps() / target)) * 100.0 : 0.0;
		return grade(shortfall, 5.0, 15.0);
	}
	default:
		return Severity::Normal;
	}
}

BarState OmniBarMetrics::state(DisplayMetric metric)
{
	if (metric != DisplayMetric::Status)
		return BarState::None;
	if (metricState.streamStart)
		return BarState::Live;
	if (metricState.recordStart)
		return metricState.recordPausedSince ? BarState::Paused : BarState::Recording;
	return BarState::None;
}

BarState OmniBarMetrics::previewState(DisplayMetric metric, const ObsStateSnapshot *simulated)
{
	if (metric != DisplayMetric::Status)
		return BarState::None;
	if (!simulated || simulated->streaming)
		return BarState::Live;
	if (simulated->recording)
		return simulated->recordingPaused ? BarState::Paused : BarState::Recording;
	return BarState::None;
}

bool OmniBarMetrics::isLiveIn(DisplayMetric metric, const ObsStateSnapshot &state)
{
	switch (metric) {
	case DisplayMetric::StreamTime:
	case DisplayMetric::StreamBitrate:
	case DisplayMetric::StreamDropped:
		return state.streaming;
	case DisplayMetric::RecordTime:
	case DisplayMetric::RecordBitrate:
	case DisplayMetric::RecordSize:
		return state.recording;
	case DisplayMetric::ReplayTime:
	case DisplayMetric::ReplayLastSaved:
		return state.replayBuffer;
	case DisplayMetric::VirtualCamTime:
		return state.virtualCam;
	case DisplayMetric::Status:
		return state.streaming || state.recording;
	default:
		return true;
	}
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

QString OmniBarMetrics::clockFormatLabel(ClockFormat format)
{
	// The sample is built from the current time rather than written out, so
	// the entry reads the way this machine's locale will actually render it.
	const char *key = format == ClockFormat::Hours12 ? "OmniBar.ClockFormat.Hours12"
							 : "OmniBar.ClockFormat.Hours24";
	return translated(key).arg(formatClock(format));
}

QString OmniBarMetrics::clockFormatName(ClockFormat format)
{
	return format == ClockFormat::Hours12 ? QStringLiteral("12h") : QStringLiteral("24h");
}

ClockFormat OmniBarMetrics::clockFormatFromName(const QString &name)
{
	return name == QLatin1String("12h") ? ClockFormat::Hours12 : ClockFormat::Hours24;
}
