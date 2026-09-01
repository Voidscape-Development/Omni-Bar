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
	case StylePreset::Custom:
		return "custom";
	}
	return "obs_native";
}

BarStyle BarStyle::fromPreset(StylePreset preset)
{
	BarStyle style;
	style.preset = preset;

	// Start from the palette so every preset has concrete, editable colours
	// even when it is set to follow the OBS theme.
	QPalette palette = QApplication::palette();
	style.barBackground = QColor(0, 0, 0, 0);
	style.buttonBackground = QColor(0, 0, 0, 0);
	style.buttonHover = withAlpha(palette.color(QPalette::Highlight), 70);
	style.buttonChecked = palette.color(QPalette::Highlight);
	style.buttonBorder = palette.color(QPalette::Mid);
	style.textColor = palette.color(QPalette::ButtonText);

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

	case StylePreset::ModernRounded: {
		style.iconSize = 32;
		style.spacing = 6;
		style.buttonPadding = 8;
		style.cornerRadius = 8;
		style.borderWidth = 1;
		style.useCustomColors = true;

		bool dark = omniBarIsDarkTheme();
		style.barBackground = QColor(0, 0, 0, 0);
		style.buttonBackground = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
		style.buttonHover = dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 26);
		style.buttonChecked = palette.color(QPalette::Highlight);
		style.buttonBorder = dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40);
		style.textColor = palette.color(QPalette::ButtonText);
		break;
	}

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
		break;
	}
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

int BarStyle::buttonExtent() const
{
	return iconSize + (buttonPadding * 2) + (borderWidth * 2);
}

// Shared button rules, reused for the toolbar, the flyout panels and the
// editor's preview so all three render a button identically.
static QString buttonRules(const QString &selector, const BarStyle &style, const QColor &background,
			   const QColor &hover, const QColor &checked, const QColor &border, const QColor &text)
{
	return QString("%1 {"
		       " background-color: %2;"
		       " border: %3px solid %4;"
		       " border-radius: %5px;"
		       " padding: %6px;"
		       " color: %7;"
		       " }"
		       "%1:hover { background-color: %8; }"
		       "%1:checked { background-color: %9; }"
		       "%1:pressed { background-color: %9; }")
		.arg(selector)
		.arg(cssColor(background))
		.arg(style.borderWidth)
		.arg(cssColor(border))
		.arg(style.cornerRadius)
		.arg(style.buttonPadding)
		.arg(cssColor(text))
		.arg(cssColor(hover))
		.arg(cssColor(checked));
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
			     effectiveButtonChecked(), effectiveButtonBorder(), effectiveTextColor());
	return sheet;
}

QString BarStyle::buttonAccentStyleSheet(const QColor &accent) const
{
	if (!accent.isValid())
		return QString();

	// The override recolours the states that read as "this button is on":
	// hover gets a translucent tint of the accent, checked gets it solid.
	return buttonRules("QToolButton", *this, effectiveButtonBackground(), withAlpha(accent, 90), accent,
			   effectiveButtonBorder(), effectiveTextColor());
}

QString BarStyle::displayAccentStyleSheet(const QColor &accent) const
{
	if (!accent.isValid())
		return QString();

	return buttonRules("QToolButton", *this, effectiveButtonBackground(), effectiveButtonHover(),
			   effectiveButtonChecked(), effectiveButtonBorder(), accent);
}

QString BarStyle::buttonPulseStyleSheet(const QColor &accent, qreal factor) const
{
	QColor base = accent.isValid() ? accent : effectiveButtonChecked();

	// Only the active fill breathes: the icon, the label and the border stay
	// put, so the button never reads as flickering or disabled.
	QColor dimmed = withAlpha(base, qRound(base.alpha() * qBound(0.0, factor, 1.0)));

	return buttonRules("QToolButton", *this, effectiveButtonBackground(), withAlpha(base, 90), dimmed,
			   effectiveButtonBorder(), effectiveTextColor());
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
				.arg(cssColor(effectiveButtonBorder()))
				.arg(qMax(2, cornerRadius));

	sheet += buttonRules(container + " QToolButton", *this, effectiveButtonBackground(), effectiveButtonHover(),
			     effectiveButtonChecked(), effectiveButtonBorder(), effectiveTextColor());
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
	if (name == "compact")
		return StylePreset::Compact;
	if (name == "modern_rounded")
		return StylePreset::ModernRounded;
	if (name == "neon_accent")
		return StylePreset::NeonAccent;
	if (name == "custom")
		return StylePreset::Custom;
	return StylePreset::ObsNative;
}

BarStyle BarStyle::deserialize(obs_data_t *data)
{
	BarStyle style = BarStyle::fromPreset(StylePreset::ObsNative);
	if (!data)
		return style;

	style.preset = presetFromName(QString::fromUtf8(obs_data_get_string(data, "preset")));

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

	style.barBackground = readColor(data, "bar_background", style.barBackground);
	style.buttonBackground = readColor(data, "button_background", style.buttonBackground);
	style.buttonHover = readColor(data, "button_hover", style.buttonHover);
	style.buttonChecked = readColor(data, "button_checked", style.buttonChecked);
	style.buttonBorder = readColor(data, "button_border", style.buttonBorder);
	style.textColor = readColor(data, "text_color", style.textColor);

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
	       pulseIntensity == other.pulseIntensity && useCustomColors == other.useCustomColors &&
	       barBackground == other.barBackground && buttonBackground == other.buttonBackground &&
	       buttonHover == other.buttonHover && buttonChecked == other.buttonChecked &&
	       buttonBorder == other.buttonBorder && textColor == other.textColor;
}
