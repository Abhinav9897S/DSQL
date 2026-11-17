#include "gui/ExplainView.h"
#include "gui/DsInfo.h"
#include "gui/Theme.h"
#include "gui/QtStr.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cstring>

static QString esc(const QString& s) { return s.toHtmlEscaped(); }
static QString esc(const Str& s) { return qs(s).toHtmlEscaped(); }

QLabel* sectionTitle(const QString& text, QWidget* parent) {
    QLabel* l = new QLabel(text.toUpper(), parent);
    l->setObjectName("sectionTitle");
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.82);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setContentsMargins(0, 12, 0, 2);
    return l;
}

static QLabel* richLabel(const QString& html, QWidget* parent) {
    QLabel* l = new QLabel(parent);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setText(html);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

// ---------------------------------------------------------------- chips

DsChip::DsChip(const char* key, const QString& detail, QWidget* parent) : QFrame(parent), key_(key) {
    const ThemeColors& t = theme();
    const DsInfo& d = dsInfo(key);
    QColor c(d.color);
    setObjectName("dsChip");
    setCursor(Qt::PointingHandCursor);
    setToolTip(QString("What is a %1? Click to read about it.").arg(d.name));
    setStyleSheet(QString("QFrame#dsChip { background: %1; border: 1px solid %2; border-radius: 8px; }")
                      .arg(hex(mix(c, t.panel, 0.08)), hex(mix(c, t.border, 0.45))));
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(9, 6, 9, 6);
    l->setSpacing(1);
    QLabel* name = new QLabel(QString("<span style='color:%1'>&#9679;</span>&nbsp;<b>%2</b>").arg(d.color, d.name), this);
    QLabel* info = new QLabel(detail, this);
    info->setFont(monoFont(8));
    info->setStyleSheet(QString("color: %1").arg(hex(t.text2)));
    info->setWordWrap(true);
    for (QLabel* x : {name, info}) x->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->addWidget(name);
    l->addWidget(info);
}

void DsChip::mousePressEvent(QMouseEvent*) { emit clicked(key_); }

// one entry per structure the statement actually used, with its counters
struct Usage {
    const char* key;
    QString text;
};

static int usage(const DsStats& s, Usage out[]) {
    int n = 0;
    auto join = [](QString a, const QString& b) { return a.isEmpty() ? b : a + " · " + b; };
    long long rot = s.avlLL + s.avlRR + s.avlLR + s.avlRL;
    if (s.bptVisits || s.bptSplits || s.bptMerges) {
        QString t = QString("%1 visits").arg(s.bptVisits);
        if (s.bptSplits) t = join(t, QString("%1 splits").arg(s.bptSplits));
        if (s.bptMerges) t = join(t, QString("%1 merges").arg(s.bptMerges));
        if (s.bptBorrows) t = join(t, QString("%1 borrows").arg(s.bptBorrows));
        if (s.bptLeafHops) t = join(t, QString("%1 leaf hops").arg(s.bptLeafHops));
        out[n++] = {"bptree", t};
    }
    if (s.avlVisits || rot) out[n++] = {"avl", QString("%1 visits").arg(s.avlVisits) + (rot ? QString(" · %1 rotations").arg(rot) : "")};
    if (s.hashProbes) out[n++] = {"hash", QString("%1 probes").arg(s.hashProbes) + (s.hashRehashes ? QString(" · %1 resizes").arg(s.hashRehashes) : "")};
    if (s.poolHits || s.poolMisses)
        out[n++] = {"pool", QString("%1 hits · %2 misses").arg(s.poolHits).arg(s.poolMisses) +
                                (s.poolEvictions ? QString(" · %1 evicted").arg(s.poolEvictions) : "")};
    if (s.listMoves) out[n++] = {"list", QString("%1 relinks").arg(s.listMoves)};
    if (s.arrayGrows) out[n++] = {"array", QString("%1 regrows").arg(s.arrayGrows)};
    if (s.stackPush) out[n++] = {"stack", QString("%1 push · %2 pop").arg(s.stackPush).arg(s.stackPop)};
    if (s.queuePop) out[n++] = {"queue", QString("%1 tokens out").arg(s.queuePop)};
    if (s.sortCompares) out[n++] = {"sort", QString("%1 compares").arg(s.sortCompares)};
    if (s.diskReads || s.diskWrites) out[n++] = {"disk", QString("%1 reads · %2 writes").arg(s.diskReads).arg(s.diskWrites)};
    return n;
}

// ---------------------------------------------------------------- pipeline

PipelineWidget::PipelineWidget(const Result* r, QWidget* parent) : QWidget(parent) {
    setMinimumHeight(372);
    if (!r) return;
    real_ = true;
    s_ = r->stats;
    ok_ = r->ok;
    parsed_ = r->kind != "ERROR";
    executes_ = r->kind == "SELECT" || r->kind == "INSERT" || r->kind == "UPDATE" || r->kind == "DELETE";
    for (int i = 0; i < r->trace.steps.size(); i++) {
        const TraceStep& st = r->trace.steps[i];
        if (st.ds == "hash" && st.title.startsWith("Catalog")) catalog_ = true;
        if (st.ds == "bptree" || st.ds == "avl") index_ = true;
        if (st.ds == "disk") disk_ = true;
    }
    index_ = index_ || s_.bptVisits + s_.avlVisits > 0;
    disk_ = disk_ || s_.diskReads + s_.diskWrites > 0;
}

static void drawNode(QPainter& p, const QRectF& r, const char* key, const QString& title, const QString& sub, bool on) {
    const ThemeColors& t = theme();
    QColor c(dsInfo(key).color);
    QPen pen(on ? c : t.border2, on ? 1.4 : 1.0);
    if (!on) pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(on ? mix(c, t.panel, 0.12) : t.panel);
    p.drawRoundedRect(r, 8, 8);

    QFont f = uiFont(9);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(on ? t.text : t.muted);
    p.drawText(QRectF(r.left(), r.top() + 3, r.width(), r.height() / 2), Qt::AlignHCenter | Qt::AlignBottom, title);
    p.setFont(monoFont(8));
    p.setPen(on ? mix(c, t.text, 0.75) : t.muted);
    p.drawText(QRectF(r.left() + 4, r.center().y() + 1, r.width() - 8, r.height() / 2 - 2), Qt::AlignHCenter | Qt::AlignTop,
               p.fontMetrics().elidedText(sub, Qt::ElideRight, (int)r.width() - 8));
}

static void drawArrowDown(QPainter& p, double x, double y1, double y2, const QString& label) {
    const ThemeColors& t = theme();
    p.setPen(QPen(t.border2, 1.2));
    p.drawLine(QPointF(x, y1), QPointF(x, y2 - 2));
    QPainterPath head;
    head.moveTo(x - 4, y2 - 6);
    head.lineTo(x, y2);
    head.lineTo(x + 4, y2 - 6);
    p.drawPath(head);
    p.setFont(uiFont(8));
    p.setPen(t.muted);
    p.drawText(QRectF(x + 8, y1, 200, y2 - y1), Qt::AlignLeft | Qt::AlignVCenter, label);
}

void PipelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const DsStats& s = s_;
    double w = width();
    double cw = qMax(140.0, w * 0.42), sw = qMax(80.0, w * 0.27);
    double cx = w / 2;
    double nodeH = 42, gap = 24, y = 4;
    auto center = [&](double top) { return QRectF(cx - cw / 2, top, cw, nodeH); };

    long long touches = s.poolHits + s.poolMisses;
    const char* idxKey = s.avlVisits > s.bptVisits ? "avl" : "bptree";

    struct Row {
        const char* key;
        QString title, sub;
        bool on;
    } rows[3] = {
        {"sql", "Workbench", "SQL text", true},
        {"queue", "Tokenizer", QString("queue: %1 tokens").arg(s.queuePop), s.queuePop > 0},
        {"sql", "Parser", "recursive descent", parsed_},
    };
    if (!real_) rows[1].sub = "queue of tokens";
    const char* arrows[3] = {"text", "tokens", "command"};
    for (int i = 0; i < 3; i++) {
        drawNode(p, center(y), rows[i].key, rows[i].title, rows[i].sub, rows[i].on);
        drawArrowDown(p, cx, y + nodeH, y + nodeH + gap, arrows[i]);
        y += nodeH + gap;
    }

    // executor in the middle, catalog and index on the sides
    QRectF ex = center(y);
    QRectF cat(4, y, qMin(sw, ex.left() - 18), nodeH);
    QRectF idx(w - 4 - qMin(sw, w - ex.right() - 18), y, qMin(sw, w - ex.right() - 18), nodeH);
    drawNode(p, ex, "stack", "Executor", s.stackPush ? QString("stack: %1 pushes").arg(s.stackPush) : "plan + run",
             real_ && (executes_ || ok_));
    drawNode(p, cat, "hash", "Catalog", "hash table", catalog_);
    long long visits = s.bptVisits + s.avlVisits;
    drawNode(p, idx, idxKey, "Index", visits ? QString("%1 visits").arg(visits) : "B+ / AVL", index_);
    p.setPen(QPen(theme().border2, 1.2));
    p.drawLine(QPointF(cat.right() + 3, ex.center().y()), QPointF(ex.left() - 3, ex.center().y()));
    p.drawLine(QPointF(ex.right() + 3, ex.center().y()), QPointF(idx.left() - 3, ex.center().y()));
    drawArrowDown(p, cx, y + nodeH, y + nodeH + gap, "page requests");
    y += nodeH + gap;

    drawNode(p, center(y), "pool", "Buffer pool",
             touches ? QString("%1 hit · %2 miss").arg(s.poolHits).arg(s.poolMisses) : "hash + LRU list", touches > 0);
    drawArrowDown(p, cx, y + nodeH, y + nodeH + gap, "miss / write back");
    y += nodeH + gap;
    drawNode(p, center(y), "disk", "Pages on disk",
             s.diskReads || s.diskWrites ? QString("%1 read · %2 written").arg(s.diskReads).arg(s.diskWrites) : "4 KB pages",
             disk_);
}

// ---------------------------------------------------------------- the panel

ExplainView::ExplainView(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    showIntro();
}

void ExplainView::showResult(const Result& r) {
    shown_ = r;
    shown_.rows.clear();   // the grid has its own copy
    has_ = true;
    rebuild();
}

void ExplainView::showIntro() {
    has_ = false;
    rebuild();
}

void ExplainView::refreshTheme() { rebuild(); }

void ExplainView::rebuild() {
    const ThemeColors& t = theme();
    QWidget* page = new QWidget();
    QVBoxLayout* col = new QVBoxLayout(page);
    col->setContentsMargins(14, 10, 14, 16);
    col->setSpacing(6);

    if (!has_) {
        col->addWidget(sectionTitle("How a query runs", page));
        col->addWidget(richLabel(QString("<span style='color:%1'>Run any statement and this panel walks through it: "
                                         "the order the engine touched each data structure, what every step did "
                                         "and what it cost.</span>").arg(hex(t.text2)), page));
        col->addWidget(new PipelineWidget(nullptr, page));
        col->addWidget(sectionTitle("Structures in this engine", page));
        QGridLayout* grid = new QGridLayout();
        grid->setSpacing(7);
        for (int i = 0; i < DS_INFO_COUNT - 1; i++) {
            DsChip* chip = new DsChip(DS_INFO[i].key, DS_INFO[i].file, page);
            connect(chip, &DsChip::clicked, this, &ExplainView::learnRequested);
            grid->addWidget(chip, i / 2, i % 2);
        }
        col->addLayout(grid);
        col->addStretch();
        setWidget(page);
        return;
    }

    const Result& r = shown_;

    // header card
    QFrame* card = new QFrame(page);
    card->setObjectName("card");
    QVBoxLayout* cl = new QVBoxLayout(card);
    cl->setContentsMargins(12, 10, 12, 10);
    QColor kindColor = r.ok ? t.accent : t.err;
    QString head = QString("<span style='background-color:%1; color:%2; font-family:monospace; font-weight:600'>&nbsp;%3&nbsp;</span>"
                           "&nbsp;&nbsp;<span style='color:%4'>line %5 &middot; %6 ms</span>")
                       .arg(hex(mix(kindColor, t.panel, 0.18)), hex(kindColor), esc(r.kind), hex(t.muted))
                       .arg(r.line)
                       .arg(r.ms, 0, 'f', 3);
    cl->addWidget(richLabel(head, card));
    QLabel* sql = new QLabel(qs(r.sql), card);
    sql->setFont(monoFont(9));
    sql->setWordWrap(true);
    sql->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sql->setStyleSheet(QString("color: %1").arg(hex(t.text2)));
    cl->addWidget(sql);
    if (!r.plan.empty())
        cl->addWidget(richLabel(QString("<span style='color:%1'>Plan</span>&nbsp; <b>%2</b>").arg(hex(t.muted), esc(r.plan)), card));
    cl->addWidget(richLabel(QString("<span style='color:%1'>%2</span>").arg(hex(r.ok ? t.text : t.err), esc(r.message)), card));
    col->addWidget(card);

    col->addWidget(sectionTitle("Pipeline", page));
    col->addWidget(new PipelineWidget(&r, page));

    Usage used[16];
    int n = usage(r.stats, used);
    if (n) {
        col->addWidget(sectionTitle(QString("Data structures used (%1)").arg(n), page));
        QGridLayout* grid = new QGridLayout();
        grid->setSpacing(7);
        for (int i = 0; i < n; i++) {
            DsChip* chip = new DsChip(used[i].key, used[i].text, page);
            connect(chip, &DsChip::clicked, this, &ExplainView::learnRequested);
            grid->addWidget(chip, i / 2, i % 2);
        }
        col->addLayout(grid);
    }

    if (r.trace.steps.size()) {
        col->addWidget(sectionTitle(QString("Step by step (%1)").arg(r.trace.steps.size()), page));
        QString html = "<table width='100%' cellspacing='0' cellpadding='4'>";
        for (int i = 0; i < r.trace.steps.size(); i++) {
            const TraceStep& s = r.trace.steps[i];
            const DsInfo& d = dsInfo(s.ds.c_str());
            QColor c(d.color);
            QString badge = s.ds == "sql" ? "SQL" : d.name;
            html += QString("<tr><td width='24' valign='top' align='center'><b style='color:%1'>%2</b></td><td valign='top'>"
                            "<span style='background-color:%3; color:%1'>&nbsp;%4&nbsp;</span>&nbsp; <b>%5</b>%6<br>"
                            "<span style='color:%7'>%8</span></td></tr>")
                        .arg(hex(c))
                        .arg(i + 1)
                        .arg(hex(mix(c, t.panel, 0.16)), esc(badge), esc(s.title),
                             s.repeat > 1 ? QString(" <span style='color:%1'>&times;%2</span>").arg(hex(t.muted)).arg(s.repeat) : QString(),
                             hex(t.text2), esc(s.detail));
        }
        col->addWidget(richLabel(html + "</table>", page));
    }

    if (r.tokens.size()) {
        col->addWidget(sectionTitle(QString("Token queue (%1 tokens, FIFO)").arg(r.tokens.size()), page));
        QString html;
        for (int i = 0; i < r.tokens.size() && i < 120; i++) {
            const Token& tok = r.tokens[i];
            QColor c = tok.type == Tok::Keyword ? t.kw
                     : tok.type == Tok::String ? t.str
                     : (tok.type == Tok::Int || tok.type == Tok::Float) ? t.num
                     : tok.type == Tok::Symbol ? t.op : t.text;
            html += QString("<span style='background-color:%1; color:%2; font-family:monospace'>&nbsp;%3&nbsp;</span>"
                            "<span style='color:%4; font-size:7pt'>%5</span>&nbsp; ")
                        .arg(hex(t.panel3), hex(c), esc(tok.text), hex(t.muted), QString(tokName(tok.type)).toLower());
        }
        col->addWidget(richLabel(html, page));
    }

    col->addWidget(sectionTitle("Counters for this statement", page));
    struct { const char* label; long long v; } counters[] = {
        {"queue pushes", r.stats.queuePush}, {"queue pops", r.stats.queuePop}, {"stack pushes", r.stats.stackPush},
        {"stack pops", r.stats.stackPop}, {"DynArray regrows", r.stats.arrayGrows}, {"linked list relinks", r.stats.listMoves},
        {"hash chain probes", r.stats.hashProbes}, {"hash table resizes", r.stats.hashRehashes},
        {"AVL node visits", r.stats.avlVisits}, {"AVL LL rotations", r.stats.avlLL}, {"AVL RR rotations", r.stats.avlRR},
        {"AVL LR rotations", r.stats.avlLR}, {"AVL RL rotations", r.stats.avlRL}, {"B+ tree node visits", r.stats.bptVisits},
        {"B+ tree splits", r.stats.bptSplits}, {"B+ tree merges", r.stats.bptMerges}, {"B+ tree borrows", r.stats.bptBorrows},
        {"B+ tree leaf hops", r.stats.bptLeafHops}, {"buffer pool hits", r.stats.poolHits},
        {"buffer pool misses", r.stats.poolMisses}, {"buffer pool evictions", r.stats.poolEvictions},
        {"disk page reads", r.stats.diskReads}, {"disk page writes", r.stats.diskWrites},
        {"merge sort comparisons", r.stats.sortCompares},
    };
    QString table = "<table width='100%' cellspacing='0' cellpadding='3'>";
    int shown = 0;
    for (auto& c : counters) {
        if (!c.v) continue;
        table += QString("<tr><td>%1</td><td align='right' style='font-family:monospace; color:%2'>%3</td></tr>")
                     .arg(c.label, hex(t.text2)).arg(c.v);
        shown++;
    }
    col->addWidget(richLabel(shown ? table + "</table>" : QString("<span style='color:%1'>Nothing counted.</span>").arg(hex(t.muted)), page));
    col->addStretch();
    setWidget(page);
}
