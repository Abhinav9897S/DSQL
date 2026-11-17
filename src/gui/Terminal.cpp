#include "gui/Terminal.h"
#include "gui/CodeEditor.h"
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QFontDatabase>
#include <QApplication>
#include <QPalette>

Terminal::Terminal(QWidget* parent) : QWidget(parent) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QVBoxLayout* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    out_ = new QPlainTextEdit(this);
    out_->setReadOnly(true);
    out_->setFont(mono);
    out_->setLineWrapMode(QPlainTextEdit::NoWrap);
    out_->setMaximumBlockCount(5000);   // old lines fall off the top
    col->addWidget(out_, 1);

    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(4, 0, 4, 2);
    prompt_ = new QLabel("minisql>", this);
    prompt_->setFont(mono);
    input_ = new QLineEdit(this);
    input_->setFont(mono);
    input_->setPlaceholderText("type a statement and press Enter   (clear = clear the terminal)");
    input_->installEventFilter(this);
    connect(input_, &QLineEdit::returnPressed, this, &Terminal::submit);
    row->addWidget(prompt_);
    row->addWidget(input_, 1);
    col->addLayout(row);

    refreshColors();
    printNote("MiniSQL terminal. Output and errors from the editor show up here.\n");
}

// an invalid colour means "the normal text colour", which then follows dark mode
void Terminal::write(const QString& text, const QColor& color, bool bold) {
    QTextCursor c(out_->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat f;
    if (color.isValid()) f.setForeground(color);
    if (bold) f.setFontWeight(QFont::Bold);
    c.insertText(text, f);
    out_->verticalScrollBar()->setValue(out_->verticalScrollBar()->maximum());
}

void Terminal::printCommand(const QString& sql) {
    write("minisql> ", editorColors().termPrompt, true);
    write(sql.trimmed() + "\n", QColor(), true);
}

void Terminal::printOutput(const QString& text) { write(text, QColor()); }

void Terminal::printSuccess(const QString& text) { write(text + "\n\n", editorColors().termOk); }

void Terminal::printError(const QString& text) { write(text + "\n\n", editorColors().termError, true); }

void Terminal::printNote(const QString& text) { write(text, QColor(128, 128, 128)); }

void Terminal::clear() { out_->clear(); }

void Terminal::refreshColors() { prompt_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(editorColors().termPrompt.name())); }

void Terminal::submit() {
    QString text = capitaliseSql(input_->text().trimmed());
    if (text.isEmpty()) return;
    input_->clear();
    if (history_.empty() || history_.back() != text) history_.push(text);
    historyPos_ = history_.size();
    if (text.compare("clear", Qt::CaseInsensitive) == 0) {
        clear();
        return;
    }
    emit commandEntered(text);
}

// Up / Down in the prompt bring back earlier statements
bool Terminal::eventFilter(QObject* watched, QEvent* e) {
    if (watched == input_ && e->type() == QEvent::KeyPress && !history_.empty()) {
        int key = static_cast<QKeyEvent*>(e)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            historyPos_ += key == Qt::Key_Up ? -1 : 1;
            if (historyPos_ < 0) historyPos_ = 0;
            if (historyPos_ >= history_.size()) {
                historyPos_ = history_.size();
                input_->clear();
            } else {
                input_->setText(history_[historyPos_]);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, e);
}
