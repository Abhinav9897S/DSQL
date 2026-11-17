#pragma once
#include <QWidget>
#include "ds/DynArray.h"

class QPlainTextEdit;
class QLineEdit;
class QLabel;

// The bottom "Terminal": every statement you run, its output as a text
// table, and errors in red. You can also type a statement at the
// minisql> prompt and press Enter. Up/Down walk through what you typed.
class Terminal : public QWidget {
    Q_OBJECT
public:
    explicit Terminal(QWidget* parent = nullptr);

    void printCommand(const QString& sql);
    void printOutput(const QString& text);
    void printSuccess(const QString& text);
    void printError(const QString& text);
    void printNote(const QString& text);
    void clear();
    void refreshColors();

signals:
    void commandEntered(const QString& sql);

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void write(const QString& text, const QColor& color, bool bold = false);
    void submit();

    QPlainTextEdit* out_;
    QLabel* prompt_;
    QLineEdit* input_;
    DynArray<QString> history_;
    int historyPos_ = 0;
};
