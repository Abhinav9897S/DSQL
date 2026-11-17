#pragma once
#include <QWidget>
#include "storage/Catalog.h"

class QComboBox;
class QScrollArea;

// The "Structures" tab: live pictures of one table's B+ tree, its AVL
// indexes and its page directory, plus the buffer pool's LRU list and the
// catalog's hash buckets. Every picture is a snapshot taken in refresh(),
// so nothing here points back into the engine afterwards.
class StructuresView : public QWidget {
    Q_OBJECT
public:
    explicit StructuresView(Catalog& cat, QWidget* parent = nullptr);
    void refresh();
    void selectTable(const QString& name);

private:
    Catalog& cat_;
    QComboBox* tables_;
    QScrollArea* scroll_;
};
