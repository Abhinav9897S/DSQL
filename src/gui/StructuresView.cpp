#include "gui/StructuresView.h"
#include "gui/ExplainView.h"
#include "gui/Theme.h"
#include "gui/QtStr.h"
#include "ds/Queue.h"
#include "index/Index.h"
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPainter>
#include <QPainterPath>
#include <QHelpEvent>
#include <QToolTip>
#include <QTimer>

static const QColor TEAL("#2dd4bf");
static const QColor GREEN("#34d399");
static const QColor ORANGE("#fb923c");
static const QColor AMBER("#fbbf24");

static QLabel* metaLabel(const QString& html, QWidget* parent) {
    QLabel* l = new QLabel(QString("<span style='color:%1'>%2</span>").arg(hex(theme().muted), html), parent);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    return l;
}

static QString b(long long v) { return QString("<b style='color:%1'>%2</b>").arg(hex(theme().text2)).arg(v); }

// ---------------------------------------------------------------- B+ tree

namespace {

class BPlusTreeWidget : public QWidget {
public:
    BPlusTreeWidget(const BPlusTree<Value>& tree, QWidget* parent) : QWidget(parent), font_(monoFont(8)) {
        typedef BPlusTree<Value>::Node Node;
        struct Pending {
            const Node* n = nullptr;
            int parent = -1;
            int level = 0;
        };
        rows_ = tree.nodeCount() > 240;   // too big to draw as a real tree
        const int perLevel = 24;
        DynArray<int> shown;

        // breadth-first walk, using our own queue
        Queue<Pending> q;
        q.push(Pending{tree.root(), -1, 0});
        while (!q.empty()) {
            Pending p = q.pop();
            if (p.level >= levelTotal_.size()) {
                levelTotal_.push(0);
                shown.push(0);
            }
            levelTotal_[p.level]++;
            int idx = -1;
            if (!rows_ || shown[p.level] < perLevel) {
                Box box;
                box.leaf = p.n->leaf;
                box.level = p.level;
                box.parent = p.parent;
                for (int k = 0; k < p.n->n; k++) box.keys.push(p.n->keys[k].toString());
                if (p.n->leaf)
                    for (int k = 0; k < p.n->n; k++) box.rids.push(p.n->vals[k]);
                idx = boxes_.size();
                boxes_.push(std::move(box));
                shown[p.level]++;
                if (p.parent >= 0) {
                    if (boxes_[p.parent].firstChild < 0) boxes_[p.parent].firstChild = idx;
                    boxes_[p.parent].kids++;
                }
            }
            if (!p.n->leaf)
                for (int k = 0; k <= p.n->n; k++) q.push(Pending{p.n->kids[k], idx, p.level + 1});
        }
        layout();
    }

    int rootX() const { return boxes_.empty() ? 0 : (int)(boxes_[0].x + boxes_[0].w / 2); }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemeColors& t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font_);
        QFontMetrics fm(font_);

        // edges from a parent's key boundary to the top of each child
        if (!rows_) {
            p.setPen(QPen(t.border2, 1.2));
            for (int i = 0; i < boxes_.size(); i++) {
                const Box& c = boxes_[i];
                if (c.parent < 0) continue;
                const Box& par = boxes_[c.parent];
                int k = i - par.firstChild;
                double ax = par.x + 3;
                for (int j = 0; j < k && j < par.cells.size(); j++) ax += par.cells[j];
                if (k >= par.keys.size()) ax = par.x + par.w - 3;
                p.drawLine(QPointF(ax, y(par) + NODE_H), QPointF(c.x + c.w / 2, y(c)));
            }
        }

        int lastLeaf = -1;
        for (int i = 0; i < boxes_.size(); i++) {
            const Box& bx = boxes_[i];
            QRectF r(bx.x, y(bx), bx.w, NODE_H);
            QColor edge = bx.leaf ? TEAL : mix(TEAL, t.border2, 0.5);
            p.setPen(QPen(edge, 1.2));
            p.setBrush(bx.leaf ? mix(TEAL, t.panel, 0.12) : t.panel);
            p.drawRoundedRect(r, 5, 5);
            double cx = bx.x;
            for (int k = 0; k < bx.keys.size(); k++) {
                if (k) {
                    p.setPen(QPen(mix(edge, t.panel, 0.5), 1));
                    p.drawLine(QPointF(cx, r.top() + 4), QPointF(cx, r.bottom() - 4));
                }
                p.setPen(t.text);
                p.drawText(QRectF(cx, r.top(), bx.cells[k], NODE_H), Qt::AlignCenter, qs(bx.keys[k]));
                cx += bx.cells[k];
            }
            if (bx.keys.empty()) {
                p.setPen(t.muted);
                p.drawText(r, Qt::AlignCenter, "empty");
            }
            // leaf -> next leaf
            if (bx.leaf) {
                if (lastLeaf >= 0 && boxes_[lastLeaf].level == bx.level) {
                    const Box& prev = boxes_[lastLeaf];
                    double y0 = y(bx) + NODE_H / 2, x0 = prev.x + prev.w + 2, x1 = bx.x - 2;
                    p.setPen(QPen(TEAL, 1.3));
                    p.drawLine(QPointF(x0, y0), QPointF(x1, y0));
                    QPainterPath head;
                    head.moveTo(x1 - 5, y0 - 3.5);
                    head.lineTo(x1, y0);
                    head.lineTo(x1 - 5, y0 + 3.5);
                    p.drawPath(head);
                }
                lastLeaf = i;
            }
        }

        if (rows_) {
            for (int lvl = 0; lvl < levelTotal_.size(); lvl++) {
                double yy = MARGIN + lvl * LEVEL_H;
                p.setPen(t.muted);
                QString tag = lvl == 0 ? "root" : lvl == levelTotal_.size() - 1 ? "leaves" : QString("level %1").arg(lvl);
                p.drawText(QRectF(4, yy, LABEL_W - 8, NODE_H), Qt::AlignLeft | Qt::AlignVCenter, tag);
                int shownHere = 0;
                double right = LABEL_W;
                for (int i = 0; i < boxes_.size(); i++)
                    if (boxes_[i].level == lvl) {
                        shownHere++;
                        right = boxes_[i].x + boxes_[i].w;
                    }
                if (levelTotal_[lvl] > shownHere)
                    p.drawText(QPointF(right + 10, yy + NODE_H / 2 + fm.ascent() / 2 - 1),
                               QString("+%1 more").arg(levelTotal_[lvl] - shownHere));
            }
        }
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            QHelpEvent* h = static_cast<QHelpEvent*>(e);
            for (int i = 0; i < boxes_.size(); i++) {
                const Box& bx = boxes_[i];
                if (!QRectF(bx.x, y(bx), bx.w, NODE_H).contains(h->pos())) continue;
                QString tip = bx.leaf ? "Leaf. Keys and the row ids they point to:\n" : "Internal node. Separator keys only:\n";
                for (int k = 0; k < bx.keys.size(); k++) {
                    tip += qs(bx.keys[k]);
                    if (bx.leaf) tip += QString(" -> row %1").arg(bx.rids[k]);
                    tip += k + 1 < bx.keys.size() ? ", " : "";
                }
                QToolTip::showText(h->globalPos(), tip, this);
                return true;
            }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    struct Box {
        DynArray<Str> keys;
        DynArray<int> rids;
        DynArray<double> cells;   // width of each key cell
        bool leaf = false;
        int level = 0;
        int parent = -1;
        int firstChild = -1;
        int kids = 0;
        double x = 0;
        double w = 0;
    };

    static constexpr double NODE_H = 26, LEVEL_H = 60, GAP = 16, MARGIN = 12, LABEL_W = 60;

    double y(const Box& bx) const { return MARGIN + bx.level * LEVEL_H; }

    void layout() {
        QFontMetrics fm(font_);
        for (int i = 0; i < boxes_.size(); i++) {
            Box& bx = boxes_[i];
            bx.w = 0;
            for (int k = 0; k < bx.keys.size(); k++) {
                double cw = fm.horizontalAdvance(qs(bx.keys[k])) + 14;
                bx.cells.push(cw);
                bx.w += cw;
            }
            if (bx.keys.empty()) bx.w = 46;
        }
        int levels = levelTotal_.size();
        double maxRight = 0;
        if (rows_) {
            for (int lvl = 0; lvl < levels; lvl++) {
                double x = LABEL_W;
                for (int i = 0; i < boxes_.size(); i++) {
                    if (boxes_[i].level != lvl) continue;
                    boxes_[i].x = x;
                    x += boxes_[i].w + GAP;
                }
                maxRight = qMax(maxRight, x + 70);
            }
        } else {
            // leaves left to right, then every parent centred over its children
            double x = MARGIN;
            for (int i = 0; i < boxes_.size(); i++)
                if (boxes_[i].level == levels - 1) {
                    boxes_[i].x = x;
                    x += boxes_[i].w + GAP;
                }
            for (int lvl = levels - 2; lvl >= 0; lvl--) {
                double prevRight = -1e9;
                for (int i = 0; i < boxes_.size(); i++) {
                    Box& bx = boxes_[i];
                    if (bx.level != lvl) continue;
                    if (bx.kids > 0) {
                        const Box& first = boxes_[bx.firstChild];
                        const Box& last = boxes_[bx.firstChild + bx.kids - 1];
                        double center = (first.x + first.w / 2 + last.x + last.w / 2) / 2;
                        bx.x = center - bx.w / 2;
                    }
                    if (bx.x < prevRight + GAP) bx.x = prevRight + GAP;
                    if (bx.x < MARGIN) bx.x = MARGIN;
                    prevRight = bx.x + bx.w;
                }
            }
            for (int i = 0; i < boxes_.size(); i++) maxRight = qMax(maxRight, boxes_[i].x + boxes_[i].w);
        }
        int w = (int)(maxRight + MARGIN);
        int h = (int)(MARGIN * 2 + (levels - 1) * LEVEL_H + NODE_H);
        setFixedSize(qMax(w, 120), h);
    }

    DynArray<Box> boxes_;
    DynArray<int> levelTotal_;
    bool rows_ = false;
    QFont font_;
};

// ---------------------------------------------------------------- AVL tree

class AvlTreeWidget : public QWidget {
public:
    AvlTreeWidget(const AVLTree<Value>& tree, QWidget* parent) : QWidget(parent), font_(monoFont(8)) {
        int order = 0;
        root_ = walk(tree.root(), 0, order);
        int w = qMax(220, (int)(dots_.size() * SPACING + 2 * MARGIN));
        setFixedSize(w, (int)((depth_ + 1) * LEVEL_H + 2 * MARGIN));
    }

    int rootX() const { return root_ >= 0 ? (int)cx(dots_[root_]) : 0; }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemeColors& t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (dots_.empty()) {
            p.setPen(t.muted);
            p.drawText(rect(), Qt::AlignCenter, "empty tree");
            return;
        }
        p.setPen(QPen(t.border2, 1.3));
        for (int i = 0; i < edges_.size(); i++) {
            const Dot& a = dots_[edges_[i].from];
            const Dot& c = dots_[edges_[i].to];
            p.drawLine(QPointF(cx(a), cy(a)), QPointF(cx(c), cy(c)));
        }
        p.setFont(font_);
        QFontMetrics fm(font_);
        for (int i = 0; i < dots_.size(); i++) {
            const Dot& d = dots_[i];
            QPointF c(cx(d), cy(d));
            p.setPen(QPen(GREEN, 1.5));
            p.setBrush(mix(GREEN, t.panel, 0.13));
            p.drawEllipse(c, R, R);
            p.setPen(t.text);
            QString key = fm.elidedText(qs(d.key), Qt::ElideRight, (int)(2 * R - 4));
            p.drawText(QRectF(c.x() - R, c.y() - R, 2 * R, 2 * R), Qt::AlignCenter, key);
            if (d.rows > 1) {
                p.setPen(t.muted);
                p.drawText(QPointF(c.x() + R + 1, c.y() - R + 6), QString("x%1").arg(d.rows));
            }
            if (d.cut) {
                p.setPen(t.muted);
                p.drawText(QRectF(c.x() - R, c.y() + R, 2 * R, 14), Qt::AlignCenter, "...");
            }
        }
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            QHelpEvent* h = static_cast<QHelpEvent*>(e);
            for (int i = 0; i < dots_.size(); i++) {
                const Dot& d = dots_[i];
                QPointF diff = h->pos() - QPointF(cx(d), cy(d));
                if (diff.x() * diff.x() + diff.y() * diff.y() > R * R) continue;
                QToolTip::showText(h->globalPos(),
                                   QString("key %1\n%2 row id(s) share it\nsubtree height %3").arg(qs(d.key)).arg(d.rows).arg(d.height),
                                   this);
                return true;
            }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    struct Dot {
        Str key;
        int rows = 0;
        int height = 0;
        int order = 0;   // in-order position = column
        int depth = 0;
        bool cut = false;
    };
    struct Edge {
        int from = 0;
        int to = 0;
    };

    static constexpr double SPACING = 42, LEVEL_H = 52, MARGIN = 20, R = 15;
    static constexpr int MAX_DEPTH = 6;

    double cx(const Dot& d) const { return MARGIN + d.order * SPACING + SPACING / 2; }
    double cy(const Dot& d) const { return MARGIN + d.depth * LEVEL_H + R; }

    // in-order walk: x comes from the visit order, y from the depth
    int walk(const AVLTree<Value>::Node* n, int depth, int& order) {
        if (!n) return -1;
        if (depth > MAX_DEPTH) return -1;
        int l = walk(n->left, depth + 1, order);
        Dot d;
        d.key = n->key.toString();
        d.rows = n->rids.size();
        d.height = n->height;
        d.order = order++;
        d.depth = depth;
        d.cut = depth == MAX_DEPTH && (n->left || n->right);
        int me = dots_.size();
        dots_.push(std::move(d));
        int r = walk(n->right, depth + 1, order);
        if (l >= 0) edges_.push(Edge{me, l});
        if (r >= 0) edges_.push(Edge{me, r});
        if (depth > depth_) depth_ = depth;
        return me;
    }

    DynArray<Dot> dots_;
    DynArray<Edge> edges_;
    int root_ = -1;
    int depth_ = 0;
    QFont font_;
};

// ---------------------------------------------------------------- pages

class PagesWidget : public QWidget {
public:
    PagesWidget(Table* t, QWidget* parent) : QWidget(parent), per_(t->slotsPerPage) {
        for (int p = 0; p < t->pageCount(); p++) fill_.push(t->pageFill(p));
        QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        int n = qMin(fill_.size(), MAX_SHOWN);
        int perRow = qMax(1, (w - 2) / STEP_X);
        int rows = (n + perRow - 1) / perRow;
        return qMax(30, rows * STEP_Y + 4);
    }
    QSize sizeHint() const override { return QSize(240, heightForWidth(240)); }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemeColors& t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (fill_.empty()) {
            p.setPen(t.muted);
            p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, "No pages yet. The first INSERT allocates one.");
            return;
        }
        int perRow = qMax(1, (width() - 2) / STEP_X);
        for (int i = 0; i < fill_.size() && i < MAX_SHOWN; i++) {
            QRectF r = cell(i, perRow);
            p.setPen(QPen(t.border2, 1));
            p.setBrush(t.panel);
            p.drawRoundedRect(r, 3, 3);
            double h = (r.height() - 2) * fill_[i] / per_;
            QRectF f(r.left() + 1, r.bottom() - 1 - h, r.width() - 2, h);
            p.setPen(Qt::NoPen);
            p.setBrush(mix(theme().accent, t.panel, 0.85));
            p.drawRoundedRect(f, 2, 2);
        }
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            QHelpEvent* h = static_cast<QHelpEvent*>(e);
            int perRow = qMax(1, (width() - 2) / STEP_X);
            for (int i = 0; i < fill_.size() && i < MAX_SHOWN; i++)
                if (cell(i, perRow).contains(h->pos())) {
                    QToolTip::showText(h->globalPos(), QString("page %1: %2 of %3 slots used").arg(i).arg(fill_[i]).arg(per_), this);
                    return true;
                }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    static constexpr int STEP_X = 17, STEP_Y = 29, MAX_SHOWN = 600;
    QRectF cell(int i, int perRow) const { return QRectF(1 + (i % perRow) * STEP_X, 2 + (i / perRow) * STEP_Y, 14, 25); }
    DynArray<int> fill_;
    int per_;
};

// ---------------------------------------------------------------- chips (LRU list, hash buckets)

// lays out small labelled boxes left to right, wrapping at the widget's width
class ChipFlow : public QWidget {
public:
    explicit ChipFlow(QWidget* parent) : QWidget(parent), font_(monoFont(8)) {
        QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    struct Chip {
        QString text;
        QColor color;
        bool plain = false;    // arrow or caption, no box
        bool dashed = false;
        bool newLine = false;  // start a new row before this chip
        QString tip;
    };
    void add(const Chip& c) { chips_.push(c); }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        DynArray<QRectF> rects;
        return place(w, rects);
    }
    QSize sizeHint() const override { return QSize(260, heightForWidth(260)); }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemeColors& t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font_);
        DynArray<QRectF> rects;
        place(width(), rects);
        for (int i = 0; i < chips_.size(); i++) {
            const Chip& c = chips_[i];
            if (!c.plain) {
                QPen pen(mix(c.color, t.border, 0.55), 1.1);
                if (c.dashed) pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                p.setBrush(mix(c.color, t.panel, 0.1));
                p.drawRoundedRect(rects[i], 4, 4);
            }
            p.setPen(c.plain ? t.muted : t.text);
            p.drawText(rects[i], Qt::AlignCenter, c.text);
        }
    }

    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            QHelpEvent* h = static_cast<QHelpEvent*>(e);
            DynArray<QRectF> rects;
            place(width(), rects);
            for (int i = 0; i < chips_.size(); i++)
                if (!chips_[i].tip.isEmpty() && rects[i].contains(h->pos())) {
                    QToolTip::showText(h->globalPos(), chips_[i].tip, this);
                    return true;
                }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    int place(int width, DynArray<QRectF>& rects) const {
        QFontMetrics fm(font_);
        double x = 0, y = 0, h = 22;
        for (int i = 0; i < chips_.size(); i++) {
            const Chip& c = chips_[i];
            double w = fm.horizontalAdvance(c.text) + (c.plain ? 6 : 14);
            if ((c.newLine && i > 0) || (x > 0 && x + w > width)) {
                x = 0;
                y += h + 6;
            }
            rects.push(QRectF(x, y, w, h));
            x += w + 5;
        }
        return (int)(y + h + 2);
    }

    DynArray<Chip> chips_;
    QFont font_;
};

}   // namespace

// ---------------------------------------------------------------- the tab

StructuresView::StructuresView(Catalog& cat, QWidget* parent) : QWidget(parent), cat_(cat) {
    QVBoxLayout* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    QWidget* bar = new QWidget(this);
    QHBoxLayout* row = new QHBoxLayout(bar);
    row->setContentsMargins(12, 10, 12, 4);
    row->addWidget(new QLabel("Table", bar));
    tables_ = new QComboBox(bar);
    tables_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    row->addWidget(tables_, 1);
    QToolButton* again = new QToolButton(bar);
    again->setText("Refresh");
    again->setToolTip("Take a fresh snapshot of the structures");
    row->addWidget(again);
    col->addWidget(bar);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    col->addWidget(scroll_, 1);

    connect(tables_, &QComboBox::currentTextChanged, this, [this] { refresh(); });
    connect(again, &QToolButton::clicked, this, [this] { refresh(); });
    refresh();
}

void StructuresView::selectTable(const QString& name) {
    int i = tables_->findText(name);
    if (i >= 0) tables_->setCurrentIndex(i);
    refresh();
}

// pictures wider than the panel scroll sideways inside a framed box
static QScrollArea* framed(QWidget* picture, QWidget* parent) {
    const ThemeColors& t = theme();
    QScrollArea* box = new QScrollArea(parent);
    box->setWidget(picture);
    box->setWidgetResizable(false);
    box->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    box->setFixedHeight(qMin(picture->height() + 18, 440));
    box->setStyleSheet(QString("QScrollArea { background: %1; border: 1px solid %2; border-radius: 10px; }")
                           .arg(hex(t.panel2), hex(t.border)));
    picture->setStyleSheet("background: transparent;");
    return box;
}

void StructuresView::refresh() {
    const ThemeColors& t = theme();
    QString want = tables_->currentText();
    tables_->blockSignals(true);
    tables_->clear();
    if (cat_.isOpen())
        for (int i = 0; i < cat_.tableCount(); i++) tables_->addItem(qs(cat_.tableAt(i)->name));
    int pick = tables_->findText(want);
    if (pick >= 0) tables_->setCurrentIndex(pick);
    tables_->blockSignals(false);
    tables_->setEnabled(tables_->count() > 0);

    Table* tb = cat_.isOpen() && tables_->count() ? cat_.find(toStr(tables_->currentText()), false) : nullptr;

    QWidget* page = new QWidget();
    QVBoxLayout* col = new QVBoxLayout(page);
    col->setContentsMargins(12, 2, 12, 16);
    col->setSpacing(6);

    if (tb) {
        BPlusIndex* pk = dynamic_cast<BPlusIndex*>(tb->pk);
        if (pk) {
            col->addWidget(sectionTitle("B+ tree · primary key on " + qs(tb->cols[tb->pkCol].name), page));
            col->addWidget(metaLabel(QString("order %1 &nbsp; height %2 &nbsp; keys %3 &nbsp; nodes %4 &nbsp;&middot;&nbsp; "
                                             "hover a node for its row ids")
                                         .arg(b(pk->order()), b(pk->height()), b(pk->keys()), b(pk->nodes())),
                                     page));
            BPlusTreeWidget* pic = new BPlusTreeWidget(pk->tree(), nullptr);
            QScrollArea* box = framed(pic, page);
            col->addWidget(box);
            int rootX = pic->rootX();
            QTimer::singleShot(0, box, [box, rootX] {   // start with the root in view
                box->horizontalScrollBar()->setValue(rootX - box->viewport()->width() / 2);
            });
        } else {
            col->addWidget(sectionTitle("B+ tree", page));
            col->addWidget(metaLabel("This table has no primary key, so it has no B+ tree.", page));
        }

        for (int i = 0; i < tb->indexes.size(); i++) {
            AVLIndex* ix = dynamic_cast<AVLIndex*>(tb->indexes[i].index);
            if (!ix) continue;
            col->addWidget(sectionTitle("AVL tree · " + qs(tb->indexes[i].name) + " on " + qs(tb->cols[tb->indexes[i].col].name), page));
            col->addWidget(metaLabel(QString("height %1 &nbsp; distinct keys %2 &nbsp; row ids %3")
                                         .arg(b(ix->height()), b(ix->keys()), b(ix->tree().size())),
                                     page));
            AvlTreeWidget* pic = new AvlTreeWidget(ix->tree(), nullptr);
            QScrollArea* box = framed(pic, page);
            col->addWidget(box);
            int rootX = pic->rootX();
            QTimer::singleShot(0, box, [box, rootX] {   // start with the root in view
                box->horizontalScrollBar()->setValue(rootX - box->viewport()->width() / 2);
            });
        }
        if (tb->indexes.empty()) {
            col->addWidget(sectionTitle("AVL indexes", page));
            col->addWidget(metaLabel("None yet. <code>CREATE INDEX idx ON " + qs(tb->name) +
                                         "(column);</code> builds an AVL tree.", page));
        }

        col->addWidget(sectionTitle("Page directory", page));
        col->addWidget(metaLabel(QString("pages %1 &nbsp; slots per page %2 &nbsp; rows %3 &nbsp; slot size %4 bytes")
                                     .arg(b(tb->pageCount()), b(tb->slotsPerPage), b(tb->rowCount), b(tb->slotSize)),
                                 page));
        col->addWidget(new PagesWidget(tb, page));
    } else {
        col->addWidget(sectionTitle("Tables", page));
        col->addWidget(metaLabel(cat_.isOpen() ? "No tables yet. Create one and its B+ tree, indexes and pages show up here."
                                               : "Open a folder or an in-memory session first.",
                                 page));
    }

    if (cat_.isOpen()) {
        BufferPool& pool = cat_.pool();
        long long total = pool.hits + pool.misses;
        col->addWidget(sectionTitle("Buffer pool · LRU list", page));
        col->addWidget(metaLabel(QString("frames used %1 / %2 &nbsp; hits %3 &nbsp; misses %4 &nbsp; evictions %5 &nbsp; hit rate <b>%6</b>")
                                     .arg(b(pool.used()), b(pool.capacity()), b(pool.hits), b(pool.misses), b(pool.evictions))
                                     .arg(total ? QString::number(100.0 * pool.hits / total, 'f', 1) + "%" : QString("-")),
                                 page));
        ChipFlow* lru = new ChipFlow(page);
        lru->add({"most recent", t.muted, true, false, false, ""});
        int shown = 0;
        for (LinkedList<int>::Node* n = pool.lru().first(); n && shown < 60; n = pool.lru().after(n), shown++) {
            const BufferPool::Frame& f = pool.frame(n->val);
            QString label = qs(f.file ? f.file->table() : Str("?")) + QString(":p%1").arg(f.pageNo);
            lru->add({label + (f.dirty ? " *" : ""), ORANGE, false, f.dirty, false,
                      QString("frame %1%2").arg(n->val).arg(f.dirty ? ", dirty: changed, not written to disk yet" : "")});
        }
        if (pool.used() > shown) lru->add({QString("+%1").arg(pool.used() - shown), t.muted, true, false, false, ""});
        lru->add({pool.used() ? "least recent (next victim)" : "empty", t.muted, true, false, false, ""});
        col->addWidget(lru);

        HashTable<Str, Table*>& names = cat_.names();
        col->addWidget(sectionTitle("Catalog hash table", page));
        col->addWidget(metaLabel(QString("buckets %1 &nbsp; tables %2 &nbsp;&middot;&nbsp; each row is one bucket's chain")
                                     .arg(b(names.bucketCount()), b(names.size())),
                                 page));
        ChipFlow* buckets = new ChipFlow(page);
        for (int bk = 0; bk < names.bucketCount(); bk++) {
            HashTable<Str, Table*>::Entry* e = names.bucket(bk);
            if (!e && names.bucketCount() > 16) continue;   // keep big tables readable
            buckets->add({QString("[%1]").arg(bk, 2), t.muted, true, false, true, ""});
            if (!e) buckets->add({"empty", t.muted, true, false, false, ""});
            for (; e; e = e->next) {
                buckets->add({"->", t.muted, true, false, false, ""});
                buckets->add({qs(e->key), AMBER, false, false, false, "table " + qs(e->key)});
            }
        }
        col->addWidget(buckets);
    }

    col->addStretch();
    scroll_->setWidget(page);
}
