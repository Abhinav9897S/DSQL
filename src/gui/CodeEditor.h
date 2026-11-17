#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QColor>
#include "ds/DynArray.h"

// Colours for the editor and the terminal, one set per theme.
struct EditorColors {
    QColor keyword, type, function, string, number, comment;
    QColor gutterBg, gutterText, gutterCurrent, currentLine;
    QColor error, errorBg;
    QColor termPrompt, termOk, termError;
};

const EditorColors& editorColors();
void setEditorColorsDark(bool dark);

// 0 = not SQL, 1 = keyword, 2 = type name, 3 = function
int sqlWordKind(const QString& word);

// "select * from t" -> "SELECT * FROM t" (strings and comments left alone)
QString capitaliseSql(const QString& text);

// Colours keywords, types, functions, strings, numbers and comments.
// The block state remembers whether a /* comment */ is still open.
class SqlHighlighter : public QSyntaxHighlighter {
public:
    explicit SqlHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

protected:
    void highlightBlock(const QString& text) override;
};

// A problem to show in the editor: red squiggle + red line number + tooltip.
struct EditorMark {
    int line = 1;
    int col = 1;
    int length = 0;   // 0 = the whole line
    QString message;
};

// The SQL editor: line numbers, syntax colours, current-line highlight,
// error marks, and SQL words turned to capitals as you type them.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setMarks(const DynArray<EditorMark>& marks);
    void addMark(const EditorMark& mark);
    void uppercaseKeywords();   // the whole script, e.g. after a paste
    void refreshColors();
    void goToLine(int line);

    int gutterWidth() const;
    void paintGutter(QPaintEvent* e);
    QString markAt(int line) const;

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void insertFromMimeData(const QMimeData* source) override;
    bool viewportEvent(QEvent* e) override;

private:
    void capitalizeWordBeforeCursor();
    bool isCode(const QTextBlock& block, int column) const;

    // indentation and bracket helpers for keyPressEvent
    void newLineWithIndent();
    void indentLines(bool in);
    bool smartBackspace();
    bool openPair(QChar open);
    bool skipClosing(QChar close);
    bool dedentClosingParen();
    void updateHighlights();

    QWidget* gutter_;
    SqlHighlighter* highlighter_;
    DynArray<EditorMark> marks_;
};
