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

#include "button-action.hpp"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QUuid>
#include <QStringList>

// ActivationCondition implementation
bool ActivationCondition::isSatisfied(const ButtonAction *action) const
{
	if (source == ConditionSource::Always)
		return true;

	bool state = false;
	switch (source) {
	case ConditionSource::OwnAction:
		state = action && action->isActive();
		break;
	case ConditionSource::Streaming:
		state = obs_frontend_streaming_active();
		break;
	case ConditionSource::Recording:
		state = obs_frontend_recording_active();
		break;
	case ConditionSource::RecordingPaused:
		state = obs_frontend_recording_paused();
		break;
	case ConditionSource::ReplayBuffer:
		state = obs_frontend_replay_buffer_active();
		break;
	case ConditionSource::VirtualCam:
		state = obs_frontend_virtualcam_active();
		break;
	case ConditionSource::StudioMode:
		state = obs_frontend_preview_program_mode_active();
		break;
	case ConditionSource::Always:
		return true;
	}

	return inverted ? !state : state;
}

static const char *conditionSourceKey(ConditionSource source)
{
	switch (source) {
	case ConditionSource::Always:
		return "OmniBar.Condition.Always";
	case ConditionSource::OwnAction:
		return "OmniBar.Condition.OwnAction";
	case ConditionSource::Streaming:
		return "OmniBar.Condition.Streaming";
	case ConditionSource::Recording:
		return "OmniBar.Condition.Recording";
	case ConditionSource::RecordingPaused:
		return "OmniBar.Condition.RecordingPaused";
	case ConditionSource::ReplayBuffer:
		return "OmniBar.Condition.ReplayBuffer";
	case ConditionSource::VirtualCam:
		return "OmniBar.Condition.VirtualCam";
	case ConditionSource::StudioMode:
		return "OmniBar.Condition.StudioMode";
	}
	return "OmniBar.Condition.Always";
}

QString ActivationCondition::describe() const
{
	QString name = QString::fromUtf8(obs_module_text(conditionSourceKey(source)));
	if (!isSet())
		return name;
	if (inverted)
		return QString::fromUtf8(obs_module_text("OmniBar.Condition.NotFormat")).arg(name);
	return name;
}

void ActivationCondition::serialize(obs_data_t *data, const char *prefix) const
{
	if (!data || !prefix)
		return;
	obs_data_set_int(data, QString("%1_source").arg(prefix).toUtf8().constData(), static_cast<int>(source));
	obs_data_set_bool(data, QString("%1_inverted").arg(prefix).toUtf8().constData(), inverted);
}

ActivationCondition ActivationCondition::deserialize(obs_data_t *data, const char *prefix)
{
	ActivationCondition condition;
	if (!data || !prefix)
		return condition;

	QString sourceKey = QString("%1_source").arg(prefix);
	if (obs_data_has_user_value(data, sourceKey.toUtf8().constData()))
		condition.source = static_cast<ConditionSource>(obs_data_get_int(data, sourceKey.toUtf8().constData()));
	condition.inverted = obs_data_get_bool(data, QString("%1_inverted").arg(prefix).toUtf8().constData());
	return condition;
}

// ButtonAction static deserialize
std::unique_ptr<ButtonAction> ButtonAction::deserialize(obs_data_t *data)
{
	if (!data)
		return nullptr;

	const char *typeStr = obs_data_get_string(data, "type");
	if (!typeStr)
		return nullptr;

	QString type = QString::fromUtf8(typeStr);

	if (type == "none") {
		return std::make_unique<NoAction>();
	} else if (type == "frontend") {
		const char *actionStr = obs_data_get_string(data, "action");
		FrontendActionType actionType = FrontendAction::getFrontendActionFromName(QString::fromUtf8(actionStr));
		return std::make_unique<FrontendAction>(actionType);
	} else if (type == "source_hotkey") {
		QString source = QString::fromUtf8(obs_data_get_string(data, "source"));
		QString hotkey = QString::fromUtf8(obs_data_get_string(data, "hotkey"));
		return std::make_unique<SourceHotkeyAction>(source, hotkey);
	} else if (type == "source_filter") {
		QString source = QString::fromUtf8(obs_data_get_string(data, "source"));
		QString filter = QString::fromUtf8(obs_data_get_string(data, "filter"));
		return std::make_unique<SourceFilterAction>(source, filter);
	} else if (type == "source_visibility") {
		QString scene = QString::fromUtf8(obs_data_get_string(data, "scene"));
		QString source = QString::fromUtf8(obs_data_get_string(data, "source"));
		return std::make_unique<SourceVisibilityAction>(scene, source);
	} else if (type == "spacer") {
		int width = static_cast<int>(obs_data_get_int(data, "width"));
		return std::make_unique<SpacerAction>(width);
	} else if (type == "divider") {
		int thickness = static_cast<int>(obs_data_get_int(data, "thickness"));
		int length = static_cast<int>(obs_data_get_int(data, "length_percent"));
		auto divider = std::make_unique<DividerAction>(thickness > 0 ? thickness : 1, length > 0 ? length : 70);
		if (obs_data_get_bool(data, "use_custom_color")) {
			QColor color(QString::fromUtf8(obs_data_get_string(data, "color")));
			if (color.isValid())
				divider->setCustomColor(color);
		}
		return divider;
	}

	return nullptr;
}

// NoAction implementation
QString NoAction::getDisplayName() const
{
	return QString::fromUtf8(obs_module_text("OmniBar.ActionType.None"));
}

obs_data_t *NoAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "none");
	return data;
}

// FrontendAction implementation
FrontendAction::FrontendAction(FrontendActionType type) : actionType(type) {}

void FrontendAction::execute()
{
	switch (actionType) {
	case FrontendActionType::StartStreaming:
		obs_frontend_streaming_start();
		break;
	case FrontendActionType::StopStreaming:
		obs_frontend_streaming_stop();
		break;
	case FrontendActionType::ToggleStreaming:
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
		break;
	case FrontendActionType::StartRecording:
		obs_frontend_recording_start();
		break;
	case FrontendActionType::StopRecording:
		obs_frontend_recording_stop();
		break;
	case FrontendActionType::ToggleRecording:
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		else
			obs_frontend_recording_start();
		break;
	case FrontendActionType::PauseRecording:
		obs_frontend_recording_pause(true);
		break;
	case FrontendActionType::UnpauseRecording:
		obs_frontend_recording_pause(false);
		break;
	case FrontendActionType::TogglePauseRecording:
		obs_frontend_recording_pause(!obs_frontend_recording_paused());
		break;
	case FrontendActionType::StartReplayBuffer:
		obs_frontend_replay_buffer_start();
		break;
	case FrontendActionType::StopReplayBuffer:
		obs_frontend_replay_buffer_stop();
		break;
	case FrontendActionType::ToggleReplayBuffer:
		if (obs_frontend_replay_buffer_active())
			obs_frontend_replay_buffer_stop();
		else
			obs_frontend_replay_buffer_start();
		break;
	case FrontendActionType::SaveReplayBuffer:
		obs_frontend_replay_buffer_save();
		break;
	case FrontendActionType::StartVirtualCam:
		obs_frontend_start_virtualcam();
		break;
	case FrontendActionType::StopVirtualCam:
		obs_frontend_stop_virtualcam();
		break;
	case FrontendActionType::ToggleVirtualCam:
		if (obs_frontend_virtualcam_active())
			obs_frontend_stop_virtualcam();
		else
			obs_frontend_start_virtualcam();
		break;
	case FrontendActionType::EnableStudioMode:
		obs_frontend_set_preview_program_mode(true);
		break;
	case FrontendActionType::DisableStudioMode:
		obs_frontend_set_preview_program_mode(false);
		break;
	case FrontendActionType::ToggleStudioMode:
		obs_frontend_set_preview_program_mode(!obs_frontend_preview_program_mode_active());
		break;
	case FrontendActionType::TransitionToProgram:
		obs_frontend_preview_program_trigger_transition();
		break;
	}
}

bool FrontendAction::isActive() const
{
	switch (actionType) {
	case FrontendActionType::StartStreaming:
	case FrontendActionType::StopStreaming:
	case FrontendActionType::ToggleStreaming:
		return obs_frontend_streaming_active();
	case FrontendActionType::StartRecording:
	case FrontendActionType::StopRecording:
	case FrontendActionType::ToggleRecording:
		return obs_frontend_recording_active();
	case FrontendActionType::PauseRecording:
	case FrontendActionType::UnpauseRecording:
	case FrontendActionType::TogglePauseRecording:
		return obs_frontend_recording_paused();
	case FrontendActionType::StartReplayBuffer:
	case FrontendActionType::StopReplayBuffer:
	case FrontendActionType::ToggleReplayBuffer:
		return obs_frontend_replay_buffer_active();
	case FrontendActionType::SaveReplayBuffer:
		return false;
	case FrontendActionType::StartVirtualCam:
	case FrontendActionType::StopVirtualCam:
	case FrontendActionType::ToggleVirtualCam:
		return obs_frontend_virtualcam_active();
	case FrontendActionType::EnableStudioMode:
	case FrontendActionType::DisableStudioMode:
	case FrontendActionType::ToggleStudioMode:
		return obs_frontend_preview_program_mode_active();
	case FrontendActionType::TransitionToProgram:
		return false;
	}
	return false;
}

// Locale key for the name shown to the user. Kept separate from the serialised
// identifier, which must never change.
static const char *frontendActionLabelKey(FrontendActionType type)
{
	switch (type) {
	case FrontendActionType::StartStreaming:
		return "OmniBar.Action.StartStream";
	case FrontendActionType::StopStreaming:
		return "OmniBar.Action.StopStream";
	case FrontendActionType::ToggleStreaming:
		return "OmniBar.Action.ToggleStream";
	case FrontendActionType::StartRecording:
		return "OmniBar.Action.StartRecord";
	case FrontendActionType::StopRecording:
		return "OmniBar.Action.StopRecord";
	case FrontendActionType::ToggleRecording:
		return "OmniBar.Action.ToggleRecord";
	case FrontendActionType::PauseRecording:
		return "OmniBar.Action.Pause";
	case FrontendActionType::UnpauseRecording:
		return "OmniBar.Action.Unpause";
	case FrontendActionType::TogglePauseRecording:
		return "OmniBar.Action.TogglePause";
	case FrontendActionType::StartReplayBuffer:
		return "OmniBar.Action.StartReplay";
	case FrontendActionType::StopReplayBuffer:
		return "OmniBar.Action.StopReplay";
	case FrontendActionType::ToggleReplayBuffer:
		return "OmniBar.Action.ToggleReplay";
	case FrontendActionType::SaveReplayBuffer:
		return "OmniBar.Action.SaveReplay";
	case FrontendActionType::StartVirtualCam:
		return "OmniBar.Action.StartVirtualCam";
	case FrontendActionType::StopVirtualCam:
		return "OmniBar.Action.StopVirtualCam";
	case FrontendActionType::ToggleVirtualCam:
		return "OmniBar.Action.ToggleVirtualCam";
	case FrontendActionType::EnableStudioMode:
		return "OmniBar.Action.EnableStudioMode";
	case FrontendActionType::DisableStudioMode:
		return "OmniBar.Action.DisableStudioMode";
	case FrontendActionType::ToggleStudioMode:
		return "OmniBar.Action.ToggleStudioMode";
	case FrontendActionType::TransitionToProgram:
		return "OmniBar.Action.Transition";
	}
	return "OmniBar.Action.ToggleStream";
}

QString FrontendAction::getFrontendActionLabel(FrontendActionType type)
{
	return QString::fromUtf8(obs_module_text(frontendActionLabelKey(type)));
}

QString FrontendAction::getDisplayName() const
{
	return getFrontendActionLabel(actionType);
}

obs_data_t *FrontendAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "frontend");
	obs_data_set_string(data, "action", getFrontendActionName(actionType).toUtf8().constData());
	return data;
}

QString FrontendAction::getFrontendActionName(FrontendActionType type)
{
	switch (type) {
	case FrontendActionType::StartStreaming:
		return "start_streaming";
	case FrontendActionType::StopStreaming:
		return "stop_streaming";
	case FrontendActionType::ToggleStreaming:
		return "toggle_streaming";
	case FrontendActionType::StartRecording:
		return "start_recording";
	case FrontendActionType::StopRecording:
		return "stop_recording";
	case FrontendActionType::ToggleRecording:
		return "toggle_recording";
	case FrontendActionType::PauseRecording:
		return "pause_recording";
	case FrontendActionType::UnpauseRecording:
		return "unpause_recording";
	case FrontendActionType::TogglePauseRecording:
		return "toggle_pause_recording";
	case FrontendActionType::StartReplayBuffer:
		return "start_replay_buffer";
	case FrontendActionType::StopReplayBuffer:
		return "stop_replay_buffer";
	case FrontendActionType::ToggleReplayBuffer:
		return "toggle_replay_buffer";
	case FrontendActionType::SaveReplayBuffer:
		return "save_replay_buffer";
	case FrontendActionType::StartVirtualCam:
		return "start_virtual_cam";
	case FrontendActionType::StopVirtualCam:
		return "stop_virtual_cam";
	case FrontendActionType::ToggleVirtualCam:
		return "toggle_virtual_cam";
	case FrontendActionType::EnableStudioMode:
		return "enable_studio_mode";
	case FrontendActionType::DisableStudioMode:
		return "disable_studio_mode";
	case FrontendActionType::ToggleStudioMode:
		return "toggle_studio_mode";
	case FrontendActionType::TransitionToProgram:
		return "transition_to_program";
	}
	return "unknown";
}

FrontendActionType FrontendAction::getFrontendActionFromName(const QString &name)
{
	if (name == "start_streaming")
		return FrontendActionType::StartStreaming;
	if (name == "stop_streaming")
		return FrontendActionType::StopStreaming;
	if (name == "toggle_streaming")
		return FrontendActionType::ToggleStreaming;
	if (name == "start_recording")
		return FrontendActionType::StartRecording;
	if (name == "stop_recording")
		return FrontendActionType::StopRecording;
	if (name == "toggle_recording")
		return FrontendActionType::ToggleRecording;
	if (name == "pause_recording")
		return FrontendActionType::PauseRecording;
	if (name == "unpause_recording")
		return FrontendActionType::UnpauseRecording;
	if (name == "toggle_pause_recording")
		return FrontendActionType::TogglePauseRecording;
	if (name == "start_replay_buffer")
		return FrontendActionType::StartReplayBuffer;
	if (name == "stop_replay_buffer")
		return FrontendActionType::StopReplayBuffer;
	if (name == "toggle_replay_buffer")
		return FrontendActionType::ToggleReplayBuffer;
	if (name == "save_replay_buffer")
		return FrontendActionType::SaveReplayBuffer;
	if (name == "start_virtual_cam")
		return FrontendActionType::StartVirtualCam;
	if (name == "stop_virtual_cam")
		return FrontendActionType::StopVirtualCam;
	if (name == "toggle_virtual_cam")
		return FrontendActionType::ToggleVirtualCam;
	if (name == "enable_studio_mode")
		return FrontendActionType::EnableStudioMode;
	if (name == "disable_studio_mode")
		return FrontendActionType::DisableStudioMode;
	if (name == "toggle_studio_mode")
		return FrontendActionType::ToggleStudioMode;
	if (name == "transition_to_program")
		return FrontendActionType::TransitionToProgram;

	return FrontendActionType::ToggleStreaming;
}

// SourceHotkeyAction implementation
SourceHotkeyAction::SourceHotkeyAction(const QString &source, const QString &hotkey)
	: sourceName(source),
	  hotkeyName(hotkey)
{
}

QString SourceHotkeyAction::hotkeyOwnerName(obs_hotkey_t *hotkey)
{
	if (!hotkey)
		return QString();

	if (obs_hotkey_get_registerer_type(hotkey) != OBS_HOTKEY_REGISTERER_SOURCE)
		return QString();

	auto *weak = static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(hotkey));
	if (!weak)
		return QString();

	obs_source_t *source = obs_weak_source_get_source(weak);
	if (!source)
		return QString();

	QString name = QString::fromUtf8(obs_source_get_name(source));
	obs_source_release(source);
	return name;
}

void SourceHotkeyAction::refreshHotkeyId()
{
	hotkeyId = OBS_INVALID_HOTKEY_ID;

	struct HotkeySearchData {
		QString targetName;
		QString targetSource;
		obs_hotkey_id *resultId;
	};

	HotkeySearchData searchData{hotkeyName, sourceName, &hotkeyId};

	// Hotkey names are only unique per registerer - every scene item shares
	// "libobs.show_scene_item", for instance - so the owning source has to
	// be part of the match or the wrong source gets triggered.
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			HotkeySearchData *search = static_cast<HotkeySearchData *>(data);
			QString name = QString::fromUtf8(obs_hotkey_get_name(key));
			if (name != search->targetName)
				return true;

			QString owner = SourceHotkeyAction::hotkeyOwnerName(key);
			if (!search->targetSource.isEmpty() && owner != search->targetSource)
				return true;

			*search->resultId = id;
			return false;
		},
		&searchData);
}

void SourceHotkeyAction::execute()
{
	refreshHotkeyId();
	if (hotkeyId != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_trigger_routed_callback(hotkeyId, true);
		obs_hotkey_trigger_routed_callback(hotkeyId, false);
	}
}

bool SourceHotkeyAction::isValid() const
{
	if (hotkeyName.isEmpty())
		return false;

	// Hotkeys that belong to OBS itself have no source to check.
	if (sourceName.isEmpty())
		return true;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return false;
	obs_source_release(source);
	return true;
}

QString SourceHotkeyAction::hotkeyDescription() const
{
	if (!cachedDescription.isEmpty())
		return cachedDescription;

	struct DescriptionSearch {
		QString targetName;
		QString targetSource;
		QString *result;
	};

	DescriptionSearch search{hotkeyName, sourceName, &cachedDescription};

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *key) {
			DescriptionSearch *s = static_cast<DescriptionSearch *>(data);
			if (QString::fromUtf8(obs_hotkey_get_name(key)) != s->targetName)
				return true;
			if (SourceHotkeyAction::hotkeyOwnerName(key) != s->targetSource)
				return true;

			const char *description = obs_hotkey_get_description(key);
			if (description && *description)
				*s->result = QString::fromUtf8(description);
			return false;
		},
		&search);

	return cachedDescription.isEmpty() ? hotkeyName : cachedDescription;
}

QString SourceHotkeyAction::getDisplayName() const
{
	if (sourceName.isEmpty())
		return QString::fromUtf8(obs_module_text("OmniBar.Summary.HotkeyGlobal")).arg(hotkeyDescription());
	return QString::fromUtf8(obs_module_text("OmniBar.Summary.Hotkey")).arg(hotkeyDescription(), sourceName);
}

obs_data_t *SourceHotkeyAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "source_hotkey");
	obs_data_set_string(data, "source", sourceName.toUtf8().constData());
	obs_data_set_string(data, "hotkey", hotkeyName.toUtf8().constData());
	return data;
}

// SourceFilterAction implementation
SourceFilterAction::SourceFilterAction(const QString &source, const QString &filter)
	: sourceName(source),
	  filterName(filter)
{
}

void SourceFilterAction::execute()
{
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return;

	obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
	if (filter) {
		bool enabled = obs_source_enabled(filter);
		obs_source_set_enabled(filter, !enabled);
		obs_source_release(filter);
	}
	obs_source_release(source);
}

bool SourceFilterAction::isActive() const
{
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return false;

	obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
	bool enabled = false;
	if (filter) {
		enabled = obs_source_enabled(filter);
		obs_source_release(filter);
	}
	obs_source_release(source);
	return enabled;
}

bool SourceFilterAction::isValid() const
{
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return false;

	obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
	bool valid = filter != nullptr;
	if (filter)
		obs_source_release(filter);
	obs_source_release(source);
	return valid;
}

QString SourceFilterAction::getDisplayName() const
{
	return QString::fromUtf8(obs_module_text("OmniBar.Summary.Filter")).arg(filterName, sourceName);
}

obs_data_t *SourceFilterAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "source_filter");
	obs_data_set_string(data, "source", sourceName.toUtf8().constData());
	obs_data_set_string(data, "filter", filterName.toUtf8().constData());
	return data;
}

// SourceVisibilityAction implementation
SourceVisibilityAction::SourceVisibilityAction(const QString &scene, const QString &source)
	: sceneName(scene),
	  sourceName(source)
{
}

void SourceVisibilityAction::execute()
{
	obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sceneSource)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return;
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.toUtf8().constData());
	if (item) {
		bool visible = obs_sceneitem_visible(item);
		obs_sceneitem_set_visible(item, !visible);
	}
	obs_source_release(sceneSource);
}

bool SourceVisibilityAction::isActive() const
{
	obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sceneSource)
		return false;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return false;
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.toUtf8().constData());
	bool visible = false;
	if (item) {
		visible = obs_sceneitem_visible(item);
	}
	obs_source_release(sceneSource);
	return visible;
}

bool SourceVisibilityAction::isValid() const
{
	obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sceneSource)
		return false;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return false;
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.toUtf8().constData());
	bool valid = item != nullptr;
	obs_source_release(sceneSource);
	return valid;
}

QString SourceVisibilityAction::getDisplayName() const
{
	return QString::fromUtf8(obs_module_text("OmniBar.Summary.Visibility")).arg(sourceName, sceneName);
}

obs_data_t *SourceVisibilityAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "source_visibility");
	obs_data_set_string(data, "scene", sceneName.toUtf8().constData());
	obs_data_set_string(data, "source", sourceName.toUtf8().constData());
	return data;
}

// SpacerAction implementation
SpacerAction::SpacerAction(int width) : spacerWidth(width) {}

QString SpacerAction::getDisplayName() const
{
	return QString::fromUtf8(obs_module_text("OmniBar.ActionType.Spacer"));
}

obs_data_t *SpacerAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "spacer");
	obs_data_set_int(data, "width", spacerWidth);
	return data;
}

// DividerAction implementation
DividerAction::DividerAction(int lineThickness, int linePercent) : thickness(lineThickness), lengthPercent(linePercent)
{
}

QString DividerAction::getDisplayName() const
{
	return QString::fromUtf8(obs_module_text("OmniBar.ActionType.Divider"));
}

obs_data_t *DividerAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "divider");
	obs_data_set_int(data, "thickness", thickness);
	obs_data_set_int(data, "length_percent", lengthPercent);
	obs_data_set_bool(data, "use_custom_color", hasCustomColor());
	obs_data_set_string(data, "color", color.isValid() ? color.name(QColor::HexArgb).toUtf8().constData() : "");
	return data;
}

// ButtonConfig implementation
ButtonConfig::ButtonConfig()
{
	id = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

obs_data_t *ButtonConfig::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", id.toUtf8().constData());
	obs_data_set_string(data, "label", label.toUtf8().constData());
	obs_data_set_string(data, "tooltip", tooltip.toUtf8().constData());
	obs_data_set_string(data, "icon", iconPath.toUtf8().constData());
	obs_data_set_int(data, "display_mode", static_cast<int>(displayMode));
	obs_data_set_bool(data, "tint_icon", tintIcon);
	obs_data_set_bool(data, "use_custom_color", useCustomColor);
	obs_data_set_string(data, "custom_color",
			    customColor.isValid() ? customColor.name(QColor::HexArgb).toUtf8().constData() : "");
	showCondition.serialize(data, "show_condition");
	obs_data_set_bool(data, "is_group", isGroup);
	obs_data_set_int(data, "group_display", static_cast<int>(groupDisplay));
	obs_data_set_int(data, "group_expand", static_cast<int>(groupExpand));
	expandCondition.serialize(data, "expand_condition");

	if (action) {
		obs_data_t *actionData = action->serialize();
		obs_data_set_obj(data, "action", actionData);
		obs_data_release(actionData);
	}

	if (!children.isEmpty()) {
		obs_data_array_t *childArray = obs_data_array_create();
		for (const auto &child : children) {
			obs_data_t *childData = child->serialize();
			obs_data_array_push_back(childArray, childData);
			obs_data_release(childData);
		}
		obs_data_set_array(data, "children", childArray);
		obs_data_array_release(childArray);
	}

	return data;
}

std::shared_ptr<ButtonConfig> ButtonConfig::deserialize(obs_data_t *data)
{
	if (!data)
		return nullptr;

	auto config = std::make_shared<ButtonConfig>();
	QString storedId = QString::fromUtf8(obs_data_get_string(data, "id"));
	if (!storedId.isEmpty())
		config->id = storedId;
	config->label = QString::fromUtf8(obs_data_get_string(data, "label"));
	config->tooltip = QString::fromUtf8(obs_data_get_string(data, "tooltip"));
	config->iconPath = QString::fromUtf8(obs_data_get_string(data, "icon"));
	config->displayMode = static_cast<ButtonDisplayMode>(obs_data_get_int(data, "display_mode"));
	config->tintIcon = obs_data_get_bool(data, "tint_icon");

	config->useCustomColor = obs_data_get_bool(data, "use_custom_color");
	QString colorName = QString::fromUtf8(obs_data_get_string(data, "custom_color"));
	if (!colorName.isEmpty()) {
		QColor color(colorName);
		if (color.isValid())
			config->customColor = color;
	}
	if (!config->customColor.isValid())
		config->useCustomColor = false;

	config->showCondition = ActivationCondition::deserialize(data, "show_condition");

	if (obs_data_has_user_value(data, "is_group")) {
		config->isGroup = obs_data_get_bool(data, "is_group");
		config->groupDisplay = static_cast<GroupDisplayMode>(obs_data_get_int(data, "group_display"));
		config->groupExpand = static_cast<GroupExpandMode>(obs_data_get_int(data, "group_expand"));
		config->expandCondition = ActivationCondition::deserialize(data, "expand_condition");
	} else {
		// Configurations written before groups were reworked used
		// "expandable" plus "expand_when_active", which only ever
		// described an inline group.
		config->isGroup = obs_data_get_bool(data, "expandable");
		config->groupDisplay = GroupDisplayMode::Inline;
		config->groupExpand = obs_data_get_bool(data, "expand_when_active") ? GroupExpandMode::WhenActive
										    : GroupExpandMode::Click;
	}

	// Before conditions existed, expanding "when active" could only mean the
	// group's own action, so state that explicitly rather than leaving it unset.
	if (config->groupExpand == GroupExpandMode::WhenActive && !config->expandCondition.isSet())
		config->expandCondition.source = ConditionSource::OwnAction;

	obs_data_t *actionData = obs_data_get_obj(data, "action");
	if (actionData) {
		config->action = ButtonAction::deserialize(actionData);
		obs_data_release(actionData);
	}
	if (!config->action)
		config->action = std::make_unique<NoAction>();

	obs_data_array_t *childArray = obs_data_get_array(data, "children");
	if (childArray) {
		size_t count = obs_data_array_count(childArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *childData = obs_data_array_item(childArray, i);
			if (childData) {
				auto child = ButtonConfig::deserialize(childData);
				if (child) {
					// Nesting is capped at one level, so a
					// child never stays a group itself.
					child->isGroup = false;
					child->children.clear();
					config->children.append(child);
				}
				obs_data_release(childData);
			}
		}
		obs_data_array_release(childArray);
	}

	if (!config->isGroup)
		config->children.clear();

	return config;
}

bool ButtonConfig::isSpacer() const
{
	return action && action->getType() == ActionType::Spacer;
}

bool ButtonConfig::isDivider() const
{
	return action && action->getType() == ActionType::Divider;
}

bool ButtonConfig::isDecoration() const
{
	return isSpacer() || isDivider();
}

bool ButtonConfig::hasAction() const
{
	return action && action->getType() != ActionType::None && !isDecoration();
}

bool ButtonConfig::isValid() const
{
	if (!action)
		return false;

	if (isGroup) {
		// A group survives as long as it can still show something:
		// its own action, or at least one usable child.
		if (hasAction() && action->isValid())
			return true;
		if (children.isEmpty())
			return true;
		for (const auto &child : children) {
			if (child && child->isValid())
				return true;
		}
		return false;
	}

	return action->isValid();
}

QString ButtonConfig::displayText() const
{
	if (!label.isEmpty())
		return label;
	if (!tooltip.isEmpty())
		return tooltip;
	if (action)
		return action->getDisplayName();
	return id;
}

QString ButtonConfig::summaryText() const
{
	if (isSpacer()) {
		auto *spacer = dynamic_cast<SpacerAction *>(action.get());
		return QString("%1 px").arg(spacer ? spacer->getWidth() : 0);
	}

	if (isDivider()) {
		auto *divider = dynamic_cast<DividerAction *>(action.get());
		if (!divider)
			return QString();
		return QString("%1 px / %2 %").arg(divider->getThickness()).arg(divider->getLengthPercent());
	}

	if (isGroup) {
		QString modeKey;
		switch (groupExpand) {
		case GroupExpandMode::Click:
			modeKey = "OmniBar.Group.Expand.Click";
			break;
		case GroupExpandMode::WhenActive:
			modeKey = "OmniBar.Group.Expand.WhenActive";
			break;
		case GroupExpandMode::Hover:
			modeKey = "OmniBar.Group.Expand.Hover";
			break;
		}

		QString displayKey = groupDisplay == GroupDisplayMode::Flyout ? "OmniBar.Group.Display.Flyout"
									      : "OmniBar.Group.Display.Inline";

		QStringList parts;
		parts << QString::fromUtf8(obs_module_text("OmniBar.Group.ChildCount")).arg(children.size());
		if (expandCondition.isSet())
			parts << expandCondition.describe();
		parts << QString::fromUtf8(obs_module_text(displayKey.toUtf8().constData()));
		parts << QString::fromUtf8(obs_module_text(modeKey.toUtf8().constData()));
		if (hasAction())
			parts << action->getDisplayName();
		return parts.join(" - ");
	}

	QString summary = action ? action->getDisplayName() : QString();
	if (showCondition.isSet())
		summary +=
			" - " +
			QString::fromUtf8(obs_module_text("OmniBar.Condition.OnlyWhile")).arg(showCondition.describe());
	return summary;
}

std::shared_ptr<ButtonConfig> ButtonConfig::clone() const
{
	obs_data_t *data = serialize();
	auto copy = ButtonConfig::deserialize(data);
	obs_data_release(data);
	return copy;
}
