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

// ButtonAction static deserialize
std::unique_ptr<ButtonAction> ButtonAction::deserialize(obs_data_t *data)
{
	if (!data)
		return nullptr;

	const char *typeStr = obs_data_get_string(data, "type");
	if (!typeStr)
		return nullptr;

	QString type = QString::fromUtf8(typeStr);

	if (type == "frontend") {
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
	}

	return nullptr;
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

QString FrontendAction::getDisplayName() const
{
	return getFrontendActionName(actionType);
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

void SourceHotkeyAction::refreshHotkeyId()
{
	hotkeyId = OBS_INVALID_HOTKEY_ID;

	struct HotkeySearchData {
		QString targetName;
		obs_hotkey_id *resultId;
	};

	HotkeySearchData searchData{hotkeyName, &hotkeyId};

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			HotkeySearchData *search = static_cast<HotkeySearchData *>(data);
			QString name = QString::fromUtf8(obs_hotkey_get_name(key));
			if (name == search->targetName) {
				*search->resultId = id;
				return false;
			}
			return true;
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
	if (sourceName.isEmpty())
		return false;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return false;
	obs_source_release(source);
	return true;
}

QString SourceHotkeyAction::getDisplayName() const
{
	return QString("Hotkey: %1 (%2)").arg(hotkeyName, sourceName);
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
	return QString("Filter: %1 on %2").arg(filterName, sourceName);
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
	return QString("Visibility: %1 in %2").arg(sourceName, sceneName);
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

obs_data_t *SpacerAction::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", "spacer");
	obs_data_set_int(data, "width", spacerWidth);
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
	obs_data_set_string(data, "tooltip", tooltip.toUtf8().constData());
	obs_data_set_string(data, "icon", iconPath.toUtf8().constData());
	obs_data_set_bool(data, "expandable", expandable);
	obs_data_set_bool(data, "expand_when_active", expandWhenActive);

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
	config->id = QString::fromUtf8(obs_data_get_string(data, "id"));
	config->tooltip = QString::fromUtf8(obs_data_get_string(data, "tooltip"));
	config->iconPath = QString::fromUtf8(obs_data_get_string(data, "icon"));
	config->expandable = obs_data_get_bool(data, "expandable");
	config->expandWhenActive = obs_data_get_bool(data, "expand_when_active");

	obs_data_t *actionData = obs_data_get_obj(data, "action");
	if (actionData) {
		config->action = ButtonAction::deserialize(actionData);
		obs_data_release(actionData);
	}

	obs_data_array_t *childArray = obs_data_get_array(data, "children");
	if (childArray) {
		size_t count = obs_data_array_count(childArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *childData = obs_data_array_item(childArray, i);
			if (childData) {
				auto child = ButtonConfig::deserialize(childData);
				if (child) {
					config->children.append(child);
				}
				obs_data_release(childData);
			}
		}
		obs_data_array_release(childArray);
	}

	return config;
}

bool ButtonConfig::isValid() const
{
	if (!action)
		return false;
	return action->isValid();
}
