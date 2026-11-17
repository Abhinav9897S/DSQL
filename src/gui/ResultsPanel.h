#pragma once
#include <QTabWidget>
#include <QAbstractTableModel>
#include "sql/Executor.h"

class QTableWidget;

// One executed statement as the GUI remembers it.
struct RunEntry {
    Result result;
    int number = 0;
    QString time;
};

// Feeds a result set to a QTableView straight from our DynArray<Row>,
// so even 10,000 rows don't need 10,000 widgets.
class ResultModel : public QAbstractTableModel {
public:
    ResultModel(const Result& r, QObject* parent);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    DynArray<Str> columns_;
    DynArray<Type> types_;
    DynArray<Row> rows_;
};

// Bottom panel: a grid per result set of the last run, and an Output tab
// that logs every statement ever run (click one to explain it).
class ResultsPanel : public QTabWidget {
    Q_OBJECT
public:
    explicit ResultsPanel(QWidget* parent = nullptr);
    void showRun(const DynArray<RunEntry>& history, int firstOfRun);
    void clearAll();
    void refreshTheme();

signals:
    void entrySelected(int historyIndex);

private:
    void fillLog(const DynArray<RunEntry>& history);
    QTableWidget* log_;
    const DynArray<RunEntry>* history_ = nullptr;
};
