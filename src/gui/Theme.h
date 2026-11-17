#pragma once
#include <QColor>
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QFont>

// Colours for the current theme. Custom-painted widgets read these directly,
// everything else gets them through the palette and the stylesheet.
struct ThemeColors {
    bool dark = true;
    QColor bg, panel, panel2, panel3, border, border2;
    QColor text, text2, muted;
    QColor accent, accent2, accentInk, ok, err, warn;
    QColor editorBg, gutter, activeLine, selection;
    QColor kw, type, func, str, num, comment, op;
};

const ThemeColors& theme();
void applyTheme(bool dark);

// a over b: t = 1 gives a, t = 0 gives b
QColor mix(const QColor& a, const QColor& b, double t);
QString hex(const QColor& c);

// stroked icon from the built-in set, drawn in `color`
QIcon icon(const char* name, const QColor& color, int size = 18);
QPixmap logoPixmap(int size);

QFont monoFont(int pointSize = 10);
QFont uiFont(int pointSize = 10);
