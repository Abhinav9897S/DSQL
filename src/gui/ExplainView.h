#pragma once
#include <QScrollArea>
#include <QFrame>
#include "sql/Executor.h"

class QLabel;

// The "Explain" tab: for one statement it shows the pipeline it went
// through, which data structures it used (with their counters), the
// step-by-step trace, the token queue and the raw counters.
class ExplainView : public QScrollArea {
    Q_OBJECT
public:
    explicit ExplainView(QWidget* parent = nullptr);
    void showResult(const Result& r);
    void showIntro();
    void refreshTheme();

signals:
    void learnRequested(const QString& key);

private:
    void rebuild();
    Result shown_;
    bool has_ = false;
};

// Boxes-and-arrows picture of the engine; stages the statement touched light up.
class PipelineWidget : public QWidget {
public:
    PipelineWidget(const Result* r, QWidget* parent);
    QSize sizeHint() const override { return QSize(300, 372); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    DsStats s_;
    bool ok_ = false;
    bool parsed_ = false;
    bool catalog_ = false;
    bool index_ = false;
    bool executes_ = false;
    bool disk_ = false;
    bool real_ = false;   // false for the intro picture
};

// A clickable coloured card naming one data structure.
class DsChip : public QFrame {
    Q_OBJECT
public:
    DsChip(const char* key, const QString& detail, QWidget* parent);

signals:
    void clicked(const QString& key);

protected:
    void mousePressEvent(QMouseEvent*) override;

private:
    QString key_;
};

QLabel* sectionTitle(const QString& text, QWidget* parent);
