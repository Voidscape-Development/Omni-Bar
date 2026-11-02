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

#include "bar-style.hpp"
#include <obs-module.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QPalette>
#include <QtMath>

bool omniBarIsDarkTheme()
{
	QColor windowColor = QApplication::palette().color(QPalette::Window);
	int luminance = (windowColor.red() * 299 + windowColor.green() * 587 + windowColor.blue() * 114) / 1000;
	return luminance < 128;
}

// Qt's stylesheet parser reliably accepts a percentage alpha, so emit that
// rather than the 0-255 form.
static QString cssColor(const QColor &color)
{
	if (!color.isValid())
		return QStringLiteral("transparent");
	if (color.alpha() == 0)
		return QStringLiteral("transparent");
	return QString("rgba(%1, %2, %3, %4%)")
		.arg(color.red())
		.arg(color.green())
		.arg(color.blue())
		.arg(qRound(color.alphaF() * 100.0));
}

static QColor withAlpha(const QColor &color, int alpha)
{
	QColor result = color;
	result.setAlpha(alpha);
	return result;
}

// front painted over back, source-over. With an opaque back the result is the
// single colour that front already appears to be once it has been drawn.
static QColor composite(const QColor &front, const QColor &back)
{
	if (!front.isValid())
		return back;
	if (!back.isValid() || front.alpha() == 255)
		return front;
	if (front.alpha() == 0)
		return back;

	qreal fa = front.alphaF();
	qreal ba = back.alphaF();
	qreal outAlpha = fa + ba * (1.0 - fa);
	if (outAlpha <= 0.0)
		return QColor(0, 0, 0, 0);

	auto channel = [&](qreal f, qreal b) {
		return ((f * fa) + (b * ba * (1.0 - fa))) / outAlpha;
	};

	QColor result;
	result.setRgbF(channel(front.redF(), back.redF()), channel(front.greenF(), back.greenF()),
		       channel(front.blueF(), back.blueF()), outAlpha);
	return result;
}

// Qt draws a rounded stylesheet border as four mitred edges plus four corner
// arcs, and those strokes overlap where they meet. A translucent border colour
// is therefore blended twice in every corner, which shows as four dark blots
// that grow with the border width. Resolving the colour against whatever is
// behind it hands Qt an opaque stroke instead: the edges render exactly as they
// did, and the corners no longer stack.
static QColor solidBorder(const QColor &border, const QColor &fill, const QColor &backdrop)
{
	// A border the user has turned off stays off - compositing a fully
	// transparent colour would turn it back on as the backdrop.
	if (!border.isValid() || border.alpha() == 0)
		return border;
	return composite(border, composite(fill, backdrop));
}

// The opaque colour behind a button on the bar: the bar's own background over
// the window it is docked in.
static QColor barBackdrop(const BarStyle &style)
{
	return composite(style.effectiveBarBackground(), QApplication::palette().color(QPalette::Window));
}

qreal omniBarPulseFactor(const BarStyle &style)
{
	// One clock for every pulsing button, started on first use, so buttons
	// that light up at different times are still in phase with each other.
	static QElapsedTimer clock;
	if (!clock.isValid())
		clock.start();

	int period = qMax(100, style.pulsePeriod);
	qreal depth = qBound(0.0, style.pulseIntensity / 100.0, 1.0);

	// A cosine breath: full colour at the start of each period, dimmest in
	// the middle, back to full at the end, with no jump between cycles.
	qreal phase = static_cast<qreal>(clock.elapsed() % period) / period;
	qreal dip = 0.5 - (0.5 * qCos(phase * 2.0 * M_PI));

	return 1.0 - (depth * dip);
}

QString BarStyle::presetName(StylePreset preset)
{
	switch (preset) {
	case StylePreset::ObsNative:
		return "obs_native";
	case StylePreset::Compact:
		return "compact";
	case StylePreset::ModernRounded:
		return "modern_rounded";
	case StylePreset::NeonAccent:
		return "neon_accent";
	case StylePreset::Broadcast:
		return "broadcast";
	case StylePreset::Capsule:
		return "capsule";
	case StylePreset::MonoLine:
		return "mono_line";
	case StylePreset::HighContrast:
		return "high_contrast";
	case StylePreset::Phosphor:
		return "phosphor";
	case StylePreset::Daylight:
		return "daylight";
	case StylePreset::Custom:
		return "custom";
	}
	return "obs_native";
}

// The six state colours plus the two readout warnings, in one call so a preset
// cannot set some and forget the rest.
static void setStates(BarStyle &style, const QColor &live, const QColor &recording, const QColor &paused,
		      const QColor &replay, const QColor &virtualCam, const QColor &studio, const QColor &warning,
		      const QColor &critical)
{
	style.stateLive = live;
	style.stateRecording = recording;
	style.statePaused = paused;
	style.stateReplay = replay;
	style.stateVirtualCam = virtualCam;
	style.stateStudio = studio;
	style.warningColor = warning;
	style.criticalColor = critical;
}

BarStyle BarStyle::fromPreset(StylePreset preset)
{
	BarStyle style;
	style.preset = preset;

	// Start from the palette so every preset has concrete, editable colours
	// even when it is set to follow the OBS theme.
	QPalette palette = QApplication::palette();
	bool dark = omniBarIsDarkTheme();
	QColor highlight = palette.color(QPalette::Highlight);

	style.barBackground = QColor(0, 0, 0, 0);
	style.buttonBackground = QColor(0, 0, 0, 0);
	style.buttonHover = withAlpha(highlight, 70);
	style.buttonChecked = highlight;
	style.buttonBorder = palette.color(QPalette::Mid);
	style.textColor = palette.color(QPalette::ButtonText);

	// Tally colours that read on whichever background the theme has. Studio
	// mode keeps the theme's own highlight, since it is a mode of OBS rather
	// than something going out.
	if (dark)
		setStates(style, QColor("#e5484d"), QColor("#e5484d"), QColor("#f5a524"), QColor("#30a46c"),
			  QColor("#8e7cf0"), highlight, QColor("#f5a524"), QColor("#e5484d"));
	else
		setStates(style, QColor("#d92d34"), QColor("#d92d34"), QColor("#b86e00"), QColor("#18794e"),
			  QColor("#6e56cf"), highlight, QColor("#b86e00"), QColor("#d92d34"));

	switch (preset) {
	case StylePreset::ObsNative:
	case StylePreset::Custom:
		style.iconSize = 32;
		style.spacing = 4;
		style.buttonPadding = 4;
		style.cornerRadius = 4;
		style.borderWidth = 0;
		style.useCustomColors = false;
		break;

	case StylePreset::Compact:
		style.iconSize = 20;
		style.spacing = 2;
		style.buttonPadding = 2;
		style.cornerRadius = 0;
		style.borderWidth = 0;
		style.useCustomColors = false;
		break;

	case StylePreset::ModernRounded:
		style.iconSize = 32;
		style.spacing = 6;
		style.buttonPadding = 8;
		style.cornerRadius = 8;
		style.borderWidth = 1;
		style.useCustomColors = true;
		style.barBackground = QColor(0, 0, 0, 0);
		style.buttonBackground = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
		style.buttonHover = dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 26);
		style.buttonChecked = highlight;
		style.buttonBorder = dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40);
		style.textColor = palette.color(QPalette::ButtonText);
		break;

	case StylePreset::NeonAccent: {
		style.iconSize = 28;
		style.spacing = 6;
		style.buttonPadding = 8;
		style.cornerRadius = 6;
		style.borderWidth = 1;
		style.useCustomColors = true;
		// The loudest of the presets, so its pulse is quicker and deeper.
		style.pulsePeriod = 1000;
		style.pulseIntensity = 75;

		const QColor accent(0, 229, 255);
		style.barBackground = QColor(18, 20, 26);
		style.buttonBackground = QColor(255, 255, 255, 10);
		style.buttonHover = withAlpha(accent, 60);
		style.buttonChecked = accent;
		style.buttonBorder = withAlpha(accent, 110);
		style.textColor = QColor(235, 245, 250);
		setStates(style, QColor("#ff3d8b"), QColor("#ff3d8b"), QColor("#ffd166"), accent, QColor("#b388ff"),
			  accent, QColor("#ffd166"), QColor("#ff3d8b"));
		break;
	}

	case StylePreset::Broadcast:
		// Graphite with hairline-quiet buttons: on air, the only colour on
		// the bar is whatever is actually going out.
		style.iconSize = 26;
		style.spacing = 4;
		style.buttonPadding = 6;
		style.cornerRadius = 3;
		style.borderWidth = 0;
		style.indicator = ActiveIndicator::Underline;
		style.useCustomColors = true;
		style.barBackground = QColor("#14161b");
		style.buttonBackground = QColor(0, 0, 0, 0);
		style.buttonHover = QColor("#232730");
		style.buttonChecked = QColor("#4aa3ff");
		style.buttonBorder = QColor("#2a2e37");
		style.textColor = QColor("#c9ced8");
		setStates(style, QColor("#ff4d4f"), QColor("#ff6b3d"), QColor("#f5a524"), QColor("#3ecf8e"),
			  QColor("#9d8cff"), QColor("#4aa3ff"), QColor("#f5a524"), QColor("#ff4d4f"));
		break;

	case StylePreset::Capsule:
		// Round buttons marked with a dot rather than filled.
		style.iconSize = 24;
		style.spacing = 6;
		style.buttonPadding = 8;
		style.cornerRadius = 32;
		style.borderWidth = 0;
		style.indicator = ActiveIndicator::Dot;
		style.useCustomColors = true;
		style.barBackground = QColor("#2a2d36");
		style.buttonBackground = QColor(0, 0, 0, 0);
		style.buttonHover = QColor(255, 255, 255, 23);
		style.buttonChecked = QColor("#6e9bff");
		style.buttonBorder = QColor(255, 255, 255, 18);
		style.textColor = QColor("#dfe2e8");
		setStates(style, QColor("#ff5c64"), QColor("#ff5c64"), QColor("#ffb224"), QColor("#46d39a"),
			  QColor("#a493ff"), QColor("#6e9bff"), QColor("#ffb224"), QColor("#ff5c64"));
		break;

	case StylePreset::MonoLine: {
		// No fills at all: outlined buttons, a ring for active, and one ink
		// for everything except going out.
		style.iconSize = 24;
		style.spacing = 4;
		style.buttonPadding = 6;
		style.cornerRadius = 6;
		style.borderWidth = 1;
		style.indicator = ActiveIndicator::Ring;
		style.useCustomColors = false;
		QColor ink = palette.color(QPalette::ButtonText);
		setStates(style, style.stateLive, style.stateRecording, ink, ink, ink, ink, style.warningColor,
			  style.criticalColor);
		break;
	}

	case StylePreset::HighContrast:
		// Black, white and one yellow, with borders thick enough to find.
		style.iconSize = 28;
		style.spacing = 4;
		style.buttonPadding = 6;
		style.cornerRadius = 4;
		style.borderWidth = 2;
		style.pulseIntensity = 40;
		style.useCustomColors = true;
		style.barBackground = QColor("#000000");
		style.buttonBackground = QColor("#000000");
		style.buttonHover = QColor("#333333");
		style.buttonChecked = QColor("#ffd400");
		style.buttonBorder = QColor("#ffffff");
		style.textColor = QColor("#ffffff");
		setStates(style, QColor("#ffd400"), QColor("#ffd400"), QColor("#ffd400"), QColor("#ffd400"),
			  QColor("#ffd400"), QColor("#ffd400"), QColor("#ffd400"), QColor("#ff4d4d"));
		break;

	case StylePreset::Phosphor:
		// A green-screen terminal, with red kept back for going out.
		style.iconSize = 24;
		style.spacing = 4;
		style.buttonPadding = 6;
		style.cornerRadius = 2;
		style.borderWidth = 0;
		style.useCustomColors = true;
		style.barBackground = QColor("#07130c");
		style.buttonBackground = QColor(0, 0, 0, 0);
		style.buttonHover = QColor("#0d2416");
		style.buttonChecked = QColor("#39d98a");
		style.buttonBorder = QColor("#14361f");
		style.textColor = QColor("#7dffb0");
		setStates(style, QColor("#ff5f56"), QColor("#ff5f56"), QColor("#ffd36e"), QColor("#39d98a"),
			  QColor("#39d98a"), QColor("#39d98a"), QColor("#ffd36e"), QColor("#ff5f56"));
		break;

	case StylePreset::Daylight:
		// For the light OBS themes: a white bar with soft hover fills.
		style.iconSize = 26;
		style.spacing = 4;
		style.buttonPadding = 6;
		style.cornerRadius = 6;
		style.borderWidth = 0;
		style.useCustomColors = true;
		style.barBackground = QColor("#ffffff");
		style.buttonBackground = QColor(0, 0, 0, 0);
		style.buttonHover = QColor("#eef1f5");
		style.buttonChecked = QColor("#2b59c3");
		style.buttonBorder = QColor("#d5dae1");
		style.textColor = QColor("#2a2f37");
		setStates(style, QColor("#d92d34"), QColor("#d92d34"), QColor("#b86e00"), QColor("#18794e"),
			  QColor("#6e56cf"), QColor("#2b59c3"), QColor("#b86e00"), QColor("#d92d34"));
		break;
	}

	return style;
}

QColor BarStyle::effectiveBarBackground() const
{
	if (useCustomColors)
		return barBackground;
	return QColor(0, 0, 0, 0);
}

QColor BarStyle::effectiveButtonBackground() const
{
	if (useCustomColors)
		return buttonBackground;
	return QColor(0, 0, 0, 0);
}

QColor BarStyle::effectiveButtonHover() const
{
	if (useCustomColors)
		return buttonHover;
	return withAlpha(QApplication::palette().color(QPalette::Highlight), 70);
}

QColor BarStyle::effectiveButtonChecked() const
{
	if (useCustomColors)
		return buttonChecked;
	return QApplication::palette().color(QPalette::Highlight);
}

QColor BarStyle::effectiveButtonBorder() const
{
	if (useCustomColors)
		return buttonBorder;
	return QApplication::palette().color(QPalette::Mid);
}

QColor BarStyle::effectiveTextColor() const
{
	if (useCustomColors)
		return textColor;
	return QApplication::palette().color(QPalette::ButtonText);
}

QColor BarStyle::stateColor(BarState state) const
{
	QColor color;
	if (useStateColors) {
		switch (state) {
		case BarState::Live:
			color = stateLive;
			break;
		case BarState::Recording:
			color = stateRecording;
			break;
		case BarState::Paused:
			color = statePaused;
			break;
		case BarState::Replay:
			color = stateReplay;
			break;
		case BarState::VirtualCam:
			color = stateVirtualCam;
			break;
		case BarState::Studio:
			color = stateStudio;
			break;
		case BarState::None:
			break;
		}
	}
	return color.isValid() ? color : effectiveButtonChecked();
}

QColor BarStyle::severityColor(Severity severity) const
{
	switch (severity) {
	case Severity::Warning:
		return warningColor.isValid() ? warningColor : QColor("#f5a524");
	case Severity::Critical:
		return criticalColor.isValid() ? criticalColor : QColor("#e5484d");
	case Severity::Normal:
		break;
	}
	return QColor();
}

int BarStyle::effectiveCornerRadius(int width, int height) const
{
	return qMax(0, qMin(cornerRadius, qMin(width, height) / 2));
}

QColor omniBarContrastingColor(const QColor &fill)
{
	int luminance = (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000;
	return luminance < 150 ? QColor(255, 255, 255) : QColor(17, 19, 24);
}

int BarStyle::buttonExtent() const
{
	// Every entry on the bar that is not stacking its label over its icon is
	// this tall, so an icon-only button, one with its label beside the icon
	// and a readout showing nothing but text all line up with each other. The
	// text is measured as well as the icon: a small icon with a large UI font
	// would otherwise leave the labelled entries taller than their neighbours.
	int content = qMax(iconSize, QFontMetrics(QApplication::font()).height());
	return content + (buttonPadding * 2) + (borderWidth * 2);
}

// Shared button rules, reused for the toolbar, the flyout panels and the
// editor's preview so all three render a button identically. The active state
// is painted by the button itself - it can breathe, and it can be a line or a
// dot rather than a fill - so the checked rule only clears the way for it.
static QString buttonRules(const QString &selector, const BarStyle &style, const QColor &background,
			   const QColor &hover, const QColor &border, const QColor &text, const QColor &backdrop)
{
	// Each state fills the button with a different colour, and the border is
	// drawn over that fill, so the resolved border has to be worked out once
	// per state rather than once for the button.
	QColor restingBorder = solidBorder(border, background, backdrop);
	QColor hoverBorder = solidBorder(border, hover, backdrop);

	// A radius beyond half the button bends its edges inside out.
	int radius = style.effectiveCornerRadius(style.buttonExtent(), style.buttonExtent());

	return QString("%1 {"
		       " background-color: %2;"
		       " border: %3px solid %4;"
		       " border-radius: %5px;"
		       " padding: %6px;"
		       " color: %7;"
		       " }"
		       "%1:hover { background-color: %8; border-color: %9; }"
		       "%1:pressed { background-color: %8; border-color: %9; }"
		       "%1:checked { background-color: transparent; border-color: %4; }")
		.arg(selector)
		.arg(cssColor(background))
		.arg(style.borderWidth)
		.arg(cssColor(restingBorder))
		.arg(radius)
		.arg(style.buttonPadding)
		.arg(cssColor(text))
		.arg(cssColor(hover))
		.arg(cssColor(hoverBorder));
}

QString BarStyle::barStyleSheet() const
{
	QString sheet = QString("QToolBar#OmniBar {"
				" background-color: %1;"
				" border: none;"
				" spacing: %2px;"
				" padding: %3px;"
				" }")
				.arg(cssColor(effectiveBarBackground()))
				.arg(spacing)
				.arg(qMax(0, spacing / 2));

	sheet += buttonRules("QToolBar#OmniBar QToolButton", *this, effectiveButtonBackground(), effectiveButtonHover(),
			     effectiveButtonBorder(), effectiveTextColor(), barBackdrop(*this));
	return sheet;
}

QString BarStyle::buttonAccentStyleSheet(const QColor &accent) const
{
	if (!accent.isValid())
		return QString();

	// The active state is painted in the accent by the button; hover gets a
	// translucent tint of it so the two read as belonging together.
	return buttonRules("QToolButton", *this, effectiveButtonBackground(), withAlpha(accent, 90),
			   effectiveButtonBorder(), effectiveTextColor(), barBackdrop(*this));
}

QString BarStyle::displayStyleSheet(const QColor &text) const
{
	// Every state matches the resting one: a readout never lights up.
	QColor background = effectiveButtonBackground();
	return buttonRules("QToolButton", *this, background, background, effectiveButtonBorder(),
			   text.isValid() ? text : effectiveTextColor(), barBackdrop(*this));
}

QString BarStyle::panelStyleSheet(const QString &objectName) const
{
	QColor panel = effectiveBarBackground();
	if (panel.alpha() < 240) {
		// A panel floats over other UI, so it needs an opaque backdrop
		// even when the bar itself is transparent.
		QColor base = QApplication::palette().color(QPalette::Window);
		panel = useCustomColors && barBackground.alpha() > 0 ? withAlpha(barBackground, 255)
								     : withAlpha(base, 255);
	}

	QString container = QString("QWidget#%1").arg(objectName);

	QString sheet = QString("%1 {"
				" background-color: %2;"
				" border: 1px solid %3;"
				" border-radius: %4px;"
				" }")
				.arg(container)
				.arg(cssColor(panel))
				.arg(cssColor(solidBorder(effectiveButtonBorder(), panel, panel)))
				.arg(qMax(2, cornerRadius));

	// A panel is opaque by construction, so it is the whole of what sits
	// behind the buttons it holds.
	sheet += buttonRules(container + " QToolButton", *this, effectiveButtonBackground(), effectiveButtonHover(),
			     effectiveButtonBorder(), effectiveTextColor(), panel);
	return sheet;
}

QString BarStyle::flyoutStyleSheet() const
{
	return panelStyleSheet(QStringLiteral("OmniBarFlyout"));
}

obs_data_t *BarStyle::serialize() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "preset", presetName(preset).toUtf8().constData());
	obs_data_set_int(data, "icon_size", iconSize);
	obs_data_set_int(data, "spacing", spacing);
	obs_data_set_int(data, "button_padding", buttonPadding);
	obs_data_set_int(data, "corner_radius", cornerRadius);
	obs_data_set_int(data, "border_width", borderWidth);
	obs_data_set_int(data, "pulse_period", pulsePeriod);
	obs_data_set_int(data, "pulse_intensity", pulseIntensity);
	obs_data_set_bool(data, "use_custom_colors", useCustomColors);
	obs_data_set_string(data, "bar_background", barBackground.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_string(data, "button_background", buttonBackground.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_string(data, "button_hover", buttonHover.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_string(data, "button_checked", buttonChecked.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_string(data, "button_border", buttonBorder.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_string(data, "text_color", textColor.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_int(data, "indicator", static_cast<int>(indicator));
	obs_data_set_bool(data, "use_state_colors", useStateColors);

	auto setColor = [data](const char *key, const QColor &color) {
		obs_data_set_string(data, key, color.name(QColor::HexArgb).toUtf8().constData());
	};
	setColor("state_live", stateLive);
	setColor("state_recording", stateRecording);
	setColor("state_paused", statePaused);
	setColor("state_replay", stateReplay);
	setColor("state_virtual_cam", stateVirtualCam);
	setColor("state_studio", stateStudio);
	setColor("warning_color", warningColor);
	setColor("critical_color", criticalColor);
	return data;
}

static QColor readColor(obs_data_t *data, const char *key, const QColor &fallback)
{
	const char *value = obs_data_get_string(data, key);
	if (!value || !*value)
		return fallback;
	QColor color(QString::fromUtf8(value));
	return color.isValid() ? color : fallback;
}

static StylePreset presetFromName(const QString &name)
{
	const StylePreset all[] = {StylePreset::ObsNative,  StylePreset::Compact,      StylePreset::ModernRounded,
				   StylePreset::NeonAccent, StylePreset::Broadcast,    StylePreset::Capsule,
				   StylePreset::MonoLine,   StylePreset::HighContrast, StylePreset::Phosphor,
				   StylePreset::Daylight,   StylePreset::Custom};
	for (StylePreset preset : all) {
		if (name == BarStyle::presetName(preset))
			return preset;
	}
	return StylePreset::ObsNative;
}

BarStyle BarStyle::deserialize(obs_data_t *data)
{
	if (!data)
		return BarStyle::fromPreset(StylePreset::ObsNative);

	// Start from the stored preset, so a value a newer version added - the
	// state colours, the indicator - takes that preset's choice rather than
	// some other preset's.
	StylePreset preset = presetFromName(QString::fromUtf8(obs_data_get_string(data, "preset")));
	BarStyle style = BarStyle::fromPreset(preset);

	if (obs_data_has_user_value(data, "icon_size"))
		style.iconSize = static_cast<int>(obs_data_get_int(data, "icon_size"));
	if (obs_data_has_user_value(data, "spacing"))
		style.spacing = static_cast<int>(obs_data_get_int(data, "spacing"));
	if (obs_data_has_user_value(data, "button_padding"))
		style.buttonPadding = static_cast<int>(obs_data_get_int(data, "button_padding"));
	if (obs_data_has_user_value(data, "corner_radius"))
		style.cornerRadius = static_cast<int>(obs_data_get_int(data, "corner_radius"));
	if (obs_data_has_user_value(data, "border_width"))
		style.borderWidth = static_cast<int>(obs_data_get_int(data, "border_width"));
	if (obs_data_has_user_value(data, "pulse_period"))
		style.pulsePeriod = static_cast<int>(obs_data_get_int(data, "pulse_period"));
	if (obs_data_has_user_value(data, "pulse_intensity"))
		style.pulseIntensity = static_cast<int>(obs_data_get_int(data, "pulse_intensity"));
	style.useCustomColors = obs_data_get_bool(data, "use_custom_colors");

	if (obs_data_has_user_value(data, "indicator")) {
		int stored = static_cast<int>(obs_data_get_int(data, "indicator"));
		if (stored >= static_cast<int>(ActiveIndicator::Fill) &&
		    stored <= static_cast<int>(ActiveIndicator::Ring))
			style.indicator = static_cast<ActiveIndicator>(stored);
	}

	if (obs_data_has_user_value(data, "use_state_colors"))
		style.useStateColors = obs_data_get_bool(data, "use_state_colors");
	else if (preset == StylePreset::Custom)
		// A hand-built style from before state colours existed keeps the
		// single active colour it was built around.
		style.useStateColors = false;

	style.barBackground = readColor(data, "bar_background", style.barBackground);
	style.buttonBackground = readColor(data, "button_background", style.buttonBackground);
	style.buttonHover = readColor(data, "button_hover", style.buttonHover);
	style.buttonChecked = readColor(data, "button_checked", style.buttonChecked);
	style.buttonBorder = readColor(data, "button_border", style.buttonBorder);
	style.textColor = readColor(data, "text_color", style.textColor);
	style.stateLive = readColor(data, "state_live", style.stateLive);
	style.stateRecording = readColor(data, "state_recording", style.stateRecording);
	style.statePaused = readColor(data, "state_paused", style.statePaused);
	style.stateReplay = readColor(data, "state_replay", style.stateReplay);
	style.stateVirtualCam = readColor(data, "state_virtual_cam", style.stateVirtualCam);
	style.stateStudio = readColor(data, "state_studio", style.stateStudio);
	style.warningColor = readColor(data, "warning_color", style.warningColor);
	style.criticalColor = readColor(data, "critical_color", style.criticalColor);

	if (style.iconSize <= 0)
		style.iconSize = 32;
	if (style.spacing < 0)
		style.spacing = 0;
	if (style.buttonPadding < 0)
		style.buttonPadding = 0;
	if (style.cornerRadius < 0)
		style.cornerRadius = 0;
	if (style.borderWidth < 0)
		style.borderWidth = 0;
	style.pulsePeriod = qBound(300, style.pulsePeriod, 5000);
	style.pulseIntensity = qBound(10, style.pulseIntensity, 100);

	return style;
}

bool BarStyle::operator==(const BarStyle &other) const
{
	return preset == other.preset && iconSize == other.iconSize && spacing == other.spacing &&
	       buttonPadding == other.buttonPadding && cornerRadius == other.cornerRadius &&
	       borderWidth == other.borderWidth && pulsePeriod == other.pulsePeriod &&
	       pulseIntensity == other.pulseIntensity && indicator == other.indicator &&
	       useCustomColors == other.useCustomColors && barBackground == other.barBackground &&
	       buttonBackground == other.buttonBackground && buttonHover == other.buttonHover &&
	       buttonChecked == other.buttonChecked && buttonBorder == other.buttonBorder &&
	       textColor == other.textColor && useStateColors == other.useStateColors && stateLive == other.stateLive &&
	       stateRecording == other.stateRecording && statePaused == other.statePaused &&
	       stateReplay == other.stateReplay && stateVirtualCam == other.stateVirtualCam &&
	       stateStudio == other.stateStudio && warningColor == other.warningColor &&
	       criticalColor == other.criticalColor;
}
