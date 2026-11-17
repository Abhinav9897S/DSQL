#pragma once
#include <QMainWindow>
#include "ds/DynArray.h"
#include "storage/Catalog.h"
#include "sql/Executor.h"

class QStackedWidget;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class QLabel;
class QAction;
class QCheckBox;
class QTimer;
class CodeEditor;
class Terminal;

// The whole app in one window, with two pages:
//   start page - new folder, open folder, in-memory session, recent folders
//   work page  - tables | SQL editor + terminal/result/problems | how the last query ran
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Catalog& cat, Executor& exec, QWidget* parent = nullptr);
    void openFolder(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildStartPage();
    QWidget* buildWorkPage();
    void buildMenus();

    // folders
    void newFolder();
    void openFolderDialog();
    void openMemory();
    void closeFolder();
    void showWorkPage();
    void showStartPage();

    // the script in the editor
    void setScript(const QString& text, const QString& path);
    void loadScript(const QString& path);
    void openScriptDialog();
    bool saveScript();
    bool maybeSave();
    void updateScriptLabel();
    void loadSample();

    // checking and running
    void checkNow();
    void run(bool all);
    void runSql(const QString& sql, int firstLine, bool fromEditor);
    void showResult(const Result& r);
    void explain(const Result& r);
    void refreshTables();
    void tableDoubleClicked(QTreeWidgetItem* item);

    void setDarkMode(bool on);
    void rememberFolder(const QString& path);
    void refreshRecent();

    Catalog& cat_;
    Executor& exec_;

    QStackedWidget* pages_;
    QListWidget* recentList_;
    QTreeWidget* tables_;
    CodeEditor* editor_;
    QLabel* scriptLabel_;
    QLabel* problemsLabel_;
    QCheckBox* darkBox_;
    QTabWidget* output_;
    Terminal* terminal_;
    QTableWidget* resultTable_;
    QListWidget* problemsList_;
    QTextBrowser* explain_;
    QLabel* cursorLabel_;
    QTimer* checkTimer_;

    QAction* closeFolderAct_;
    QAction* openScriptAct_;
    QAction* saveScriptAct_;
    QAction* runAllAct_;
    QAction* runStatementAct_;
    QAction* sampleAct_;
    QAction* upperAct_;
    QAction* darkAct_;

    QString scriptPath_;          // file the editor is saved to (may not exist yet)
    DynArray<QString> recent_;    // recently opened folders, newest first
};
