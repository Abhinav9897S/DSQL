#pragma once
#include <QWidget>
#include <QFrame>
#include "ds/DynArray.h"

class QScrollArea;
class QToolButton;

// First screen, like an IDE's start page: create / open a folder, start an
// in-memory session, reopen a recent folder, or read about the structures.
class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
    void setRecent(const DynArray<QString>& folders);
    void refreshTheme();

signals:
    void newFolder();
    void openFolder();
    void memorySession();
    void openRecent(const QString& path);
    void forgetRecent(const QString& path);
    void toggleTheme();
    void showDs(const QString& key);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void rebuild();
    QScrollArea* scroll_;
    QToolButton* themeButton_;
    DynArray<QString> recent_;
};

// a QFrame that reports clicks
class ClickFrame : public QFrame {
    Q_OBJECT
public:
    using QFrame::QFrame;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }
};
