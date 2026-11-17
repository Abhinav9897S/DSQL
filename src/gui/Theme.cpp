#include "gui/Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QPainter>
#include <QFontDatabase>
#include <cstring>

static ThemeColors current;

const ThemeColors& theme() { return current; }

QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() * t + b.redF() * (1 - t), a.greenF() * t + b.greenF() * (1 - t),
                            a.blueF() * t + b.blueF() * (1 - t));
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

static ThemeColors darkColors() {
    ThemeColors c;
    c.dark = true;
    c.bg = QColor("#0c0f14");
    c.panel = QColor("#12161d");
    c.panel2 = QColor("#171c25");
    c.panel3 = QColor("#1f2633");
    c.border = QColor("#242b38");
    c.border2 = QColor("#323b4d");
    c.text = QColor("#d8dee9");
    c.text2 = QColor("#aab3c5");
    c.muted = QColor("#7b8498");
    c.accent = QColor("#4cb4ff");
    c.accent2 = QColor("#9b87f5");
    c.accentInk = QColor("#06121f");
    c.ok = QColor("#3ccf91");
    c.err = QColor("#ff6b6b");
    c.warn = QColor("#f5b945");
    c.editorBg = QColor("#0e1218");
    c.gutter = QColor("#4a5367");
    c.activeLine = QColor("#151a22");
    c.selection = QColor("#1d3a57");
    c.kw = QColor("#7aa2f7");
    c.type = QColor("#2ac3de");
    c.func = QColor("#e0af68");
    c.str = QColor("#9ece6a");
    c.num = QColor("#ff9e64");
    c.comment = QColor("#5c6688");
    c.op = QColor("#89ddff");
    return c;
}

static ThemeColors lightColors() {
    ThemeColors c;
    c.dark = false;
    c.bg = QColor("#eef1f5");
    c.panel = QColor("#ffffff");
    c.panel2 = QColor("#f6f7fa");
    c.panel3 = QColor("#eaedf2");
    c.border = QColor("#dfe3ea");
    c.border2 = QColor("#c9d0db");
    c.text = QColor("#1d2330");
    c.text2 = QColor("#3f4859");
    c.muted = QColor("#6b7489");
    c.accent = QColor("#0a6fd6");
    c.accent2 = QColor("#6d4ee8");
    c.accentInk = QColor("#ffffff");
    c.ok = QColor("#138a5a");
    c.err = QColor("#d63a3a");
    c.warn = QColor("#b7791f");
    c.editorBg = QColor("#ffffff");
    c.gutter = QColor("#a3aab8");
    c.activeLine = QColor("#f2f6fc");
    c.selection = QColor("#cfe2fb");
    c.kw = QColor("#0b4fc9");
    c.type = QColor("#00727f");
    c.func = QColor("#8a5a00");
    c.str = QColor("#18802d");
    c.num = QColor("#c2410c");
    c.comment = QColor("#8c95a6");
    c.op = QColor("#4f5b70");
    return c;
}

// {name} placeholders get the theme's colours
static const char* STYLE = R"(
QMainWindow, QDialog, QMessageBox { background: {bg}; }
QWidget { color: {text}; }
QMenuBar { background: {panel}; border-bottom: 1px solid {border}; padding: 2px 6px; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected { background: {panel3}; }
QMenu { background: {panel}; border: 1px solid {border2}; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 30px 6px 12px; border-radius: 5px; }
QMenu::item:selected { background: {accentSoft}; }
QMenu::separator { height: 1px; background: {border}; margin: 5px 6px; }
QToolBar { background: {panel2}; border: none; border-bottom: 1px solid {border}; spacing: 3px; padding: 4px 8px; }
QToolBar::separator { background: {border2}; width: 1px; margin: 6px 6px; }
QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 4px 8px; color: {text2}; background: transparent; }
QToolButton:hover { background: {panel3}; border-color: {border}; color: {text}; }
QToolButton:pressed { background: {border}; }
QToolButton#runButton { background: {accent}; color: {accentInk}; font-weight: 600; padding: 4px 14px; }
QToolButton#runButton:hover { background: {accentHover}; color: {accentInk}; }
QDockWidget { color: {muted}; font-weight: 600; }
QDockWidget::title { background: {panel2}; padding: 7px 10px; border-bottom: 1px solid {border}; text-align: left; }
QTabWidget::pane { border: none; border-top: 1px solid {border}; background: {panel}; }
QTabBar { background: {panel2}; }
QTabBar::tab { background: transparent; color: {muted}; padding: 7px 14px; border: none; border-right: 1px solid {border}; }
QTabBar::tab:selected { background: {editorBg}; color: {text}; border-top: 2px solid {accent}; }
QTabBar::tab:hover:!selected { color: {text}; background: {panel3}; }
QTreeWidget, QTableView, QTableWidget { background: {panel}; border: none; alternate-background-color: {panel2};
    selection-background-color: {accentSoft}; selection-color: {text}; outline: 0; }
QTreeWidget::item { padding: 3px 2px; border-radius: 4px; }
QTreeWidget::item:hover { background: {panel3}; }
QTreeWidget::item:selected { background: {accentSoft}; color: {text}; }
QHeaderView { background: {panel2}; border: none; }
QHeaderView::section { background: {panel2}; color: {text2}; padding: 5px 10px; border: none;
    border-right: 1px solid {border}; border-bottom: 1px solid {border2}; font-weight: 600; }
QTableView { gridline-color: {border}; }
QTableCornerButton::section { background: {panel2}; border: none; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: {border2}; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: {border2}; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QStatusBar { background: {panel2}; color: {text2}; border-top: 1px solid {border}; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding: 0 8px; color: {text2}; }
QLineEdit { background: {panel2}; border: 1px solid {border2}; border-radius: 6px; padding: 6px 8px; }
QLineEdit:focus { border-color: {accent}; }
QPushButton { background: {panel2}; border: 1px solid {border2}; border-radius: 7px; padding: 6px 14px; }
QPushButton:hover { border-color: {accent}; }
QPushButton#primary { background: {accent}; color: {accentInk}; border-color: {accent}; font-weight: 600; }
QPushButton#startItem { text-align: left; background: transparent; border: 1px solid transparent; border-radius: 10px; padding: 0; }
QPushButton#startItem:hover { background: {panel2}; border-color: {border}; }
QSplitter::handle { background: {border}; }
QSplitter::handle:hover { background: {accent}; }
QPlainTextEdit#sqlEditor { background: {editorBg}; border: none; selection-background-color: {selection}; selection-color: {text}; }
QTextBrowser { background: {panel}; border: none; }
QScrollArea { border: none; background: transparent; }
QToolTip { background: {panel}; color: {text}; border: 1px solid {border2}; padding: 5px; }
QFrame#card { background: {panel}; border: 1px solid {border}; border-radius: 12px; }
QLabel#muted { color: {muted}; }
QLabel#sectionTitle { color: {muted}; font-weight: 600; }
QComboBox { padding: 4px 8px; }
)";

static QString styleSheet(const ThemeColors& c) {
    QString s = QString::fromUtf8(STYLE);
    s.replace("{bg}", hex(c.bg));
    s.replace("{panel}", hex(c.panel));
    s.replace("{panel2}", hex(c.panel2));
    s.replace("{panel3}", hex(c.panel3));
    s.replace("{border}", hex(c.border));
    s.replace("{border2}", hex(c.border2));
    s.replace("{text}", hex(c.text));
    s.replace("{text2}", hex(c.text2));
    s.replace("{muted}", hex(c.muted));
    s.replace("{accent}", hex(c.accent));
    s.replace("{accentInk}", hex(c.accentInk));
    s.replace("{accentSoft}", hex(mix(c.accent, c.panel, 0.22)));
    s.replace("{accentHover}", hex(mix(c.accent, QColor(Qt::white), 0.88)));
    s.replace("{editorBg}", hex(c.editorBg));
    s.replace("{selection}", hex(c.selection));
    return s;
}

void applyTheme(bool dark) {
    current = dark ? darkColors() : lightColors();
    const ThemeColors& c = current;
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, c.bg);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.panel);
    p.setColor(QPalette::AlternateBase, c.panel2);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.panel2);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, mix(c.accent, c.panel, 0.35));
    p.setColor(QPalette::HighlightedText, c.text);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border2);
    p.setColor(QPalette::Light, c.panel3);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
    qApp->setPalette(p);
    qApp->setStyleSheet(styleSheet(c));
}

// ---------------------------------------------------------------- icons

struct IconDef {
    const char* name;
    const char* svg;   // inner SVG, 24x24 viewBox, stroked
};

static const IconDef ICONS[] = {
    {"folder", "<path d='M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z'/>"},
    {"folderPlus", "<path d='M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z'/><path d='M12 11v6M9 14h6'/>"},
    {"file", "<path d='M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z'/><path d='M14 2v6h6'/>"},
    {"filePlus", "<path d='M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z'/><path d='M14 2v6h6M12 18v-6M9 15h6'/>"},
    {"save", "<path d='M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z'/><path d='M17 21v-8H7v8M7 3v5h8'/>"},
    {"zap", "<path d='M13 2 3 14h9l-1 8 10-12h-9l1-8z'/>"},
    {"play", "<path d='m6 4 14 8-14 8z'/>"},
    {"table", "<rect x='3' y='3' width='18' height='18' rx='2'/><path d='M3 9h18M3 15h18M9 9v12'/>"},
    {"key", "<circle cx='7.5' cy='15.5' r='4.5'/><path d='m10.7 12.3 9.8-9.8M16 7l3 3M18.5 4.5l2 2'/>"},
    {"diamond", "<path d='M12 3 21 12 12 21 3 12z'/>"},
    {"database", "<ellipse cx='12' cy='5' rx='9' ry='3'/><path d='M21 12c0 1.7-4 3-9 3s-9-1.3-9-3'/><path d='M3 5v14c0 1.7 4 3 9 3s9-1.3 9-3V5'/>"},
    {"sun", "<circle cx='12' cy='12' r='4.5'/><path d='M12 1.5v2.5M12 20v2.5M4.6 4.6l1.8 1.8M17.6 17.6l1.8 1.8M1.5 12H4M20 12h2.5M4.6 19.4l1.8-1.8M17.6 6.4l1.8-1.8'/>"},
    {"moon", "<path d='M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z'/>"},
    {"x", "<path d='M18 6 6 18M6 6l12 12'/>"},
    {"refresh", "<path d='M23 4v6h-6'/><path d='M20.5 15a9 9 0 1 1-2.1-9.4L23 10'/>"},
    {"cpu", "<rect x='4' y='4' width='16' height='16' rx='2'/><rect x='9' y='9' width='6' height='6'/><path d='M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3'/>"},
    {"trash", "<path d='M3 6h18M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2'/>"},
    {"layers", "<path d='m12 2 10 5-10 5L2 7z'/><path d='m2 17 10 5 10-5M2 12l10 5 10-5'/>"},
    {"book", "<path d='M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z'/><path d='M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z'/>"},
    {"activity", "<path d='M22 12h-4l-3 9L9 3l-3 9H2'/>"},
    {"disk", "<path d='M22 12H2'/><path d='M5.5 5.1 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.5-6.9A2 2 0 0 0 16.8 4H7.2a2 2 0 0 0-1.7 1.1z'/><path d='M6 16h.01M10 16h.01'/>"},
    {"check", "<path d='M20 6 9 17l-5-5'/>"},
    {"alert", "<circle cx='12' cy='12' r='10'/><path d='M12 8v4M12 16h.01'/>"},
    {"terminal", "<path d='m4 17 6-6-6-6M12 19h8'/>"},
    {"logout", "<path d='M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4'/><path d='m16 17 5-5-5-5M21 12H9'/>"},
};

static QPixmap renderSvg(const QByteArray& svg, int size) {
    qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm((int)(size * dpr), (int)(size * dpr));
    pm.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p);
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon icon(const char* name, const QColor& color, int size) {
    for (const IconDef& d : ICONS) {
        if (strcmp(d.name, name) != 0) continue;
        QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='" +
                         hex(color).toUtf8() + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>" +
                         QByteArray(d.svg) + "</svg>";
        return QIcon(renderSvg(svg, size));
    }
    return QIcon();
}

QPixmap logoPixmap(int size) {
    QByteArray svg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><defs>"
        "<linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>"
        "<stop offset='0' stop-color='#38bdf8'/><stop offset='1' stop-color='#8b5cf6'/></linearGradient></defs>"
        "<rect width='32' height='32' rx='8' fill='url(#g)'/>"
        "<g fill='none' stroke='#fff' stroke-width='2.2' stroke-linecap='round'>"
        "<ellipse cx='16' cy='9.5' rx='8' ry='3'/><path d='M8 9.5v13c0 1.7 3.6 3 8 3s8-1.3 8-3v-13'/>"
        "<path d='M8 16c0 1.7 3.6 3 8 3s8-1.3 8-3'/></g></svg>";
    return renderSvg(svg, size);
}

// ---------------------------------------------------------------- fonts

static QFont pickFont(const char* const* families, int pointSize, bool mono) {
    for (int i = 0; families[i]; i++) {
        if (QFontDatabase::hasFamily(families[i])) {
            QFont f(families[i], pointSize);
            if (mono) f.setStyleHint(QFont::Monospace);
            return f;
        }
    }
    QFont f = mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QApplication::font();
    f.setPointSize(pointSize);
    return f;
}

QFont monoFont(int pointSize) {
    static const char* const families[] = {"Cascadia Mono", "Cascadia Code", "JetBrains Mono", "Consolas",
                                           "SF Mono", "Menlo", "DejaVu Sans Mono", nullptr};
    return pickFont(families, pointSize, true);
}

QFont uiFont(int pointSize) {
    static const char* const families[] = {"Segoe UI", "Inter", "SF Pro Text", "Helvetica Neue", "Ubuntu",
                                           "DejaVu Sans", nullptr};
    return pickFont(families, pointSize, false);
}
