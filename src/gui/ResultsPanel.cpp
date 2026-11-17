#include "gui/ResultsPanel.h"
#include "gui/Theme.h"
#include "gui/QtStr.h"
#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QLabel>
#include <QTabBar>

// ---------------------------------------------------------------- model

ResultModel::ResultModel(const Result& r, QObject* parent)
    : QAbstractTableModel(parent), columns_(r.columns), types_(r.types), rows_(r.rows) {}

int ResultModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : rows_.size(); }
int ResultModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : columns_.size(); }

QVariant ResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    const Value& v = rows_[index.row()][index.column()];
    Type type = types_[index.column()];
    bool number = type == Type::Int || type == Type::Float;
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return qs(v.toString());
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::Alignment((number ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter));
    if (role == Qt::ForegroundRole) {
        if (number) return theme().num;
        if (type == Type::Bool) return theme().kw;
    }
    if (role == Qt::FontRole) return monoFont(9);
    return QVariant();
}

QVariant ResultModel::headerData(int section, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (o == Qt::Vertical) return section + 1;
    return qs(columns_[section]) + "\n" + typeName(types_[section]);
}

// ---------------------------------------------------------------- panel

ResultsPanel::ResultsPanel(QWidget* parent) : QTabWidget(parent) {
    setDocumentMode(true);
    log_ = new QTableWidget(0, 6, this);
    log_->setHorizontalHeaderItem(0, new QTableWidgetItem(""));
    log_->setHorizontalHeaderItem(1, new QTableWidgetItem("#"));
    log_->setHorizontalHeaderItem(2, new QTableWidgetItem("Time"));
    log_->setHorizontalHeaderItem(3, new QTableWidgetItem("Statement"));
    log_->setHorizontalHeaderItem(4, new QTableWidgetItem("Message"));
    log_->setHorizontalHeaderItem(5, new QTableWidgetItem("Duration"));
    log_->verticalHeader()->hide();
    log_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    log_->setSelectionBehavior(QAbstractItemView::SelectRows);
    log_->setSelectionMode(QAbstractItemView::SingleSelection);
    log_->setShowGrid(false);
    log_->setWordWrap(false);
    log_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    log_->horizontalHeader()->setStretchLastSection(false);
    log_->setColumnWidth(0, 30);
    log_->setColumnWidth(1, 44);
    log_->setColumnWidth(2, 80);
    log_->setColumnWidth(3, 380);
    log_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    log_->setColumnWidth(5, 90);
    log_->verticalHeader()->setDefaultSectionSize(26);
    addTab(log_, icon("terminal", theme().text2, 16), "Output");

    connect(log_, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem* it = log_->item(row, 1);
        if (it) emit entrySelected(it->data(Qt::UserRole).toInt());
    });
    connect(this, &QTabWidget::currentChanged, this, [this](int i) {
        QVariant v = widget(i) ? widget(i)->property("historyIndex") : QVariant();
        if (v.isValid()) emit entrySelected(v.toInt());
    });
}

void ResultsPanel::fillLog(const DynArray<RunEntry>& history) {
    const ThemeColors& t = theme();
    log_->setRowCount(history.size());
    for (int i = 0; i < history.size(); i++) {
        const RunEntry& e = history[i];
        const Result& r = e.result;
        QTableWidgetItem* st = new QTableWidgetItem(icon(r.ok ? "check" : "alert", r.ok ? t.ok : t.err, 16), "");
        QTableWidgetItem* num = new QTableWidgetItem(QString::number(e.number));
        num->setData(Qt::UserRole, i);
        QString sql = qs(r.sql).simplified();
        QTableWidgetItem* stmt = new QTableWidgetItem(sql);
        stmt->setToolTip(qs(r.sql));
        stmt->setFont(monoFont(9));
        QTableWidgetItem* msg = new QTableWidgetItem(qs(r.message));
        msg->setToolTip(qs(r.message));
        if (!r.ok) msg->setForeground(t.err);
        QTableWidgetItem* ms = new QTableWidgetItem(QString::number(r.ms, 'f', 3) + " ms");
        QTableWidgetItem* time = new QTableWidgetItem(e.time);
        for (QTableWidgetItem* it : {num, time, ms}) it->setForeground(t.muted);
        log_->setItem(i, 0, st);
        log_->setItem(i, 1, num);
        log_->setItem(i, 2, time);
        log_->setItem(i, 3, stmt);
        log_->setItem(i, 4, msg);
        log_->setItem(i, 5, ms);
    }
    log_->scrollToBottom();
}

void ResultsPanel::showRun(const DynArray<RunEntry>& history, int firstOfRun) {
    history_ = &history;
    blockSignals(true);
    while (count() > 1) {   // drop the previous run's grids, keep Output (last)
        QWidget* w = widget(0);
        removeTab(0);
        delete w;
    }

    int grids = 0;
    int failed = -1;
    for (int i = firstOfRun; i < history.size(); i++) {
        const Result& r = history[i].result;
        if (!r.ok) failed = i;
        if (!r.ok || r.columns.empty()) continue;

        QWidget* page = new QWidget();
        page->setProperty("historyIndex", i);
        QVBoxLayout* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);
        QTableView* view = new QTableView(page);
        view->setModel(new ResultModel(r, view));
        view->setAlternatingRowColors(true);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->verticalHeader()->setDefaultSectionSize(24);
        view->horizontalHeader()->setResizeContentsPrecision(200);
        view->horizontalHeader()->setStretchLastSection(false);
        view->horizontalHeader()->setMinimumSectionSize(70);
        view->resizeColumnsToContents();
        col->addWidget(view, 1);
        QLabel* foot = new QLabel(QString("%1 row%2 · %3 ms%4")
                                      .arg(r.rows.size())
                                      .arg(r.rows.size() == 1 ? "" : "s")
                                      .arg(r.ms, 0, 'f', 3)
                                      .arg(r.plan.empty() ? QString() : " · " + qs(r.plan)),
                                  page);
        foot->setObjectName("muted");
        foot->setContentsMargins(10, 5, 10, 5);
        col->addWidget(foot);
        grids++;
        insertTab(count() - 1, page, icon("table", theme().text2, 16), QString("Result %1 (%2)").arg(grids).arg(r.rows.size()));
    }

    fillLog(history);
    // land on the last grid, or on Output if something failed or nothing returned rows
    setCurrentIndex(failed >= 0 || grids == 0 ? count() - 1 : grids - 1);
    blockSignals(false);
    if (failed >= 0) log_->selectRow(failed);
}

void ResultsPanel::clearAll() {
    blockSignals(true);
    while (count() > 1) {
        QWidget* w = widget(0);
        removeTab(0);
        delete w;
    }
    log_->setRowCount(0);
    blockSignals(false);
}

void ResultsPanel::refreshTheme() {
    setTabIcon(count() - 1, icon("terminal", theme().text2, 16));
    if (history_) fillLog(*history_);
}
