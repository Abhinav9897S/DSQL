#pragma once
#include <QTextBrowser>

// The "Learn" tab: a card per data structure (how it works, its costs,
// where it lives in the code) with a query you can drop into the editor.
class LearnView : public QTextBrowser {
    Q_OBJECT
public:
    explicit LearnView(QWidget* parent = nullptr);
    void rebuild();
    void focusOn(const QString& key);

signals:
    void insertSnippet(const QString& sql);
};

// the same card as HTML, also used by the welcome page's pop-up
QString dsCardHtml(const char* key, bool withTry);
