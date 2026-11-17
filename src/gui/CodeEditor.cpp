#include "gui/CodeEditor.h"
#include "gui/QtStr.h"
#include "ds/HashTable.h"
#include <QPainter>
#include <QTextBlock>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QFontDatabase>
#include <QMimeData>

// ---------------------------------------------------------------- colours

static EditorColors colors;

const EditorColors& editorColors() { return colors; }

void setEditorColorsDark(bool dark) {
    EditorColors& c = colors;
    if (dark) {   // close to VS Code's dark theme
        c.keyword = QColor("#569cd6");
        c.type = QColor("#4ec9b0");
        c.function = QColor("#dcdcaa");
        c.string = QColor("#ce9178");
        c.number = QColor("#b5cea8");
        c.comment = QColor("#6a9955");
        c.gutterBg = QColor("#1e1e1e");
        c.gutterText = QColor("#6e7681");
        c.gutterCurrent = QColor("#c6c6c6");
        c.currentLine = QColor("#2a2d2e");
        c.error = QColor("#f14c4c");
        c.errorBg = QColor("#3c1f1f");
        c.termPrompt = QColor("#569cd6");
        c.termOk = QColor("#4ec9b0");
        c.termError = QColor("#f14c4c");
    } else {
        c.keyword = QColor("#0033b3");
        c.type = QColor("#00627a");
        c.function = QColor("#795e26");
        c.string = QColor("#067d17");
        c.number = QColor("#1750eb");
        c.comment = QColor("#8c8c8c");
        c.gutterBg = QColor("#f3f3f3");
        c.gutterText = QColor("#9a9a9a");
        c.gutterCurrent = QColor("#333333");
        c.currentLine = QColor("#f2f6fd");
        c.error = QColor("#e51400");
        c.errorBg = QColor("#fdecec");
        c.termPrompt = QColor("#0033b3");
        c.termOk = QColor("#067d17");
        c.termError = QColor("#cd3131");
    }
}

// ---------------------------------------------------------------- SQL words

// the word lists live in our own hash table, like the engine's lexer
int sqlWordKind(const QString& word) {
    static HashTable<Str, int> words(128);
    if (words.size() == 0) {
        const char* lists[3] = {
            "SELECT FROM WHERE INSERT INTO VALUES CREATE TABLE INDEX ON DROP DELETE UPDATE SET AND OR NOT JOIN "
            "INNER PRIMARY KEY ORDER BY ASC DESC LIMIT GROUP AS BETWEEN LIKE TRUE FALSE SHOW TABLES DESCRIBE "
            "NULL IF EXISTS",
            "INT INTEGER BIGINT FLOAT REAL DOUBLE VARCHAR CHAR TEXT BOOL BOOLEAN",
            "COUNT SUM AVG MIN MAX",
        };
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
    return kind ? *kind : 0;
}

static bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == '_'; }

// i is on an opening quote; returns the index just past the closing one
static int skipString(const QString& text, int i) {
    int n = text.size();
    i++;
    while (i < n) {
        if (text[i] == '\'' && i + 1 < n && text[i + 1] == '\'') i += 2;   // '' is an escaped quote
        else if (text[i] == '\'') return i + 1;
        else i++;
    }
    return n;
}

// ---------------------------------------------------------------- highlighter

static QTextCharFormat charFormat(const QColor& c, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(c);
    if (bold) f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

void SqlHighlighter::highlightBlock(const QString& text) {
    const EditorColors& c = editorColors();
    int n = text.size();
    int i = 0;
    setCurrentBlockState(0);

    if (previousBlockState() == 1) {   // a /* comment */ from an earlier line is still open
        int close = text.indexOf("*/");
        int stop = close < 0 ? n : close + 2;
        setFormat(0, stop, charFormat(c.comment, false, true));
        if (close < 0) {
            setCurrentBlockState(1);
            return;
        }
        i = stop;
    }

    while (i < n) {
        QChar ch = text[i];
        if (ch == '-' && i + 1 < n && text[i + 1] == '-') {
            setFormat(i, n - i, charFormat(c.comment, false, true));
            return;
        }
        if (ch == '/' && i + 1 < n && text[i + 1] == '*') {
            int close = text.indexOf("*/", i + 2);
            int stop = close < 0 ? n : close + 2;
            setFormat(i, stop - i, charFormat(c.comment, false, true));
            if (close < 0) {
                setCurrentBlockState(1);
                return;
            }
            i = stop;
        } else if (ch == '\'') {
            int stop = skipString(text, i);
            setFormat(i, stop - i, charFormat(c.string));
            i = stop;
        } else if (ch.isDigit()) {
            int j = i;
            while (j < n && (text[j].isDigit() || text[j] == '.')) j++;
            setFormat(i, j - i, charFormat(c.number));
            i = j;
        } else if (isWordChar(ch)) {
            int j = i;
            while (j < n && isWordChar(text[j])) j++;
            int kind = sqlWordKind(text.mid(i, j - i));
            if (kind == 1) setFormat(i, j - i, charFormat(c.keyword, true));
            else if (kind == 2) setFormat(i, j - i, charFormat(c.type));
            else if (kind == 3) setFormat(i, j - i, charFormat(c.function));
            i = j;
        } else {
            i++;
        }
    }
}

// ---------------------------------------------------------------- editor

namespace {
class Gutter : public QWidget {
public:
    explicit Gutter(CodeEditor* e) : QWidget(e), editor_(e) {}
    QSize sizeHint() const override { return QSize(editor_->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* e) override { editor_->paintGutter(e); }
    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {   // hover a red line number to read the error
            QHelpEvent* h = static_cast<QHelpEvent*>(e);
            QTextBlock b = editor_->cursorForPosition(QPoint(0, h->pos().y())).block();
            QString msg = editor_->markAt(b.blockNumber() + 1);
            if (msg.isEmpty()) QToolTip::hideText();
            else QToolTip::showText(h->globalPos(), msg, this);
            return true;
        }
        return QWidget::event(e);
    }

private:
    CodeEditor* editor_;
};
}

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(11);
    setFont(f);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    gutter_ = new Gutter(this);
    highlighter_ = new SqlHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy) gutter_->scroll(0, dy);
        else gutter_->update(0, r.y(), gutter_->width(), r.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateHighlights);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    updateHighlights();
}

int CodeEditor::gutterWidth() const {
    int digits = 1;
    for (int n = qMax(1, blockCount()); n >= 10; n /= 10) digits++;
    return 20 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    QRect r = contentsRect();
    gutter_->setGeometry(QRect(r.left(), r.top(), gutterWidth(), r.height()));
}

QString CodeEditor::markAt(int line) const {
    QString msg;
    for (int i = 0; i < marks_.size(); i++)
        if (marks_[i].line == line) msg += (msg.isEmpty() ? "" : "\n") + marks_[i].message;
    return msg;
}

void CodeEditor::paintGutter(QPaintEvent* e) {
    const EditorColors& c = editorColors();
    QPainter p(gutter_);
    p.fillRect(e->rect(), c.gutterBg);
    p.setFont(font());
    int current = textCursor().blockNumber() + 1;

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= e->rect().bottom()) {
        int h = qRound(blockBoundingRect(block).height());
        int line = block.blockNumber() + 1;
        if (block.isVisible() && top + h >= e->rect().top()) {
            bool bad = !markAt(line).isEmpty();
            if (bad) {   // red dot + red number on lines with a problem
                p.setPen(Qt::NoPen);
                p.setBrush(c.error);
                p.drawEllipse(QPointF(7, top + h / 2.0), 3, 3);
            }
            p.setPen(bad ? c.error : line == current ? c.gutterCurrent : c.gutterText);
            p.drawText(0, top, gutter_->width() - 8, h, Qt::AlignRight | Qt::AlignVCenter, QString::number(line));
        }
        block = block.next();
        top += h;
    }
}

void CodeEditor::updateHighlights() {
    const EditorColors& c = editorColors();
    QList<QTextEdit::ExtraSelection> list;   // Qt's API takes its own list type here

    QTextEdit::ExtraSelection cur;
    cur.format.setBackground(c.currentLine);
    cur.format.setProperty(QTextFormat::FullWidthSelection, true);
    cur.cursor = textCursor();
    cur.cursor.clearSelection();
    list.append(cur);

    for (int i = 0; i < marks_.size(); i++) {
        const EditorMark& m = marks_[i];
        QTextBlock b = document()->findBlockByNumber(m.line - 1);
        if (!b.isValid()) continue;

        QTextEdit::ExtraSelection line;   // faint red behind the whole line
        line.format.setBackground(c.errorBg);
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = QTextCursor(b);
        list.append(line);

        // red squiggle under the exact spot (or the whole line)
        int start = m.length > 0 ? qMin(m.col - 1, b.length() - 1) : 0;
        int len = m.length > 0 ? m.length : b.length() - 1;
        if (start + len > b.length() - 1) len = b.length() - 1 - start;
        if (len <= 0) {   // e.g. "missing ;" at the very end: mark the last character
            start = qMax(0, b.length() - 2);
            len = b.length() > 1 ? 1 : 0;
        }
        if (len > 0) {
            QTextEdit::ExtraSelection sq;
            sq.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            sq.format.setUnderlineColor(c.error);
            sq.cursor = QTextCursor(b);
            sq.cursor.setPosition(b.position() + start);
            sq.cursor.setPosition(b.position() + start + len, QTextCursor::KeepAnchor);
            list.append(sq);
        }
    }
    setExtraSelections(list);
    gutter_->update();
}

void CodeEditor::setMarks(const DynArray<EditorMark>& marks) {
    marks_ = marks;
    updateHighlights();
}

void CodeEditor::addMark(const EditorMark& mark) {
    marks_.push(mark);
    updateHighlights();
}

void CodeEditor::refreshColors() {
    highlighter_->rehighlight();
    updateHighlights();
}

void CodeEditor::goToLine(int line) {
    QTextBlock b = document()->findBlockByNumber(line - 1);
    if (!b.isValid()) return;
    setTextCursor(QTextCursor(b));
    centerCursor();
    setFocus();
}

bool CodeEditor::viewportEvent(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {   // hover an underlined spot to read the error
        QHelpEvent* h = static_cast<QHelpEvent*>(e);
        QString msg = markAt(cursorForPosition(h->pos()).blockNumber() + 1);
        if (msg.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(h->globalPos(), msg, viewport());
        return true;
    }
    return QPlainTextEdit::viewportEvent(e);
}

// true if `column` of this line is plain SQL, not inside a string or a comment
bool CodeEditor::isCode(const QTextBlock& block, int column) const {
    QString text = block.text();
    bool inComment = block.previous().isValid() && block.previous().userState() == 1;
    bool inString = false;
    for (int i = 0; i < column && i < text.size(); i++) {
        QChar ch = text[i];
        if (inComment) {
            if (ch == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                inComment = false;
                i++;
            }
        } else if (inString) {
            if (ch == '\'') inString = false;   // '' just toggles twice
        } else if (ch == '\'') {
            inString = true;
        } else if (ch == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            return false;   // the rest of the line is a comment
        } else if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            inComment = true;
            i++;
        }
    }
    return !inComment && !inString;
}

// "select" + space  ->  "SELECT "
void CodeEditor::capitalizeWordBeforeCursor() {
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) return;
    QTextBlock block = cursor.block();
    QString text = block.text();
    int end = cursor.positionInBlock();
    int start = end;
    while (start > 0 && isWordChar(text[start - 1])) start--;
    if (start == end || text[start].isDigit()) return;
    if (start > 0 && text[start - 1] == '.') return;   // table.column stays as typed
    QString word = text.mid(start, end - start);
    if (!sqlWordKind(word) || word == word.toUpper() || !isCode(block, start)) return;

    QTextCursor w(block);
    w.setPosition(block.position() + start);
    w.setPosition(block.position() + end, QTextCursor::KeepAnchor);
    w.insertText(word.toUpper());
}

// lowercase SQL words outside strings and comments, as (position, NEW TEXT)
struct WordFix {
    int start = 0;
    QString word;
};

static DynArray<WordFix> findWordsToCapitalise(const QString& text) {
    DynArray<WordFix> fixes;
    int n = text.size();
    int i = 0;
    while (i < n) {
        QChar ch = text[i];
        if (ch == '-' && i + 1 < n && text[i + 1] == '-') {
            while (i < n && text[i] != '\n') i++;
        } else if (ch == '/' && i + 1 < n && text[i + 1] == '*') {
            int close = text.indexOf("*/", i + 2);
            i = close < 0 ? n : close + 2;
        } else if (ch == '\'') {
            i = skipString(text, i);
        } else if (isWordChar(ch) && !ch.isDigit()) {
            int j = i;
            while (j < n && isWordChar(text[j])) j++;
            QString word = text.mid(i, j - i);
            bool afterDot = i > 0 && text[i - 1] == '.';   // table.column stays as typed
            if (!afterDot && sqlWordKind(word) && word != word.toUpper()) fixes.push(WordFix{i, word.toUpper()});
            i = j;
        } else {
            i++;
        }
    }
    return fixes;
}

QString capitaliseSql(const QString& text) {
    QString out = text;
    DynArray<WordFix> fixes = findWordsToCapitalise(text);
    for (int k = 0; k < fixes.size(); k++) out.replace(fixes[k].start, fixes[k].word.size(), fixes[k].word);
    return out;
}

void CodeEditor::uppercaseKeywords() {
    DynArray<WordFix> fixes = findWordsToCapitalise(toPlainText());
    if (fixes.empty()) return;
    QTextCursor c(document());
    c.beginEditBlock();   // one undo step for the lot
    for (int k = fixes.size() - 1; k >= 0; k--) {
        c.setPosition(fixes[k].start);
        c.setPosition(fixes[k].start + fixes[k].word.size(), QTextCursor::KeepAnchor);
        c.insertText(fixes[k].word);
    }
    c.endEditBlock();
}

void CodeEditor::insertFromMimeData(const QMimeData* source) {
    QPlainTextEdit::insertFromMimeData(source);
    uppercaseKeywords();   // pasted SQL gets capitalised too
}

// ---------------------------------------------------------------- indentation and brackets

static const int INDENT = 4;   // spaces per level

static int leadingSpaces(const QString& line) {
    int n = 0;
    while (n < line.size() && line[n] == ' ') n++;
    return n;
}

// Enter keeps the indentation; after "(" it goes one level deeper, and if the
// matching ")" is right there it moves to its own line:
//     CREATE TABLE t (|)   ->   CREATE TABLE t (
//                                   |
//                               )
void CodeEditor::newLineWithIndent() {
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    QString line = c.block().text();
    int pos = c.positionInBlock();
    QString indent(qMin(leadingSpaces(line), pos), ' ');

    int b = pos - 1;
    while (b >= 0 && line[b] == ' ') b--;   // look past spaces before the cursor
    bool afterOpen = b >= 0 && line[b] == '(';
    bool beforeClose = pos < line.size() && line[pos] == ')';

    if (afterOpen) {
        c.insertText("\n" + indent + QString(INDENT, ' '));
        if (beforeClose) {
            int inside = c.position();
            c.insertText("\n" + indent);
            c.setPosition(inside);
        }
    } else {
        c.insertText("\n" + indent);
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
}

// Tab / Shift+Tab on a selection moves every selected line one level
void CodeEditor::indentLines(bool in) {
    QTextCursor c = textCursor();
    int start = c.selectionStart(), end = c.selectionEnd();
    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (c.hasSelection() && last.position() == end && end > start) last = last.previous();   // selection ends at a line start

    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QString text = b.text();
        if (in) {
            if (!text.trimmed().isEmpty()) {
                edit.setPosition(b.position());
                edit.insertText(QString(INDENT, ' '));
            }
        } else {
            int remove = qMin(leadingSpaces(text), INDENT);
            if (remove == 0 && text.startsWith('\t')) remove = 1;
            if (remove > 0) {
                edit.setPosition(b.position());
                edit.setPosition(b.position() + remove, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            }
        }
        if (b == last) break;
    }
    edit.endEditBlock();

    if (first != last) {   // keep the moved lines selected
        QTextCursor sel(document());
        sel.setPosition(first.position());
        sel.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        setTextCursor(sel);
    }
}

// Backspace removes an empty pair "(|)" / "'|'" together, and in the
// leading spaces of a line it goes back a whole indent level
bool CodeEditor::smartBackspace() {
    QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    QString line = c.block().text();
    int pos = c.positionInBlock();
    if (pos == 0) return false;

    if (pos < line.size()) {
        QChar a = line[pos - 1], b = line[pos];
        if ((a == '(' && b == ')') || (a == '\'' && b == '\'')) {
            c.beginEditBlock();
            c.deleteChar();
            c.deletePreviousChar();
            c.endEditBlock();
            return true;
        }
    }
    if (line[pos - 1] == ' ' && line.left(pos).trimmed().isEmpty()) {
        int remove = pos % INDENT == 0 ? INDENT : pos % INDENT;
        c.setPosition(c.position() - remove, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        return true;
    }
    return false;
}

// "(" -> "()" and "'" -> "''" with the cursor in between; with a selection,
// the selection gets wrapped instead
bool CodeEditor::openPair(QChar open) {
    QChar close = open == '(' ? ')' : '\'';
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        QString inner = c.selectedText().replace(QChar(0x2029), '\n');   // Qt marks line breaks with U+2029 here
        c.insertText(open + inner + close);
        setTextCursor(c);
        return true;
    }
    QTextBlock block = c.block();
    QString line = block.text();
    int pos = c.positionInBlock();
    if (!isCode(block, pos)) return false;   // inside a string or comment: type it as is

    // only pair up when nothing is glued to the right, e.g. not in front of a word
    QChar next = pos < line.size() ? line[pos] : QChar(' ');
    if (!next.isSpace() && !QString("),;").contains(next)) return false;
    if (open == '\'' && pos > 0 && (isWordChar(line[pos - 1]) || line[pos - 1] == '\'')) return false;

    c.insertText(QString(open) + close);
    c.movePosition(QTextCursor::Left);
    setTextCursor(c);
    return true;
}

// typing ")" or "'" right in front of the same character just steps over it
bool CodeEditor::skipClosing(QChar close) {
    QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    QString line = c.block().text();
    int pos = c.positionInBlock();
    if (pos >= line.size() || line[pos] != close) return false;
    if (close == '\'' && isCode(c.block(), pos)) return false;   // only when it ends the string we're in
    c.movePosition(QTextCursor::Right);
    setTextCursor(c);
    return true;
}

// ")" typed on an empty line lines up with the line holding its "("
bool CodeEditor::dedentClosingParen() {
    QTextCursor c = textCursor();
    QString before = c.block().text().left(c.positionInBlock());
    if (before.isEmpty() || !before.trimmed().isEmpty()) return false;

    QString text = toPlainText();
    int depth = 0;
    for (int i = c.position() - 1; i >= 0; i--) {
        if (text[i] == ')') {
            depth++;
        } else if (text[i] == '(') {
            if (depth > 0) {
                depth--;
                continue;
            }
            QString open = document()->findBlock(i).text();   // found the matching "("
            c.beginEditBlock();
            c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            c.insertText(QString(leadingSpaces(open), ' ') + ")");
            c.endEditBlock();
            setTextCursor(c);
            return true;
        }
    }
    return false;
}

void CodeEditor::keyPressEvent(QKeyEvent* e) {
    bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
    bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    QString t = e->text();
    QChar ch = t.size() == 1 ? t[0] : QChar();

    // a finished word gets checked when you type what comes after it
    if (plain && (enter || e->key() == Qt::Key_Tab || (!ch.isNull() && QString(" (),;=<>+-*/%").contains(ch))))
        capitalizeWordBeforeCursor();

    if (e->key() == Qt::Key_Backtab) {   // Shift+Tab
        indentLines(false);
        return;
    }
    if (e->key() == Qt::Key_Tab && plain) {
        QTextCursor c = textCursor();
        if (c.hasSelection() && document()->findBlock(c.selectionStart()) != document()->findBlock(c.selectionEnd()))
            indentLines(true);
        else
            insertPlainText(QString(INDENT - c.positionInBlock() % INDENT, ' '));   // to the next tab stop
        return;
    }
    if (enter && plain && !(e->modifiers() & Qt::ShiftModifier)) {
        newLineWithIndent();
        return;
    }
    if (plain && e->key() == Qt::Key_Backspace && smartBackspace()) return;
    if (plain && ch == ')' && (skipClosing(ch) || dedentClosingParen())) return;
    if (plain && ch == '\'' && (skipClosing(ch) || openPair(ch))) return;
    if (plain && ch == '(' && openPair(ch)) return;
    QPlainTextEdit::keyPressEvent(e);
}
