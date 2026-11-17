#include "gui/SqlEditor.h"
#include "gui/Theme.h"
#include "gui/QtStr.h"
#include "ds/HashTable.h"
#include <QPainter>
#include <QTextBlock>
#include <QKeyEvent>
#include <QTimer>
#include <QScrollBar>

// ---------------------------------------------------------------- highlighter

enum WordKind { Plain, Keyword, TypeName, Function };

// keyword lookup goes through our own hash table, like the engine's lexer
static int wordKind(const QString& word) {
    static HashTable<Str, int> words(128);
    if (words.size() == 0) {
        const char* kw = "SELECT FROM WHERE INSERT INTO VALUES CREATE TABLE INDEX ON DROP DELETE UPDATE SET AND OR "
                         "NOT JOIN INNER PRIMARY KEY ORDER BY ASC DESC LIMIT GROUP AS BETWEEN LIKE TRUE FALSE "
                         "SHOW TABLES DESCRIBE NULL IF EXISTS";
        const char* types = "INT INTEGER BIGINT FLOAT REAL DOUBLE VARCHAR CHAR TEXT BOOL BOOLEAN";
        const char* funcs = "COUNT SUM AVG MIN MAX";
        const char* lists[3] = {kw, types, funcs};
        for (int k = 0; k < 3; k++) {
            Str all(lists[k]);
            int i = 0;
            while (i < all.size()) {
                int sp = all.find(' ', i);
                if (sp < 0) sp = all.size();
                words.put(all.substr(i, sp - i), k + 1);
                i = sp + 1;
            }
        }
    }
    int* kind = words.find(toStr(word.toUpper()));
    return kind ? *kind : Plain;
}

SqlHighlighter::SqlHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

static QTextCharFormat fmt(const QColor& c, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(c);
    if (bold) f.setFontWeight(QFont::DemiBold);
    if (italic) f.setFontItalic(true);
    return f;
}

void SqlHighlighter::highlightBlock(const QString& text) {
    const ThemeColors& t = theme();
    int n = text.size();
    int i = 0;
    setCurrentBlockState(0);

    if (previousBlockState() == 1) {   // still inside /* ... */
        int close = text.indexOf("*/");
        int stop = close < 0 ? n : close + 2;
        setFormat(0, stop, fmt(t.comment, false, true));
        if (close < 0) {
            setCurrentBlockState(1);
            return;
        }
        i = stop;
    }

    while (i < n) {
        QChar c = text[i];
        if (c == '-' && i + 1 < n && text[i + 1] == '-') {
            setFormat(i, n - i, fmt(t.comment, false, true));
            return;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            int close = text.indexOf("*/", i + 2);
            int stop = close < 0 ? n : close + 2;
            setFormat(i, stop - i, fmt(t.comment, false, true));
            if (close < 0) {
                setCurrentBlockState(1);
                return;
            }
            i = stop;
        } else if (c == '\'') {
            int j = i + 1;
            while (j < n) {
                if (text[j] == '\'') {
                    if (j + 1 < n && text[j + 1] == '\'') {
                        j += 2;
                        continue;
                    }
                    break;
                }
                j++;
            }
            int stop = j < n ? j + 1 : n;
            setFormat(i, stop - i, fmt(t.str));
            i = stop;
        } else if (c.isDigit()) {
            int j = i;
            while (j < n && (text[j].isDigit() || text[j] == '.')) j++;
            setFormat(i, j - i, fmt(t.num));
            i = j;
        } else if (c.isLetter() || c == '_') {
            int j = i;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == '_')) j++;
            int kind = wordKind(text.mid(i, j - i));
            if (kind == Keyword) setFormat(i, j - i, fmt(t.kw, true));
            else if (kind == TypeName) setFormat(i, j - i, fmt(t.type));
            else if (kind == Function) setFormat(i, j - i, fmt(t.func));
            i = j;
        } else {
            if (!c.isSpace()) setFormat(i, 1, fmt(t.op));
            i++;
        }
    }
}

// ---------------------------------------------------------------- statements

DynArray<TextRange> splitStatements(const QString& text) {
    DynArray<TextRange> out;
    int start = 0;
    int i = 0;
    int n = text.size();
    while (i < n) {
        QChar c = text[i];
        if (c == '\'') {
            i++;
            while (i < n) {
                if (text[i] == '\'') {
                    if (i + 1 < n && text[i + 1] == '\'') {
                        i += 2;
                        continue;
                    }
                    break;
                }
                i++;
            }
        } else if (c == '-' && i + 1 < n && text[i + 1] == '-') {
            while (i < n && text[i] != '\n') i++;
            continue;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            int close = text.indexOf("*/", i + 2);
            i = close < 0 ? n : close + 2;
            continue;
        } else if (c == ';') {
            out.push(TextRange{start, i + 1});
            start = i + 1;
        }
        i++;
    }
    if (start < n) out.push(TextRange{start, n});
    return out;
}

bool hasSql(const QString& text) {
    int n = text.size();
    int i = 0;
    while (i < n) {
        if (text[i].isSpace() || text[i] == ';') {
            i++;
        } else if (text[i] == '-' && i + 1 < n && text[i + 1] == '-') {
            while (i < n && text[i] != '\n') i++;
        } else if (text[i] == '/' && i + 1 < n && text[i + 1] == '*') {
            int close = text.indexOf("*/", i + 2);
            i = close < 0 ? n : close + 2;
        } else {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- editor

namespace {
class Gutter : public QWidget {
public:
    explicit Gutter(SqlEditor* e) : QWidget(e), editor_(e) {}
    QSize sizeHint() const override { return QSize(editor_->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* e) override { editor_->paintGutter(e); }

private:
    SqlEditor* editor_;
};
}

SqlEditor::SqlEditor(QWidget* parent) : QPlainTextEdit(parent) {
    setObjectName("sqlEditor");
    setFont(monoFont(11));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 2);
    gutter_ = new Gutter(this);
    highlighter_ = new SqlHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, &SqlEditor::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &SqlEditor::updateHighlights);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    updateHighlights();
}

int SqlEditor::gutterWidth() const {
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10) digits++;
    return 22 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void SqlEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    QRect r = contentsRect();
    gutter_->setGeometry(QRect(r.left(), r.top(), gutterWidth(), r.height()));
}

void SqlEditor::updateGutter(const QRect& rect, int dy) {
    if (dy) gutter_->scroll(0, dy);
    else gutter_->update(0, rect.y(), gutter_->width(), rect.height());
}

void SqlEditor::paintGutter(QPaintEvent* e) {
    const ThemeColors& t = theme();
    QPainter p(gutter_);
    p.fillRect(e->rect(), t.editorBg);
    p.setPen(t.border);
    p.drawLine(gutter_->width() - 1, e->rect().top(), gutter_->width() - 1, e->rect().bottom());
    p.setFont(font());

    int current = textCursor().blockNumber() + 1;
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int lineH = qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= e->rect().bottom()) {
        int line = block.blockNumber() + 1;
        if (block.isVisible() && top + lineH >= e->rect().top()) {
            if (line == errorLine_) {
                p.fillRect(0, top, gutter_->width() - 1, lineH, mix(t.err, t.editorBg, 0.18));
                p.setPen(t.err);
            } else {
                p.setPen(line == current ? t.text2 : t.gutter);
            }
            p.drawText(0, top, gutter_->width() - 12, lineH, Qt::AlignRight | Qt::AlignVCenter, QString::number(line));
        }
        block = block.next();
        top += lineH;
    }
}

void SqlEditor::updateHighlights() {
    const ThemeColors& t = theme();
    // Qt's API takes its own list type here
    QList<QTextEdit::ExtraSelection> marks;

    QTextEdit::ExtraSelection cur;
    cur.format.setBackground(t.activeLine);
    cur.format.setProperty(QTextFormat::FullWidthSelection, true);
    cur.cursor = textCursor();
    cur.cursor.clearSelection();
    marks.append(cur);

    if (errorLine_ > 0) {
        QTextBlock b = document()->findBlockByNumber(errorLine_ - 1);
        if (b.isValid()) {
            QTextEdit::ExtraSelection err;
            err.format.setBackground(mix(t.err, t.editorBg, 0.14));
            err.format.setProperty(QTextFormat::FullWidthSelection, true);
            err.cursor = QTextCursor(b);
            marks.append(err);
        }
    }
    if (flashStart_ >= 0) {
        QTextEdit::ExtraSelection f;
        f.format.setBackground(mix(t.accent, t.editorBg, 0.16));
        f.cursor = QTextCursor(document());
        f.cursor.setPosition(flashStart_);
        f.cursor.setPosition(flashEnd_, QTextCursor::KeepAnchor);
        marks.append(f);
    }
    setExtraSelections(marks);
    gutter_->update();
}

void SqlEditor::markError(int line, const QString& message) {
    errorLine_ = line;
    QTextBlock b = document()->findBlockByNumber(line - 1);
    if (b.isValid()) {
        QTextCursor c(b);
        setTextCursor(c);
        centerCursor();
    }
    setToolTip(message);
    updateHighlights();
}

void SqlEditor::clearError() {
    errorLine_ = 0;
    setToolTip(QString());
    updateHighlights();
}

// tint the text that was just sent to the engine for a moment
void SqlEditor::flash(int start, int end) {
    flashStart_ = start;
    flashEnd_ = qMin(end, document()->characterCount() - 1);
    updateHighlights();
    QTimer::singleShot(450, this, [this] {
        flashStart_ = flashEnd_ = -1;
        updateHighlights();
    });
}

bool SqlEditor::statementAtCursor(int& start, int& end) const {
    QString text = toPlainText();
    int pos = textCursor().position();
    DynArray<TextRange> all = splitStatements(text);
    bool found = false;
    for (int i = 0; i < all.size(); i++) {
        const TextRange& r = all[i];
        if (!hasSql(text.mid(r.start, r.end - r.start))) continue;
        if (r.start <= pos || !found) {   // the one under the cursor, or the last one before it
            start = r.start;
            end = r.end;
            found = true;
        }
        if (pos >= r.start && pos <= r.end) break;
    }
    return found;
}

void SqlEditor::appendSnippet(const QString& sql) {
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    QString text = toPlainText();
    QString prefix = text.isEmpty() ? "" : (text.endsWith('\n') ? "\n" : "\n\n");
    c.insertText(prefix + sql);
    setTextCursor(c);
    ensureCursorVisible();
    setFocus();
}

void SqlEditor::refreshTheme() {
    highlighter_->rehighlight();
    updateHighlights();
    viewport()->update();
}

void SqlEditor::keyPressEvent(QKeyEvent* e) {
    bool ctrl = e->modifiers() & Qt::ControlModifier;
    bool shift = e->modifiers() & Qt::ShiftModifier;
    bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;

    if (ctrl && enter) {
        emit runRequested(shift);
        return;
    }
    if (ctrl && e->key() == Qt::Key_Slash) {
        toggleComment();
        return;
    }
    if (e->key() == Qt::Key_Backtab) {
        outdent();
        return;
    }
    if (e->key() == Qt::Key_Tab && !ctrl) {
        insertPlainText("  ");
        return;
    }
    if (enter && !shift && !(e->modifiers() & Qt::AltModifier)) {
        // keep the indentation of the current line
        QString line = textCursor().block().text();
        int n = 0;
        while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) n++;
        insertPlainText("\n" + line.left(n));
        ensureCursorVisible();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

void SqlEditor::outdent() {
    QTextCursor c = textCursor();
    QString line = c.block().text();
    int spaces = line.startsWith("  ") ? 2 : line.startsWith(' ') ? 1 : 0;
    if (!spaces) return;
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
    c.removeSelectedText();
}

void SqlEditor::toggleComment() {
    QTextCursor c = textCursor();
    QTextBlock first = document()->findBlock(c.selectionStart());
    QTextBlock last = document()->findBlock(c.hasSelection() ? c.selectionEnd() - 1 : c.selectionEnd());

    bool allCommented = true;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QString t = b.text().trimmed();
        if (!t.isEmpty() && !t.startsWith("--")) allCommented = false;
        if (b == last) break;
    }

    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QString t = b.text();
        int lead = 0;
        while (lead < t.size() && t[lead].isSpace()) lead++;
        if (lead < t.size()) {
            edit.setPosition(b.position() + lead);
            if (allCommented) {
                int len = t.mid(lead).startsWith("-- ") ? 3 : 2;
                edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, len);
                edit.removeSelectedText();
            } else {
                edit.insertText("-- ");
            }
        }
        if (b == last) break;
    }
    edit.endEditBlock();
}
