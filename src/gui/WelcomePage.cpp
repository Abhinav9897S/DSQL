#include "gui/WelcomePage.h"
#include "gui/DsInfo.h"
#include "gui/Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QPainter>
#include <QRadialGradient>
#include <QFileInfo>

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // top bar: logo on the left, theme switch on the right
    QWidget* bar = new QWidget(this);
    QHBoxLayout* row = new QHBoxLayout(bar);
    row->setContentsMargins(26, 14, 22, 6);
    QLabel* logo = new QLabel(bar);
    logo->setPixmap(logoPixmap(26));
    row->addWidget(logo);
    QLabel* brand = new QLabel("<b>MiniSQL</b>&nbsp;<span style='font-weight:400'>Workbench</span>", bar);
    brand->setFont(uiFont(12));
    row->addWidget(brand);
    row->addStretch();
    themeButton_ = new QToolButton(bar);
    themeButton_->setToolTip("Switch between dark and light");
    themeButton_->setIconSize(QSize(18, 18));
    connect(themeButton_, &QToolButton::clicked, this, &WelcomePage::toggleTheme);
    row->addWidget(themeButton_);
    col->addWidget(bar);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }");
    col->addWidget(scroll_, 1);
    rebuild();
}

void WelcomePage::setRecent(const DynArray<QString>& folders) {
    recent_ = folders;
    rebuild();
}

void WelcomePage::refreshTheme() {
    rebuild();
    update();
}

// soft coloured glows behind everything
void WelcomePage::paintEvent(QPaintEvent*) {
    const ThemeColors& t = theme();
    QPainter p(this);
    p.fillRect(rect(), t.bg);
    QColor a = t.accent, b = t.accent2;
    a.setAlphaF(t.dark ? 0.16 : 0.12);
    b.setAlphaF(t.dark ? 0.14 : 0.10);
    QRadialGradient g1(QPointF(width() * 0.12, -height() * 0.1), width() * 0.6);
    g1.setColorAt(0, a);
    g1.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), g1);
    QRadialGradient g2(QPointF(width() * 1.0, height() * 0.12), width() * 0.55);
    g2.setColorAt(0, b);
    g2.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), g2);
}

static QPushButton* startButton(const char* iconName, const QString& title, const QString& sub, QWidget* parent) {
    const ThemeColors& t = theme();
    QPushButton* btn = new QPushButton(parent);
    btn->setObjectName("startItem");
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(64);
    QHBoxLayout* row = new QHBoxLayout(btn);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(14);
    QLabel* ic = new QLabel(btn);
    ic->setFixedSize(38, 38);
    ic->setAlignment(Qt::AlignCenter);
    ic->setPixmap(icon(iconName, t.accent, 20).pixmap(20, 20));
    ic->setStyleSheet(QString("background: %1; border-radius: 9px;").arg(hex(mix(t.accent, t.panel, 0.15))));
    QLabel* text = new QLabel(QString("<b style='font-size:11pt'>%1</b><br><span style='color:%2'>%3</span>")
                                  .arg(title, hex(t.muted), sub),
                              btn);
    for (QLabel* l : {ic, text}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(ic);
    row->addWidget(text, 1);
    return btn;
}

static QLabel* cardTitle(const QString& text, QWidget* parent) {
    QLabel* l = new QLabel(text.toUpper(), parent);
    l->setObjectName("sectionTitle");
    QFont f = uiFont(8);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    return l;
}

void WelcomePage::rebuild() {
    const ThemeColors& t = theme();
    themeButton_->setIcon(icon(t.dark ? "sun" : "moon", t.text2, 18));

    QWidget* page = new QWidget();
    QHBoxLayout* center = new QHBoxLayout(page);
    center->setContentsMargins(24, 10, 24, 30);
    QWidget* body = new QWidget(page);
    body->setMaximumWidth(1060);
    center->addStretch();
    center->addWidget(body, 100);
    center->addStretch();

    QVBoxLayout* col = new QVBoxLayout(body);
    col->setSpacing(14);

    QLabel* hero = new QLabel(QString("Mini<span style='color:%1'>SQL</span> Workbench").arg(hex(t.accent)), body);
    QFont hf = uiFont(30);
    hf.setWeight(QFont::Bold);
    hero->setFont(hf);
    hero->setContentsMargins(0, 18, 0, 0);
    col->addWidget(hero);
    QLabel* sub = new QLabel("A relational database engine written in C++ from hand-built data structures, with no STL "
                             "containers anywhere. Open a folder to start. Every query you run shows which structures "
                             "did the work.",
                             body);
    sub->setWordWrap(true);
    sub->setMaximumWidth(680);
    sub->setFont(uiFont(11));
    sub->setStyleSheet(QString("color: %1").arg(hex(t.text2)));
    col->addWidget(sub);
    col->addSpacing(14);

    // Start + Recent side by side
    QHBoxLayout* cards = new QHBoxLayout();
    cards->setSpacing(18);

    QFrame* start = new QFrame(body);
    start->setObjectName("card");
    QVBoxLayout* sc = new QVBoxLayout(start);
    sc->setContentsMargins(18, 16, 18, 16);
    sc->addWidget(cardTitle("Start", start));
    QPushButton* nf = startButton("folderPlus", "New folder…", "Create a workspace with a starter script", start);
    QPushButton* of = startButton("folder", "Open folder…", "Open an existing workspace, or any folder", start);
    QPushButton* mem = startButton("cpu", "In-memory session", "Try things out. Nothing is written to disk", start);
    connect(nf, &QPushButton::clicked, this, &WelcomePage::newFolder);
    connect(of, &QPushButton::clicked, this, &WelcomePage::openFolder);
    connect(mem, &QPushButton::clicked, this, &WelcomePage::memorySession);
    sc->addWidget(nf);
    sc->addWidget(of);
    sc->addWidget(mem);
    sc->addStretch();
    cards->addWidget(start, 1);

    QFrame* recent = new QFrame(body);
    recent->setObjectName("card");
    QVBoxLayout* rc = new QVBoxLayout(recent);
    rc->setContentsMargins(18, 16, 18, 16);
    rc->setSpacing(4);
    rc->addWidget(cardTitle("Recent", recent));
    if (recent_.empty()) {
        QLabel* none = new QLabel("No recent folders yet. Folders you open show up here.", recent);
        none->setObjectName("muted");
        none->setWordWrap(true);
        rc->addWidget(none);
    }
    for (int i = 0; i < recent_.size(); i++) {
        QString path = recent_[i];
        ClickFrame* rowFrame = new ClickFrame(recent);
        rowFrame->setCursor(Qt::PointingHandCursor);
        rowFrame->setToolTip(path);
        rowFrame->setObjectName("recentRow");
        rowFrame->setStyleSheet(QString("QFrame#recentRow { border-radius: 8px; } QFrame#recentRow:hover { background: %1; }")
                                    .arg(hex(t.panel2)));
        QHBoxLayout* rl = new QHBoxLayout(rowFrame);
        rl->setContentsMargins(8, 6, 6, 6);
        QLabel* ic = new QLabel(rowFrame);
        ic->setPixmap(icon("folder", t.accent, 18).pixmap(18, 18));
        QString name = QFileInfo(path).fileName();
        if (name.isEmpty()) name = path;
        QFontMetrics fm(rowFrame->font());
        QLabel* text = new QLabel(QString("<b>%1</b><br><span style='color:%2'>%3</span>")
                                      .arg(name.toHtmlEscaped(), hex(t.muted),
                                           fm.elidedText(path, Qt::ElideMiddle, 380).toHtmlEscaped()),
                                  rowFrame);
        for (QLabel* l : {ic, text}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
        QToolButton* forget = new QToolButton(rowFrame);
        forget->setIcon(icon("x", t.muted, 14));
        forget->setToolTip("Remove from this list (the folder is not touched)");
        rl->addWidget(ic);
        rl->addWidget(text, 1);
        rl->addWidget(forget);
        connect(rowFrame, &ClickFrame::clicked, this, [this, path] { emit openRecent(path); });
        connect(forget, &QToolButton::clicked, this, [this, path] { emit forgetRecent(path); });
        rc->addWidget(rowFrame);
    }
    rc->addStretch();
    cards->addWidget(recent, 1);
    col->addLayout(cards);

    // a card per data structure
    col->addSpacing(12);
    col->addWidget(cardTitle("Under the hood", body));
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(12);
    for (int i = 0; i < DS_INFO_COUNT - 1; i++) {
        const DsInfo& d = DS_INFO[i];
        QString key = d.key;
        ClickFrame* card = new ClickFrame(body);
        card->setObjectName("dsCard");
        card->setCursor(Qt::PointingHandCursor);
        card->setStyleSheet(QString("QFrame#dsCard { background: %1; border: 1px solid %2; border-top: 3px solid %3; border-radius: 10px; }"
                                    "QFrame#dsCard:hover { border-color: %3; }")
                                .arg(hex(t.panel), hex(t.border), d.color));
        card->setMinimumHeight(128);
        QVBoxLayout* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 12, 14, 12);
        cl->setSpacing(4);
        QLabel* name = new QLabel(QString("<span style='color:%1'>&#9679;</span>&nbsp;<b>%2</b>").arg(d.color, d.name), card);
        name->setFont(uiFont(11));
        QLabel* role = new QLabel(d.role, card);
        role->setWordWrap(true);
        role->setMinimumHeight(QFontMetrics(role->font()).lineSpacing() * 3);
        role->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        role->setStyleSheet(QString("color: %1").arg(hex(t.text2)));
        QLabel* file = new QLabel(d.file, card);
        file->setFont(monoFont(8));
        file->setStyleSheet(QString("color: %1").arg(hex(t.muted)));
        for (QLabel* l : {name, role, file}) {
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            cl->addWidget(l);
        }
        cl->addStretch();
        connect(card, &ClickFrame::clicked, this, [this, key] { emit showDs(key); });
        grid->addWidget(card, i / 4, i % 4);
    }
    col->addLayout(grid);

    QLabel* keys = new QLabel("Ctrl+Enter run statement &nbsp;&nbsp;·&nbsp;&nbsp; Ctrl+Shift+Enter run all "
                              "&nbsp;&nbsp;·&nbsp;&nbsp; Ctrl+S save &nbsp;&nbsp;·&nbsp;&nbsp; Ctrl+/ comment",
                              body);
    keys->setObjectName("muted");
    keys->setContentsMargins(0, 14, 0, 0);
    col->addWidget(keys);
    col->addStretch();

    scroll_->setWidget(page);
}
