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

#include <QWidget>
#include <QHash>
#include <QList>
#include <QPointer>
#include <memory>

#include "bar-style.hpp"

class ButtonConfig;
class QBoxLayout;
class QButtonGroup;
class QScrollArea;
class QToolButton;
class QAction;
class QTimer;

// The bar being edited, drawn with the bar's own widgets across the top of the
// settings window. It can be shown as it would look in a state OBS is not in
// - live, recording, paused - so conditions and state colours can be checked
// without going live, and clicking an entry selects it for editing.
class OmniBarPreview : public QWidget {
	Q_OBJECT

public:
	explicit OmniBarPreview(QWidget *parent = nullptr);

	void setEntries(const QList<std::shared_ptr<ButtonConfig>> &entries);
	void setBarStyle(const BarStyle &style);
	void setSelectedId(const QString &id);
	void setOrientation(Qt::Orientation orientation);

	// Rebuilds the drawing from the entries, style and state.
	void refresh();

	// The state the preview is being drawn in.
	ObsStateSnapshot previewState() const;

signals:
	void entryClicked(const QString &id);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	// The choices offered under "Preview as". Now follows OBS itself.
	enum class PreviewAs { Now, Idle, Live, Recording, LiveRecording, Paused };

	void addEntry(const std::shared_ptr<ButtonConfig> &config, const ObsStateSnapshot &state);
	void clearCanvas();
	void updateOverlay();
	void updateScrollHeight();

	QWidget *canvas = nullptr;
	QBoxLayout *canvasLayout = nullptr;
	QScrollArea *scroll = nullptr;
	QWidget *overlay = nullptr;
	QButtonGroup *stateGroup = nullptr;
	QButtonGroup *orientationGroup = nullptr;
	QToolButton *alsoRunningButton = nullptr;
	QAction *replayAction = nullptr;
	QAction *virtualCamAction = nullptr;
	QAction *studioAction = nullptr;
	// Redraws a preview following OBS when what OBS is doing changes.
	QTimer *followTimer = nullptr;
	ObsStateSnapshot lastFollowed;

	QList<std::shared_ptr<ButtonConfig>> entries;
	BarStyle style;
	QString selectedId;
	Qt::Orientation orientation = Qt::Horizontal;
	QHash<QString, QPointer<QWidget>> widgetsById;

	friend class OmniBarPreviewOverlay;
};
