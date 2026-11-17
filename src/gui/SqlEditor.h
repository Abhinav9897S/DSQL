#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include "ds/DynArray.h"

// Colours SQL keywords, types, functions, strings, numbers and comments.
// Works line by line; the block state remembers if a /* comment */ is still open.
class SqlHighlighter : public QSyntaxHighlighter {
public:
    explicit SqlHighlighter(QTextDocument* doc);

protected:
    void highlightBlock(const QString& text) override;
};

// [start, end) character range of one statement
struct TextRange {
    int start = 0;
    int end = 0;
};

// splits on ';' but not inside strings or comments
DynArray<TextRange> splitStatements(const QString& text);

// true if there is something besides whitespace and comments
bool hasSql(const QString& text);

// The SQL editor: line numbers, current-line highlight, a red mark on the
// line of the last error, and a few IDE keys (Tab, auto-indent, Ctrl+/).
class SqlEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SqlEditor(QWidget* parent = nullptr);

    QString filePath;   // empty until the script is saved

    void markError(int line, const QString& message);
    void clearError();
    void flash(int start, int end);
    bool statementAtCursor(int& start, int& end) const;
    void appendSnippet(const QString& sql);
    void refreshTheme();

    int gutterWidth() const;
    void paintGutter(QPaintEvent* e);

signals:
    void runRequested(bool all);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void updateGutter(const QRect& rect, int dy);
    void updateHighlights();
    void toggleComment();
    void outdent();

    QWidget* gutter_;
    SqlHighlighter* highlighter_;
    int errorLine_ = 0;
    int flashStart_ = -1;
    int flashEnd_ = -1;
};
