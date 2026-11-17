#include "gui/MainWindow.h"
#include "gui/WelcomePage.h"
#include "gui/SqlEditor.h"
#include "gui/ExplainView.h"
#include "gui/StructuresView.h"
#include "gui/LearnView.h"
#include "gui/Theme.h"
#include "gui/QtStr.h"
#include "util/Error.h"
#include "util/Platform.h"
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QHeaderView>
#include <QDockWidget>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSettings>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTime>
#include <QTextBlock>

static const char* MEMORY_STARTER =
    "-- In-memory session: tables live in RAM and vanish when you close it.\n"
    "-- Query > Open Sample Script loads a complete example.\n"
    "\n"
    "CREATE TABLE notes (id INT PRIMARY KEY, title VARCHAR(40), stars INT);\n"
    "INSERT INTO notes VALUES (1, 'B+ trees are wide', 5), (2, 'AVL trees rotate', 4), (3, 'Hash tables chain', 4);\n"
    "SELECT * FROM notes WHERE id = 2;\n";

static QString readResource(const char* path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

MainWindow::MainWindow(Catalog& cat, Executor& exec, QWidget* parent)
    : QMainWindow(parent), cat_(cat), exec_(exec), expanded_(16) {
    setWindowTitle("MiniSQL Workbench");
    resize(1440, 880);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    buildActions();
    buildWorkbench();
    buildMenus();
    buildToolbar();
    buildStatusBar();
    refreshIcons();
    loadSettings();
    showWelcome();
}

// ---------------------------------------------------------------- building

void MainWindow::buildActions() {
    auto make = [this](const QString& text, const QKeySequence& key, void (MainWindow::*fn)()) {
        QAction* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    actNewScript_ = new QAction("New Script", this);
    actNewScript_->setShortcut(QKeySequence("Ctrl+N"));
    connect(actNewScript_, &QAction::triggered, this, [this] { newScript(); });
    actOpenScript_ = make("Open Script…", QKeySequence("Ctrl+O"), &MainWindow::openScriptDialog);
    actSave_ = new QAction("Save", this);
    actSave_->setShortcut(QKeySequence("Ctrl+S"));
    connect(actSave_, &QAction::triggered, this, [this] { saveEditor(currentEditor(), false); });
    actSaveAs_ = new QAction("Save As…", this);
    actSaveAs_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(actSaveAs_, &QAction::triggered, this, [this] { saveEditor(currentEditor(), true); });
    actNewFolder_ = make("New Folder…", QKeySequence(), &MainWindow::newFolder);
    actOpenFolder_ = make("Open Folder…", QKeySequence("Ctrl+Shift+O"), &MainWindow::openFolderDialog);
    actCloseFolder_ = new QAction("Close Folder", this);
    connect(actCloseFolder_, &QAction::triggered, this, [this] { closeFolder(); });

    actRunAll_ = new QAction("Run", this);
    actRunAll_->setToolTip("Run the whole script, or just the selection (Ctrl+Shift+Enter, F5)");
    actRunAll_->setShortcuts({QKeySequence("Ctrl+Shift+Return"), QKeySequence("F5")});   // Qt takes a list here
    connect(actRunAll_, &QAction::triggered, this, [this] { run(true); });
    actRunStatement_ = new QAction("Statement", this);
    actRunStatement_->setToolTip("Run the statement under the cursor (Ctrl+Enter)");
    actRunStatement_->setShortcut(QKeySequence("Ctrl+Return"));
    connect(actRunStatement_, &QAction::triggered, this, [this] { run(false); });
    actSample_ = make("Sample", QKeySequence(), &MainWindow::openSample);
    actSample_->setToolTip("Open the sample script in a new tab");
    actClearOutput_ = new QAction("Clear Output", this);
    connect(actClearOutput_, &QAction::triggered, this, [this] {
        history_.clear();
        results_->clearAll();
        explain_->showIntro();
    });
    actTheme_ = new QAction("Dark Mode", this);
    actTheme_->setCheckable(true);
    actTheme_->setChecked(theme().dark);
    actTheme_->setToolTip("Switch between dark and light");
    connect(actTheme_, &QAction::triggered, this, &MainWindow::toggleTheme);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(actNewScript_);
    file->addAction(actOpenScript_);
    file->addAction(actSave_);
    file->addAction(actSaveAs_);
    file->addSeparator();
    file->addAction(actNewFolder_);
    file->addAction(actOpenFolder_);
    file->addAction(actCloseFolder_);
    file->addSeparator();
    QAction* quit = file->addAction("Exit");
    quit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* query = menuBar()->addMenu("&Query");
    QAction* runAll = query->addAction("Run All / Selection");
    runAll->setShortcut(QKeySequence("Ctrl+Shift+Return"));
    runAll->setShortcutContext(Qt::WidgetShortcut);   // the toolbar action owns the real shortcut
    connect(runAll, &QAction::triggered, actRunAll_, &QAction::trigger);
    QAction* runOne = query->addAction("Run Statement at Cursor");
    runOne->setShortcut(QKeySequence("Ctrl+Return"));
    runOne->setShortcutContext(Qt::WidgetShortcut);
    connect(runOne, &QAction::triggered, actRunStatement_, &QAction::trigger);
    query->addSeparator();
    QAction* sample = query->addAction("Open Sample Script");
    connect(sample, &QAction::triggered, actSample_, &QAction::trigger);
    query->addAction(actClearOutput_);

    QMenu* view = menuBar()->addMenu("&View");
    QAction* navToggle = navDock_->toggleViewAction();
    navToggle->setText("Navigator");
    view->addAction(navToggle);
    QAction* dsToggle = dsDock_->toggleViewAction();
    dsToggle->setText("Data Structures Panel");
    view->addAction(dsToggle);
    view->addSeparator();
    view->addAction(actTheme_);

    QMenu* help = menuBar()->addMenu("&Help");
    connect(help->addAction("Keyboard Shortcuts"), &QAction::triggered, this, &MainWindow::showShortcuts);
    connect(help->addAction("Data Structure Guide"), &QAction::triggered, this, [this] { showLearn("bptree"); });
    help->addSeparator();
    connect(help->addAction("About MiniSQL"), &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildToolbar() {
    toolbar_ = addToolBar("Main");
    toolbar_->setObjectName("mainToolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(18, 18));
    toolbar_->addAction(actNewScript_);
    toolbar_->addAction(actOpenFolder_);
    toolbar_->addAction(actSave_);
    toolbar_->addSeparator();
    toolbar_->addAction(actRunAll_);
    toolbar_->addAction(actRunStatement_);
    toolbar_->addSeparator();
    toolbar_->addAction(actSample_);

    for (QAction* a : {actRunAll_, actRunStatement_, actSample_})
        if (QToolButton* b = qobject_cast<QToolButton*>(toolbar_->widgetForAction(a)))
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar_->widgetForAction(actRunAll_)->setObjectName("runButton");
    actNewScript_->setToolTip("New script (Ctrl+N)");
    actOpenFolder_->setToolTip("Open folder (Ctrl+Shift+O)");
    actSave_->setToolTip("Save (Ctrl+S)");

    QWidget* spacer = new QWidget(toolbar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    chipOrder_ = new QLabel(toolbar_);
    chipOrder_->setToolTip("B+ tree order (max children per node). Start with --order N to change it.");
    chipPool_ = new QLabel(toolbar_);
    chipPool_->setToolTip("Buffer pool frames. Start with --frames N to change it.");
    toolbar_->addWidget(chipOrder_);
    toolbar_->addWidget(chipPool_);
    toolbar_->addAction(actTheme_);
}

void MainWindow::buildWorkbench() {
    stack_ = new QStackedWidget(this);
    setCentralWidget(stack_);

    welcome_ = new WelcomePage(stack_);
    connect(welcome_, &WelcomePage::newFolder, this, &MainWindow::newFolder);
    connect(welcome_, &WelcomePage::openFolder, this, &MainWindow::openFolderDialog);
    connect(welcome_, &WelcomePage::memorySession, this, &MainWindow::openMemory);
    connect(welcome_, &WelcomePage::openRecent, this, &MainWindow::openFolder);
    connect(welcome_, &WelcomePage::forgetRecent, this, &MainWindow::forgetFolder);
    connect(welcome_, &WelcomePage::toggleTheme, this, &MainWindow::toggleTheme);
    connect(welcome_, &WelcomePage::showDs, this, &MainWindow::showDsDialog);
    stack_->addWidget(welcome_);

    // centre: editor tabs above, results below
    bench_ = new QWidget(stack_);
    QVBoxLayout* col = new QVBoxLayout(bench_);
    col->setContentsMargins(0, 0, 0, 0);
    QSplitter* split = new QSplitter(Qt::Vertical, bench_);
    split_ = split;
    split->setHandleWidth(3);
    split->setChildrenCollapsible(false);
    editors_ = new QTabWidget(split);
    editors_->setMinimumHeight(160);
    editors_->setDocumentMode(true);
    editors_->setMovable(true);
    QToolButton* plus = new QToolButton(editors_);
    plus->setText("+");
    plus->setToolTip("New script (Ctrl+N)");
    plus->setAutoRaise(true);
    connect(plus, &QToolButton::clicked, this, [this] { newScript(); });
    editors_->setCornerWidget(plus, Qt::TopRightCorner);
    connect(editors_, &QTabWidget::currentChanged, this, [this] {
        SqlEditor* ed = currentEditor();
        if (!ed) return;
        QTextCursor c = ed->textCursor();
        sbCursor_->setText(QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    });
    results_ = new ResultsPanel(split);
    results_->setMinimumHeight(110);
    connect(results_, &ResultsPanel::entrySelected, this, &MainWindow::showEntry);
    split->addWidget(editors_);
    split->addWidget(results_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    col->addWidget(split);
    stack_->addWidget(bench_);

    // left dock: navigator
    nav_ = new QTreeWidget();
    nav_->setColumnCount(2);
    nav_->setHeaderHidden(true);
    nav_->header()->setStretchLastSection(false);
    nav_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    nav_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    nav_->setIndentation(14);
    nav_->setExpandsOnDoubleClick(false);
    nav_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(nav_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::navigatorMenu);
    connect(nav_, &QTreeWidget::itemDoubleClicked, this, &MainWindow::navigatorDoubleClick);
    connect(nav_, &QTreeWidget::itemClicked, this, [](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).toString() == "table") item->setExpanded(!item->isExpanded());
    });
    connect(nav_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).toString() == "table") expanded_.put(toStr(item->text(0)), true);
    });
    connect(nav_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).toString() == "table") expanded_.remove(toStr(item->text(0)));
    });
    navDock_ = new QDockWidget("Navigator", this);
    navDock_->setObjectName("navigatorDock");
    navDock_->setWidget(nav_);
    navDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, navDock_);

    // right dock: the data structure panel
    dsTabs_ = new QTabWidget();
    dsTabs_->setDocumentMode(true);
    explain_ = new ExplainView(dsTabs_);
    structures_ = new StructuresView(cat_, dsTabs_);
    learn_ = new LearnView(dsTabs_);
    dsTabs_->addTab(explain_, "Explain");
    dsTabs_->addTab(structures_, "Structures");
    dsTabs_->addTab(learn_, "Learn");
    connect(explain_, &ExplainView::learnRequested, this, &MainWindow::showLearn);
    connect(learn_, &LearnView::insertSnippet, this, [this](const QString& sql) {
        SqlEditor* ed = currentEditor();
        if (!ed) return;
        ed->appendSnippet(sql);
        statusBar()->showMessage("Inserted at the end of the script. Press Ctrl+Enter to run it.", 5000);
    });
    connect(dsTabs_, &QTabWidget::currentChanged, this, [this] {
        if (dsTabs_->currentWidget() == structures_) structures_->refresh();
    });
    dsDock_ = new QDockWidget("Data Structures", this);
    dsDock_->setObjectName("dsDock");
    dsDock_->setWidget(dsTabs_);
    dsDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dsDock_);
    resizeDocks({navDock_, dsDock_}, {250, 430}, Qt::Horizontal);   // Qt takes lists here
}

void MainWindow::buildStatusBar() {
    sbWorkspace_ = new QLabel();
    sbTables_ = new QLabel();
    sbPool_ = new QLabel();
    sbLast_ = new QLabel();
    sbCursor_ = new QLabel("Ln 1, Col 1");
    statusBar()->addWidget(sbWorkspace_);
    statusBar()->addWidget(sbTables_);
    statusBar()->addWidget(sbPool_);
    statusBar()->addPermanentWidget(sbLast_);
    statusBar()->addPermanentWidget(sbCursor_);
}

void MainWindow::refreshIcons() {
    const ThemeColors& t = theme();
    actNewScript_->setIcon(icon("filePlus", t.text2));
    actOpenScript_->setIcon(icon("file", t.text2));
    actSave_->setIcon(icon("save", t.text2));
    actOpenFolder_->setIcon(icon("folder", t.text2));
    actNewFolder_->setIcon(icon("folderPlus", t.text2));
    actCloseFolder_->setIcon(icon("logout", t.text2));
    actRunAll_->setIcon(icon("zap", t.accentInk));
    actRunStatement_->setIcon(icon("play", t.text2));
    actSample_->setIcon(icon("book", t.text2));
    actTheme_->setIcon(icon(t.dark ? "sun" : "moon", t.text2));
    actTheme_->setChecked(t.dark);
    dsTabs_->setTabIcon(0, icon("activity", t.text2, 16));
    dsTabs_->setTabIcon(1, icon("layers", t.text2, 16));
    dsTabs_->setTabIcon(2, icon("book", t.text2, 16));
    QString chip = QString("QLabel { font-family: monospace; color: %1; border: 1px solid %2; border-radius: 9px; "
                           "padding: 2px 8px; margin-left: 4px; background: %3; }")
                       .arg(hex(t.text2), hex(t.border), hex(t.panel));
    chipOrder_->setStyleSheet(chip);
    chipPool_->setStyleSheet(chip);
    for (int i = 0; i < editors_->count(); i++) {
        editors_->setTabIcon(i, icon("file", t.warn, 16));
        if (QToolButton* x = qobject_cast<QToolButton*>(editors_->tabBar()->tabButton(i, QTabBar::RightSide)))
            x->setIcon(icon("x", t.muted, 14));
    }
}

// ---------------------------------------------------------------- workspace

QString MainWindow::startDir() const {
    if (!recent_.empty()) return QFileInfo(recent_[0]).absolutePath();
    return QDir::homePath();
}

void MainWindow::newFolder() {
    QString parent = QFileDialog::getExistingDirectory(this, "Where should the new workspace go?", startDir());
    if (parent.isEmpty()) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, "New folder", "Name of the new workspace folder:", QLineEdit::Normal,
                                         "my_database", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (name.contains('/') || name.contains('\\') || name == "." || name == "..") {
        QMessageBox::warning(this, "New folder", "Please use a plain folder name, without slashes.");
        return;
    }
    QDir dir(parent);
    if (dir.exists(name)) {
        QMessageBox::warning(this, "New folder", QString("%1 already exists. Pick another name, or use Open Folder.")
                                                     .arg(QDir::toNativeSeparators(dir.filePath(name))));
        return;
    }
    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, "New folder", "Couldn't create the folder there.");
        return;
    }
    QString path = dir.filePath(name);
    QFile starter(QDir(path).filePath("getting_started.sql"));
    if (starter.open(QIODevice::WriteOnly)) starter.write(readResource(":/examples/getting_started.sql").toUtf8());
    starter.close();
    openFolder(path);
}

void MainWindow::openFolderDialog() {
    QString path = QFileDialog::getExistingDirectory(this, "Open folder", startDir());
    if (!path.isEmpty()) openFolder(path);
}

void MainWindow::openFolder(const QString& path) {
    if (!confirmDiscard()) return;
    if (!QFileInfo(path).isDir()) {
        QMessageBox::warning(this, "Open folder", QString("%1 doesn't exist any more.").arg(QDir::toNativeSeparators(path)));
        forgetFolder(path);
        return;
    }
    try {
        cat_.openFolder(toPath(QFileInfo(path).absoluteFilePath()));
    } catch (DbError& e) {
        QMessageBox::warning(this, "Open folder", qs(e.msg));
        return;
    }
    rememberFolder(QFileInfo(path).absoluteFilePath());
    enterWorkbench();

    QDir dir(path);
    if (dir.exists("getting_started.sql")) {
        openScriptFile(dir.filePath("getting_started.sql"));
    } else {
        DynArray<plat::DirEntry> files;
        plat::listDir(toPath(path), files);
        for (int i = 0; i < files.size(); i++)
            if (!files[i].dir && files[i].name.endsWith(".sql")) {
                openScriptFile(dir.filePath(fromPath(files[i].name)));
                break;
            }
        if (editors_->count() == 0) newScript();
    }
}

void MainWindow::openMemory() {
    if (!confirmDiscard()) return;
    cat_.openMemory();
    enterWorkbench();
    newScript(MEMORY_STARTER);
}

bool MainWindow::closeFolder() {
    if (!confirmDiscard()) return false;
    try {
        cat_.close();
    } catch (DbError& e) {
        QMessageBox::warning(this, "Close folder", qs(e.msg));
    }
    showWelcome();
    return true;
}

// fresh state for a newly opened workspace
void MainWindow::enterWorkbench() {
    while (editors_->count()) {
        QWidget* w = editors_->widget(0);
        editors_->removeTab(0);
        delete w;
    }
    history_.clear();
    expanded_.clear();
    untitled_ = 0;
    results_->clearAll();
    explain_->showIntro();

    stack_->setCurrentWidget(bench_);
    toolbar_->show();
    statusBar()->show();
    navDock_->show();
    dsDock_->show();
    dsTabs_->setCurrentWidget(explain_);
    setWorkspaceActions(true);
    QString name = cat_.inMemory() ? "in-memory session" : QFileInfo(fromPath(cat_.folder())).fileName();
    setWindowTitle(name + " - MiniSQL Workbench");
    // the result grid would otherwise grab most of the height
    int h = qMax(420, split_->height());
    split_->setSizes({h * 60 / 100, h * 40 / 100});   // Qt takes a list here
    chipOrder_->setText(QString("B+ order %1").arg(cat_.order()));
    chipPool_->setText(QString("pool %1 × 4 KB").arg(cat_.pool().capacity()));
    refreshNavigator();
    refreshStatus();
    structures_->refresh();
}

void MainWindow::showWelcome() {
    while (editors_->count()) {
        QWidget* w = editors_->widget(0);
        editors_->removeTab(0);
        delete w;
    }
    history_.clear();
    results_->clearAll();
    stack_->setCurrentWidget(welcome_);
    toolbar_->hide();
    statusBar()->hide();
    navDock_->hide();
    dsDock_->hide();
    setWorkspaceActions(false);
    setWindowTitle("MiniSQL Workbench");
    welcome_->setRecent(recent_);
}

void MainWindow::setWorkspaceActions(bool on) {
    for (QAction* a : {actNewScript_, actOpenScript_, actSave_, actSaveAs_, actCloseFolder_, actRunAll_, actRunStatement_,
                       actSample_, actClearOutput_})
        a->setEnabled(on);
    navDock_->toggleViewAction()->setEnabled(on);
    dsDock_->toggleViewAction()->setEnabled(on);
}

// ---------------------------------------------------------------- scripts

SqlEditor* MainWindow::currentEditor() const { return qobject_cast<SqlEditor*>(editors_->currentWidget()); }

SqlEditor* MainWindow::newScript(const QString& text, const QString& path, const QString& title) {
    SqlEditor* ed = new SqlEditor();
    ed->setPlainText(text);
    ed->document()->setModified(false);
    ed->filePath = path;
    QString name = !title.isEmpty() ? title : !path.isEmpty() ? QFileInfo(path).fileName() : QString("Query %1").arg(++untitled_);
    ed->setProperty("title", name);
    connect(ed->document(), &QTextDocument::modificationChanged, this, [this, ed] { updateTabTitle(ed); });
    connect(ed, &SqlEditor::runRequested, this, &MainWindow::run);
    connect(ed, &QPlainTextEdit::cursorPositionChanged, this, [this, ed] {
        if (ed != currentEditor()) return;
        QTextCursor c = ed->textCursor();
        sbCursor_->setText(QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    });

    int i = editors_->addTab(ed, icon("file", theme().warn, 16), name);
    QToolButton* close = new QToolButton();
    close->setIcon(icon("x", theme().muted, 14));
    close->setAutoRaise(true);
    close->setFixedSize(20, 20);
    close->setToolTip("Close");
    connect(close, &QToolButton::clicked, this, [this, ed] { closeTab(editors_->indexOf(ed)); });
    editors_->tabBar()->setTabButton(i, QTabBar::RightSide, close);
    editors_->setTabToolTip(i, path.isEmpty() ? "Not saved yet" : QDir::toNativeSeparators(path));
    editors_->setCurrentIndex(i);
    ed->setFocus();
    return ed;
}

void MainWindow::updateTabTitle(SqlEditor* ed) {
    int i = editors_->indexOf(ed);
    if (i < 0) return;
    QString name = ed->property("title").toString();
    editors_->setTabText(i, ed->document()->isModified() ? name + "  ●" : name);
}

void MainWindow::openScriptFile(const QString& path) {
    for (int i = 0; i < editors_->count(); i++) {
        SqlEditor* ed = qobject_cast<SqlEditor*>(editors_->widget(i));
        if (ed && QFileInfo(ed->filePath) == QFileInfo(path)) {
            editors_->setCurrentIndex(i);
            return;
        }
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open script", QString("Couldn't read %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    newScript(QString::fromUtf8(f.readAll()), path);
}

void MainWindow::openScriptDialog() {
    QString dir = cat_.inMemory() ? startDir() : fromPath(cat_.folder());
    QString path = QFileDialog::getOpenFileName(this, "Open script", dir, "SQL scripts (*.sql);;All files (*)");
    if (!path.isEmpty()) openScriptFile(path);
}

bool MainWindow::saveEditor(SqlEditor* ed, bool saveAs) {
    if (!ed) return false;
    QString path = ed->filePath;
    if (path.isEmpty() || saveAs) {
        QString dir = cat_.isOpen() && !cat_.inMemory() ? fromPath(cat_.folder()) : startDir();
        QString name = path.isEmpty() ? ed->property("title").toString().toLower().replace(' ', '_') : QFileInfo(path).fileName();
        if (!name.endsWith(".sql")) name += ".sql";
        path = QFileDialog::getSaveFileName(this, "Save script", QDir(dir).filePath(name), "SQL scripts (*.sql);;All files (*)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".sql", Qt::CaseInsensitive)) path += ".sql";
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save", QString("Couldn't write %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    f.write(ed->toPlainText().toUtf8());
    f.close();
    ed->filePath = path;
    ed->setProperty("title", QFileInfo(path).fileName());
    ed->document()->setModified(false);
    updateTabTitle(ed);
    editors_->setTabToolTip(editors_->indexOf(ed), QDir::toNativeSeparators(path));
    refreshNavigator();
    statusBar()->showMessage("Saved " + QDir::toNativeSeparators(path), 4000);
    return true;
}

bool MainWindow::closeTab(int index) {
    SqlEditor* ed = qobject_cast<SqlEditor*>(editors_->widget(index));
    if (!ed) return false;
    if (ed->document()->isModified()) {
        auto answer = QMessageBox::question(this, "Close script",
                                            QString("%1 has unsaved changes. Save them first?").arg(ed->property("title").toString()),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save && !saveEditor(ed, false)) return false;
    }
    editors_->removeTab(index);
    delete ed;
    if (editors_->count() == 0 && cat_.isOpen()) newScript();
    return true;
}

bool MainWindow::confirmDiscard() {
    QString names;
    for (int i = 0; i < editors_->count(); i++) {
        SqlEditor* ed = qobject_cast<SqlEditor*>(editors_->widget(i));
        if (ed && ed->document()->isModified()) names += (names.isEmpty() ? "" : ", ") + ed->property("title").toString();
    }
    if (names.isEmpty()) return true;
    return QMessageBox::question(this, "Unsaved changes", QString("Discard unsaved changes in %1?").arg(names),
                                 QMessageBox::Discard | QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::openSample() {
    QString text = readResource(":/examples/getting_started.sql");
    newScript(text, QString(), "sample.sql");
}

// ---------------------------------------------------------------- running

void MainWindow::run(bool all) {
    SqlEditor* ed = currentEditor();
    if (!ed || !cat_.isOpen()) return;
    QString text = ed->toPlainText();
    QTextCursor cursor = ed->textCursor();
    int start = 0, end = text.size();
    if (all) {
        if (cursor.hasSelection() && hasSql(text.mid(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart()))) {
            start = cursor.selectionStart();
            end = cursor.selectionEnd();
        }
    } else if (!ed->statementAtCursor(start, end)) {
        statusBar()->showMessage("Nothing to run here", 3000);
        return;
    }
    QString sql = text.mid(start, end - start);
    if (!hasSql(sql)) {
        statusBar()->showMessage("Nothing to run: the script is empty", 3000);
        return;
    }
    int firstLine = ed->document()->findBlock(start).blockNumber() + 1;
    ed->clearError();
    ed->flash(start, end);
    runSql(sql, firstLine, ed);
}

void MainWindow::runSql(const QString& sql, int firstLine, SqlEditor* ed) {
    if (history_.size() > 400) {   // keep the Output log bounded
        DynArray<RunEntry> keep;
        for (int i = history_.size() - 300; i < history_.size(); i++) keep.push(std::move(history_[i]));
        history_ = std::move(keep);
    }
    for (int i = 0; i < history_.size(); i++) history_[i].result.rows.clear();   // old grids are gone

    DynArray<Result> results;
    exec_.run(toStr(sql), results, firstLine);
    try {
        cat_.checkpoint();   // dirty pages and the catalog go to disk after every run
    } catch (DbError& e) {
        QMessageBox::warning(this, "Saving to disk", qs(e.msg));
    }

    int first = history_.size();
    int failed = -1;
    bool changed = false;
    double total = 0;
    QString now = QTime::currentTime().toString("hh:mm:ss");
    for (int i = 0; i < results.size(); i++) {
        RunEntry e;
        e.result = std::move(results[i]);
        e.number = ++runCounter_;
        e.time = now;
        changed = changed || e.result.changed;
        total += e.result.ms;
        if (!e.result.ok) failed = history_.size();
        history_.push(std::move(e));
    }
    if (history_.size() == first) return;

    results_->showRun(history_, first);
    showEntry(failed >= 0 ? failed : history_.size() - 1);
    if (dsTabs_->currentWidget() == learn_) dsTabs_->setCurrentWidget(explain_);
    if (failed >= 0) {
        const Result& r = history_[failed].result;
        if (ed && r.errorLine > 0) ed->markError(r.errorLine, qs(r.message));
        statusBar()->showMessage("Error: " + qs(r.message), 8000);
    }
    if (changed) refreshNavigator();
    if (dsTabs_->currentWidget() == structures_) structures_->refresh();
    refreshStatus();
    int n = history_.size() - first;
    sbLast_->setText(QString("%1 statement%2 · %3 ms").arg(n).arg(n == 1 ? "" : "s").arg(total, 0, 'f', 3));
}

void MainWindow::showEntry(int i) {
    if (i < 0 || i >= history_.size()) return;
    explain_->showResult(history_[i].result);
}

// ---------------------------------------------------------------- panels

void MainWindow::showLearn(const QString& key) {
    dsDock_->show();
    dsTabs_->setCurrentWidget(learn_);
    learn_->focusOn(key);
}

void MainWindow::showStructures(const QString& table) {
    dsDock_->show();
    dsTabs_->setCurrentWidget(structures_);
    structures_->selectTable(table);
}

void MainWindow::refreshNavigator() {
    const ThemeColors& t = theme();
    nav_->clear();
    if (!cat_.isOpen()) return;

    auto header = [&](const QString& text) {
        QTreeWidgetItem* h = new QTreeWidgetItem(nav_);
        h->setText(0, text);
        QFont f = nav_->font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        h->setFont(0, f);
        h->setForeground(0, t.muted);
        h->setFlags(Qt::ItemIsEnabled);
        return h;
    };
    auto note = [&](QTreeWidgetItem* parent, const QString& text) {
        QTreeWidgetItem* n = new QTreeWidgetItem(parent);
        n->setText(0, text);
        n->setForeground(0, t.muted);
        n->setFlags(Qt::ItemIsEnabled);
    };

    QTreeWidgetItem* tables = header(QString("TABLES (%1)").arg(cat_.tableCount()));
    if (cat_.tableCount() == 0) note(tables, "No tables yet. Try CREATE TABLE.");
    for (int i = 0; i < cat_.tableCount(); i++) {
        Table* tb = cat_.tableAt(i);
        QTreeWidgetItem* item = new QTreeWidgetItem(tables);
        item->setText(0, qs(tb->name));
        item->setText(1, QString::number(tb->rowCount));
        item->setIcon(0, icon("table", t.accent, 16));
        item->setForeground(1, t.muted);
        item->setData(0, Qt::UserRole, "table");
        item->setToolTip(0, QString("%1 rows on %2 page(s), %3-byte row slots.\nDouble-click to select its rows, "
                                    "right-click for more.").arg(tb->rowCount).arg(tb->pageCount()).arg(tb->slotSize));
        for (int c = 0; c < tb->cols.size(); c++) {
            const Column& col = tb->cols[c];
            QTreeWidgetItem* ci = new QTreeWidgetItem(item);
            ci->setText(0, qs(col.name));
            ci->setText(1, col.type == Type::Varchar ? QString("VARCHAR(%1)").arg(col.size) : QString(typeName(col.type)));
            ci->setForeground(1, t.muted);
            ci->setFont(1, monoFont(8));
            Index* ix = tb->indexOn(c);
            if (c == tb->pkCol) {
                ci->setIcon(0, icon("key", t.warn, 15));
                ci->setToolTip(0, "Primary key, indexed by a B+ tree");
            } else if (ix) {
                ci->setIcon(0, icon("diamond", QColor("#34d399"), 15));
                ci->setToolTip(0, QString("Indexed by an AVL tree (%1)").arg(qs(tb->indexName(c))));
            }
        }
        for (int c = 0; c < tb->cols.size(); c++) {
            Index* ix = tb->indexOn(c);
            if (!ix) continue;
            QTreeWidgetItem* xi = new QTreeWidgetItem(item);
            xi->setText(0, qs(tb->indexName(c)));
            xi->setText(1, QString("%1 · h%2").arg(c == tb->pkCol ? "B+" : "AVL").arg(ix->height()));
            xi->setIcon(0, icon("layers", c == tb->pkCol ? QColor("#2dd4bf") : QColor("#34d399"), 15));
            xi->setForeground(1, t.muted);
            xi->setToolTip(0, QString("%1 on %2: %3 keys, height %4").arg(ix->kind()).arg(qs(tb->cols[c].name)).arg(ix->keys()).arg(ix->height()));
        }
        item->setExpanded(expanded_.find(tb->name) != nullptr);
    }

    QTreeWidgetItem* scripts = header("SCRIPTS");
    QTreeWidgetItem* storage = header("STORAGE");
    if (cat_.inMemory()) {
        note(scripts, "Save scripts anywhere with Save As…");
        note(storage, "Pages live in RAM in this session.");
    } else {
        DynArray<plat::DirEntry> files;
        plat::listDir(cat_.folder(), files);
        int count = 0;
        for (int i = 0; i < files.size(); i++) {
            if (files[i].dir || !files[i].name.endsWith(".sql")) continue;
            QTreeWidgetItem* s = new QTreeWidgetItem(scripts);
            QString path = QDir(fromPath(cat_.folder())).filePath(fromPath(files[i].name));
            s->setText(0, fromPath(files[i].name));
            s->setIcon(0, icon("file", t.warn, 15));
            s->setData(0, Qt::UserRole, "script");
            s->setData(0, Qt::UserRole + 1, path);
            s->setToolTip(0, "Double-click to open");
            count++;
        }
        if (!count) note(scripts, "No .sql files in this folder.");

        DynArray<plat::DirEntry> data;
        plat::listDir(cat_.dataDir(), data);
        for (int i = 0; i < data.size(); i++) {
            if (data[i].dir) continue;
            QTreeWidgetItem* f = new QTreeWidgetItem(storage);
            f->setText(0, fromPath(data[i].name));
            long long size = data[i].size;
            f->setText(1, size < 1024 ? QString("%1 B").arg(size) : QString("%1 KB").arg(size / 1024.0, 0, 'f', 1));
            f->setForeground(1, t.muted);
            f->setIcon(0, icon("disk", t.muted, 15));
            if (data[i].name.endsWith(".tbl"))
                f->setToolTip(0, QString("data/%1: %2 page(s) of 4 KB").arg(fromPath(data[i].name)).arg(size / 4096));
            else
                f->setToolTip(0, "data/catalog.msql: the table definitions, as plain text");
        }
        if (data.empty()) note(storage, "data/ is empty until you create a table.");
    }
    for (QTreeWidgetItem* h : {tables, scripts, storage}) h->setExpanded(true);
}

void MainWindow::navigatorDoubleClick(QTreeWidgetItem* item) {
    QString kind = item->data(0, Qt::UserRole).toString();
    if (kind == "table") runSql(QString("SELECT * FROM %1;").arg(item->text(0)), 1, nullptr);
    else if (kind == "script") openScriptFile(item->data(0, Qt::UserRole + 1).toString());
}

void MainWindow::navigatorMenu(const QPoint& pos) {
    QTreeWidgetItem* item = nav_->itemAt(pos);
    if (!item) return;
    QString kind = item->data(0, Qt::UserRole).toString();
    QString name = item->text(0);
    QMenu menu(this);
    if (kind == "table") {
        connect(menu.addAction("Select Rows"), &QAction::triggered, this,
                [this, name] { runSql(QString("SELECT * FROM %1;").arg(name), 1, nullptr); });
        connect(menu.addAction("Describe"), &QAction::triggered, this,
                [this, name] { runSql(QString("DESCRIBE %1;").arg(name), 1, nullptr); });
        connect(menu.addAction("Show Its Structures"), &QAction::triggered, this, [this, name] { showStructures(name); });
    } else if (kind == "script") {
        QString path = item->data(0, Qt::UserRole + 1).toString();
        connect(menu.addAction("Open"), &QAction::triggered, this, [this, path] { openScriptFile(path); });
        connect(menu.addAction("Delete…"), &QAction::triggered, this, [this, path, name] {
            if (QMessageBox::question(this, "Delete script", QString("Delete %1 from the folder? This can't be undone.").arg(name)) !=
                QMessageBox::Yes)
                return;
            QFile::remove(path);
            for (int i = 0; i < editors_->count(); i++) {
                SqlEditor* ed = qobject_cast<SqlEditor*>(editors_->widget(i));
                if (ed && ed->filePath == path) {
                    ed->filePath.clear();
                    ed->document()->setModified(true);
                }
            }
            refreshNavigator();
        });
    } else {
        return;
    }
    menu.exec(nav_->viewport()->mapToGlobal(pos));
}

void MainWindow::refreshStatus() {
    if (!cat_.isOpen()) return;
    sbWorkspace_->setText(cat_.inMemory() ? "In-memory session" : QDir::toNativeSeparators(fromPath(cat_.folder())));
    sbTables_->setText(QString("%1 table%2").arg(cat_.tableCount()).arg(cat_.tableCount() == 1 ? "" : "s"));
    BufferPool& p = cat_.pool();
    long long total = p.hits + p.misses;
    sbPool_->setText(QString("Buffer pool %1/%2").arg(p.used()).arg(p.capacity()) +
                     (total ? QString(" · %1% hits").arg(100.0 * p.hits / total, 0, 'f', 1) : QString()));
}

// ---------------------------------------------------------------- theme, settings, dialogs

void MainWindow::toggleTheme() {
    bool dark = !theme().dark;
    applyTheme(dark);
    QSettings().setValue("theme", dark ? "dark" : "light");
    refreshIcons();
    welcome_->refreshTheme();
    explain_->refreshTheme();
    learn_->rebuild();
    results_->refreshTheme();
    structures_->refresh();
    for (int i = 0; i < editors_->count(); i++)
        if (SqlEditor* ed = qobject_cast<SqlEditor*>(editors_->widget(i))) ed->refreshTheme();
    refreshNavigator();
}

void MainWindow::loadSettings() {
    QSettings s;
    QString list = s.value("recent").toString();
    recent_.clear();
    int i = 0;
    while (i < list.size()) {
        int nl = list.indexOf('\n', i);
        if (nl < 0) nl = list.size();
        if (nl > i) recent_.push(list.mid(i, nl - i));
        i = nl + 1;
    }
    QByteArray geometry = s.value("geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
}

static void saveRecent(const DynArray<QString>& recent) {
    QString list;
    for (int i = 0; i < recent.size(); i++) list += (i ? "\n" : "") + recent[i];
    QSettings().setValue("recent", list);
}

void MainWindow::rememberFolder(const QString& path) {
    DynArray<QString> next;
    next.push(path);
    for (int i = 0; i < recent_.size() && next.size() < 8; i++)
        if (recent_[i] != path) next.push(recent_[i]);
    recent_ = std::move(next);
    saveRecent(recent_);
}

void MainWindow::forgetFolder(const QString& path) {
    for (int i = 0; i < recent_.size(); i++)
        if (recent_[i] == path) recent_.removeAt(i--);
    saveRecent(recent_);
    welcome_->setRecent(recent_);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    QSettings().setValue("geometry", saveGeometry());
    try {
        cat_.close();
    } catch (DbError&) {
    }
    e->accept();
}

void MainWindow::showShortcuts() {
    QMessageBox box(this);
    box.setWindowTitle("Keyboard shortcuts");
    box.setTextFormat(Qt::RichText);
    box.setText("<table cellpadding='5'>"
                "<tr><td>Run statement at cursor</td><td><b>Ctrl+Enter</b></td></tr>"
                "<tr><td>Run all (or the selection)</td><td><b>Ctrl+Shift+Enter</b> or <b>F5</b></td></tr>"
                "<tr><td>New script</td><td><b>Ctrl+N</b></td></tr>"
                "<tr><td>Open script</td><td><b>Ctrl+O</b></td></tr>"
                "<tr><td>Save / Save as</td><td><b>Ctrl+S</b> / <b>Ctrl+Shift+S</b></td></tr>"
                "<tr><td>Open folder</td><td><b>Ctrl+Shift+O</b></td></tr>"
                "<tr><td>Toggle comment</td><td><b>Ctrl+/</b></td></tr>"
                "<tr><td>Indent / outdent</td><td><b>Tab</b> / <b>Shift+Tab</b></td></tr>"
                "</table>");
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About MiniSQL",
                       "<h3>MiniSQL Workbench</h3>"
                       "<p>A small relational database engine for the Data Structures Lab. Every container in the "
                       "engine is hand-written C++: dynamic array, linked list, hash table, stack, queue, AVL tree, "
                       "B+ tree and merge sort. No STL containers.</p>"
                       "<p>The Data Structures panel explains which of them each query used.</p>");
}

void MainWindow::showDsDialog(const QString& key) {
    QDialog dlg(this);
    dlg.setWindowTitle("Under the hood");
    dlg.resize(560, 460);
    QVBoxLayout* col = new QVBoxLayout(&dlg);
    QTextBrowser* text = new QTextBrowser(&dlg);
    text->setHtml(dsCardHtml(key.toUtf8().constData(), false));
    col->addWidget(text);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    col->addWidget(buttons);
    dlg.exec();
}
