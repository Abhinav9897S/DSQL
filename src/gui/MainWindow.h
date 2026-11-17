#pragma once
#include <QMainWindow>
#include "ds/DynArray.h"
#include "ds/HashTable.h"
#include "storage/Catalog.h"
#include "sql/Executor.h"
#include "gui/ResultsPanel.h"

class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QDockWidget;
class QLabel;
class QAction;
class QToolBar;
class QSplitter;
class WelcomePage;
class SqlEditor;
class ExplainView;
class StructuresView;
class LearnView;

// The workbench window. The welcome page and the workbench share it; the
// navigator and the data structure panel are dock widgets you can move,
// close or float like in any IDE.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Catalog& cat, Executor& exec, QWidget* parent = nullptr);
    void openFolder(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildWorkbench();
    void buildStatusBar();
    void refreshIcons();

    // workspace
    void newFolder();
    void openFolderDialog();
    void openMemory();
    bool closeFolder();
    void enterWorkbench();
    void showWelcome();
    void setWorkspaceActions(bool on);

    // editor tabs
    SqlEditor* currentEditor() const;
    SqlEditor* newScript(const QString& text = QString(), const QString& path = QString(), const QString& title = QString());
    void openScriptFile(const QString& path);
    void openScriptDialog();
    bool saveEditor(SqlEditor* ed, bool saveAs);
    bool closeTab(int index);
    bool confirmDiscard();
    void updateTabTitle(SqlEditor* ed);
    void openSample();

    // running SQL
    void run(bool all);
    void runSql(const QString& sql, int firstLine, SqlEditor* ed);

    // panels
    void refreshNavigator();
    void refreshStatus();
    void showEntry(int historyIndex);
    void showLearn(const QString& key);
    void showStructures(const QString& table);
    void navigatorMenu(const QPoint& pos);
    void navigatorDoubleClick(QTreeWidgetItem* item);

    // misc
    void toggleTheme();
    void loadSettings();
    void rememberFolder(const QString& path);
    void forgetFolder(const QString& path);
    QString startDir() const;
    void showShortcuts();
    void showAbout();
    void showDsDialog(const QString& key);

    Catalog& cat_;
    Executor& exec_;

    QStackedWidget* stack_;
    WelcomePage* welcome_;
    QWidget* bench_;
    QTabWidget* editors_;
    ResultsPanel* results_;
    QSplitter* split_;
    QTreeWidget* nav_;
    QDockWidget* navDock_;
    QDockWidget* dsDock_;
    QTabWidget* dsTabs_;
    ExplainView* explain_;
    StructuresView* structures_;
    LearnView* learn_;
    QToolBar* toolbar_;
    QLabel* chipOrder_;
    QLabel* chipPool_;
    QLabel* sbWorkspace_;
    QLabel* sbTables_;
    QLabel* sbPool_;
    QLabel* sbLast_;
    QLabel* sbCursor_;

    QAction* actNewScript_;
    QAction* actOpenScript_;
    QAction* actSave_;
    QAction* actSaveAs_;
    QAction* actNewFolder_;
    QAction* actOpenFolder_;
    QAction* actCloseFolder_;
    QAction* actRunAll_;
    QAction* actRunStatement_;
    QAction* actSample_;
    QAction* actClearOutput_;
    QAction* actTheme_;

    DynArray<RunEntry> history_;   // every statement run in this workspace
    DynArray<QString> recent_;
    HashTable<Str, bool> expanded_; // navigator tables left open
    int runCounter_ = 0;
    int untitled_ = 0;
};
