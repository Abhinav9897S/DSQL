#include "gui/MainWindow.h"
#include "gui/CodeEditor.h"
#include "gui/Terminal.h"
#include "gui/QtStr.h"
#include "sql/Checker.h"
#include "sql/Format.h"
#include "util/Error.h"
#include <QApplication>
#include <QStackedWidget>
#include <QSplitter>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QCloseEvent>
#include <QStyleFactory>
#include <QStyle>
#include <QPalette>
#include <QTextBlock>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>

static const int MAX_ROWS_SHOWN = 1000;     // in the Result grid
static const int MAX_ROWS_IN_TERMINAL = 50;

// ---------------------------------------------------------------- helpers

// [start, end) of every statement, splitting on ';' outside strings and comments
struct TextRange {
    int start = 0;
    int end = 0;
};

static DynArray<TextRange> splitStatements(const QString& text) {
    DynArray<TextRange> out;
    int start = 0, i = 0, n = text.size();
    while (i < n) {
        QChar c = text[i];
        if (c == '\'') {   // skip a string, '' is an escaped quote
            i++;
            while (i < n) {
                if (text[i] == '\'' && i + 1 < n && text[i + 1] == '\'') i += 2;
                else if (text[i] == '\'') break;
                else i++;
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

// true if the text has something besides whitespace and comments
static bool hasSql(const QString& text) {
    int i = 0, n = text.size();
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

// trace tags from the engine -> readable names
static QString dsName(const Str& tag) {
    if (tag == "bptree") return "B+ Tree";
    if (tag == "avl") return "AVL Tree";
    if (tag == "hash") return "Hash Table";
    if (tag == "pool") return "Buffer Pool";
    if (tag == "list") return "Linked List";
    if (tag == "array") return "Dynamic Array";
    if (tag == "stack") return "Stack";
    if (tag == "queue") return "Queue";
    if (tag == "sort") return "Merge Sort";
    if (tag == "disk") return "Pages on disk";
    return "Parser / Planner";
}

// "1 hit", "3 hits"
static QString count(long long n, const char* one, const char* many) {
    return QString("%1 %2").arg(n).arg(n == 1 ? one : many);
}

static QString readResource(const char* path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// ---------------------------------------------------------------- window

MainWindow::MainWindow(Catalog& cat, Executor& exec, QWidget* parent) : QMainWindow(parent), cat_(cat), exec_(exec) {
    setWindowTitle("MiniSQL");
    resize(1280, 800);

    QSettings settings;
    bool dark = settings.value("darkMode", false).toBool();
    setEditorColorsDark(dark);   // before any editor exists

    checkTimer_ = new QTimer(this);
    checkTimer_->setSingleShot(true);
    checkTimer_->setInterval(400);   // check once typing pauses
    connect(checkTimer_, &QTimer::timeout, this, &MainWindow::checkNow);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildStartPage());
    pages_->addWidget(buildWorkPage());
    setCentralWidget(pages_);
    buildMenus();

    cursorLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(cursorLabel_);

    QString list = settings.value("recent").toString();
    int from = 0;
    while (from < list.size()) {   // one folder per line
        int nl = list.indexOf('\n', from);
        if (nl < 0) nl = list.size();
        if (nl > from) recent_.push(list.mid(from, nl - from));
        from = nl + 1;
    }
    setDarkMode(dark);
    showStartPage();
}

QWidget* MainWindow::buildStartPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* col = new QVBoxLayout(page);
    col->setContentsMargins(40, 30, 40, 30);
    col->setSpacing(12);

    QLabel* title = new QLabel("MiniSQL", page);
    QFont big = title->font();
    big.setPointSize(big.pointSize() * 2);
    big.setBold(true);
    title->setFont(big);
    col->addWidget(title);
    col->addWidget(new QLabel("A small database engine built from hand-written data structures.\n"
                              "Create or open a folder to start. Tables are saved in its data/ subfolder.",
                              page));

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* newBtn = new QPushButton("New Folder...", page);
    QPushButton* openBtn = new QPushButton("Open Folder...", page);
    QPushButton* memBtn = new QPushButton("In-Memory Session", page);
    memBtn->setToolTip("Nothing is saved to disk");
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newFolder);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openFolderDialog);
    connect(memBtn, &QPushButton::clicked, this, &MainWindow::openMemory);
    buttons->addWidget(newBtn);
    buttons->addWidget(openBtn);
    buttons->addWidget(memBtn);
    buttons->addStretch();
    col->addLayout(buttons);

    col->addSpacing(10);
    col->addWidget(new QLabel("Recent folders (double-click to open):", page));
    recentList_ = new QListWidget(page);
    connect(recentList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { openFolder(item->data(Qt::UserRole).toString()); });
    col->addWidget(recentList_, 1);
    return page;
}

QWidget* MainWindow::buildWorkPage() {
    QSplitter* split = new QSplitter(Qt::Horizontal);

    // left: tables
    tables_ = new QTreeWidget(split);
    tables_->setColumnCount(2);
    tables_->setHeaderLabels({"Tables", "Details"});   // Qt takes a list here
    tables_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tables_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tables_->header()->setStretchLastSection(false);
    tables_->setToolTip("Double-click a table to see its rows");
    connect(tables_, &QTreeWidget::itemDoubleClicked, this, &MainWindow::tableDoubleClicked);

    // middle: run bar + editor on top, terminal / result / problems below
    QSplitter* middle = new QSplitter(Qt::Vertical, split);
    QWidget* top = new QWidget(middle);
    QVBoxLayout* topCol = new QVBoxLayout(top);
    topCol->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout* bar = new QHBoxLayout();
    QPushButton* runAll = new QPushButton("Run All  (F5)", top);
    QPushButton* runOne = new QPushButton("Run Statement  (Ctrl+Enter)", top);
    runAll->setToolTip("Run every statement, or only the selected text");
    runOne->setToolTip("Run the statement the cursor is in");
    connect(runAll, &QPushButton::clicked, this, [this] { run(true); });
    connect(runOne, &QPushButton::clicked, this, [this] { run(false); });
    darkBox_ = new QCheckBox("Dark mode", top);
    connect(darkBox_, &QCheckBox::toggled, this, &MainWindow::setDarkMode);
    problemsLabel_ = new QLabel(top);
    problemsLabel_->setToolTip("The script is checked as you type. Click the Problems tab for the list.");
    scriptLabel_ = new QLabel(top);
    bar->addWidget(runAll);
    bar->addWidget(runOne);
    bar->addSpacing(12);
    bar->addWidget(darkBox_);
    bar->addStretch();
    bar->addWidget(problemsLabel_);
    bar->addSpacing(12);
    bar->addWidget(scriptLabel_);
    topCol->addLayout(bar);

    editor_ = new CodeEditor(top);
    editor_->setPlaceholderText("Type SQL here, e.g.  CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR(20));");
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        QTextCursor c = editor_->textCursor();
        cursorLabel_->setText(QString("Line %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateScriptLabel);
    connect(editor_, &QPlainTextEdit::textChanged, checkTimer_, qOverload<>(&QTimer::start));
    topCol->addWidget(editor_);

    output_ = new QTabWidget(middle);
    terminal_ = new Terminal(output_);
    connect(terminal_, &Terminal::commandEntered, this, [this](const QString& sql) { runSql(sql, 1, false); });
    resultTable_ = new QTableWidget(output_);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->setAlternatingRowColors(true);
    problemsList_ = new QListWidget(output_);
    problemsList_->setToolTip("Double-click a problem to jump to its line");
    connect(problemsList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { editor_->goToLine(item->data(Qt::UserRole).toInt()); });
    output_->addTab(terminal_, "Terminal");
    output_->addTab(resultTable_, "Result");
    output_->addTab(problemsList_, "Problems");
    middle->addWidget(top);
    middle->addWidget(output_);
    middle->setStretchFactor(0, 3);
    middle->setStretchFactor(1, 2);

    // right: how the last query ran
    QWidget* right = new QWidget(split);
    QVBoxLayout* rightCol = new QVBoxLayout(right);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->addWidget(new QLabel("<b>How the query ran</b>", right));
    explain_ = new QTextBrowser(right);
    explain_->setPlaceholderText("Run a query and this panel lists the data structures it used, step by step.");
    rightCol->addWidget(explain_);

    split->addWidget(tables_);
    split->addWidget(middle);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({200, 640, 340});   // Qt takes a list here
    return split;
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    connect(file->addAction("New Folder..."), &QAction::triggered, this, &MainWindow::newFolder);
    connect(file->addAction("Open Folder..."), &QAction::triggered, this, &MainWindow::openFolderDialog);
    closeFolderAct_ = file->addAction("Close Folder");
    connect(closeFolderAct_, &QAction::triggered, this, &MainWindow::closeFolder);
    file->addSeparator();
    openScriptAct_ = file->addAction("Open Script...");
    openScriptAct_->setShortcut(QKeySequence("Ctrl+O"));
    connect(openScriptAct_, &QAction::triggered, this, &MainWindow::openScriptDialog);
    saveScriptAct_ = file->addAction("Save Script");
    saveScriptAct_->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveScriptAct_, &QAction::triggered, this, &MainWindow::saveScript);
    file->addSeparator();
    connect(file->addAction("Exit"), &QAction::triggered, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu("&Edit");
    QAction* undo = edit->addAction("Undo");
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, editor_, &QPlainTextEdit::undo);
    QAction* redo = edit->addAction("Redo");
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, editor_, &QPlainTextEdit::redo);
    edit->addSeparator();
    upperAct_ = edit->addAction("Capitalise SQL Keywords");
    upperAct_->setShortcut(QKeySequence("Ctrl+Shift+U"));
    connect(upperAct_, &QAction::triggered, editor_, &CodeEditor::uppercaseKeywords);

    QMenu* query = menuBar()->addMenu("&Query");
    runAllAct_ = query->addAction("Run All");
    runAllAct_->setShortcuts({QKeySequence("F5"), QKeySequence("Ctrl+Shift+Return")});   // Qt takes a list here
    connect(runAllAct_, &QAction::triggered, this, [this] { run(true); });
    runStatementAct_ = query->addAction("Run Statement");
    runStatementAct_->setShortcut(QKeySequence("Ctrl+Return"));
    connect(runStatementAct_, &QAction::triggered, this, [this] { run(false); });
    query->addSeparator();
    sampleAct_ = query->addAction("Load Sample Script");
    connect(sampleAct_, &QAction::triggered, this, &MainWindow::loadSample);
    QAction* clearTerm = query->addAction("Clear Terminal");
    connect(clearTerm, &QAction::triggered, terminal_, &Terminal::clear);

    QMenu* view = menuBar()->addMenu("&View");
    darkAct_ = view->addAction("Dark Mode");
    darkAct_->setCheckable(true);
    connect(darkAct_, &QAction::toggled, this, &MainWindow::setDarkMode);

    QMenu* help = menuBar()->addMenu("&Help");
    connect(help->addAction("About"), &QAction::triggered, this, [this] {
        QMessageBox::about(this, "About MiniSQL",
                           "MiniSQL - a small relational database engine for the Data Structures Lab.\n\n"
                           "Every container in the engine is written by hand in C++ (dynamic array, linked list, "
                           "hash table, stack, queue, AVL tree, B+ tree, merge sort). No STL containers.\n\n"
                           "F5 runs the script, Ctrl+Enter the statement under the cursor. The script is checked "
                           "for errors as you type, and SQL words are capitalised for you.");
    });
}

// ---------------------------------------------------------------- folders

void MainWindow::newFolder() {
    QString parent = QFileDialog::getExistingDirectory(this, "Where should the new folder go?", QDir::homePath());
    if (parent.isEmpty()) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "my_database", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QDir dir(parent);
    if (dir.exists(name) || !dir.mkdir(name)) {
        QMessageBox::warning(this, "New Folder", "Couldn't create that folder (maybe it already exists).");
        return;
    }
    // start the folder off with the sample script
    QFile script(QDir(dir.filePath(name)).filePath("getting_started.sql"));
    if (script.open(QIODevice::WriteOnly)) script.write(readResource(":/examples/getting_started.sql").toUtf8());
    script.close();
    openFolder(dir.filePath(name));
}

void MainWindow::openFolderDialog() {
    QString path = QFileDialog::getExistingDirectory(this, "Open Folder", QDir::homePath());
    if (!path.isEmpty()) openFolder(path);
}

void MainWindow::openFolder(const QString& path) {
    if (!maybeSave()) return;
    QString full = QFileInfo(path).absoluteFilePath();
    try {
        cat_.openFolder(toPath(full));
    } catch (DbError& e) {
        QMessageBox::warning(this, "Open Folder", qs(e.msg));
        return;
    }
    rememberFolder(full);
    QDir dir(full);
    QString script = dir.filePath(dir.exists("getting_started.sql") ? "getting_started.sql" : "queries.sql");
    if (QFile::exists(script)) loadScript(script);
    else setScript(QString(), script);
    setWindowTitle(dir.dirName() + " - MiniSQL");
    showWorkPage();
}

void MainWindow::openMemory() {
    if (!maybeSave()) return;
    cat_.openMemory();
    setScript("-- In-memory session: nothing is saved to disk.\n"
              "-- Query > Load Sample Script loads a full example.\n\n"
              "CREATE TABLE notes (id INT PRIMARY KEY, title VARCHAR(40));\n"
              "INSERT INTO notes VALUES (1, 'first'), (2, 'second');\n"
              "SELECT * FROM notes WHERE id = 2;\n",
              QString());
    setWindowTitle("In-memory session - MiniSQL");
    showWorkPage();
}

void MainWindow::closeFolder() {
    if (!maybeSave()) return;
    try {
        cat_.close();
    } catch (DbError& e) {
        QMessageBox::warning(this, "Close Folder", qs(e.msg));
    }
    showStartPage();
}

void MainWindow::showWorkPage() {
    resultTable_->clear();
    resultTable_->setRowCount(0);
    resultTable_->setColumnCount(0);
    explain_->clear();
    terminal_->clear();
    terminal_->printNote(cat_.inMemory() ? "In-memory session.\n" : "Folder: " + QDir::toNativeSeparators(fromPath(cat_.folder())) + "\n");
    refreshTables();
    checkNow();
    for (QAction* a : {closeFolderAct_, openScriptAct_, saveScriptAct_, runAllAct_, runStatementAct_, sampleAct_, upperAct_})
        a->setEnabled(true);
    pages_->setCurrentIndex(1);
    output_->setCurrentWidget(terminal_);
    editor_->setFocus();
}

void MainWindow::showStartPage() {
    for (QAction* a : {closeFolderAct_, openScriptAct_, saveScriptAct_, runAllAct_, runStatementAct_, sampleAct_, upperAct_})
        a->setEnabled(false);
    refreshRecent();
    setWindowTitle("MiniSQL");
    statusBar()->clearMessage();
    cursorLabel_->clear();
    pages_->setCurrentIndex(0);
}

// ---------------------------------------------------------------- script

void MainWindow::setScript(const QString& text, const QString& path) {
    scriptPath_ = path;
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    updateScriptLabel();
    checkNow();
}

void MainWindow::loadScript(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open Script", "Couldn't read " + QDir::toNativeSeparators(path));
        return;
    }
    setScript(QString::fromUtf8(f.readAll()), path);
}

void MainWindow::openScriptDialog() {
    if (!maybeSave()) return;
    QString dir = cat_.inMemory() ? QDir::homePath() : fromPath(cat_.folder());
    QString path = QFileDialog::getOpenFileName(this, "Open Script", dir, "SQL scripts (*.sql);;All files (*)");
    if (!path.isEmpty()) loadScript(path);
}

bool MainWindow::saveScript() {
    if (scriptPath_.isEmpty()) {
        scriptPath_ = QFileDialog::getSaveFileName(this, "Save Script", QDir::homePath(), "SQL scripts (*.sql)");
        if (scriptPath_.isEmpty()) return false;
    }
    QFile f(scriptPath_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save Script", "Couldn't write " + QDir::toNativeSeparators(scriptPath_));
        return false;
    }
    f.write(editor_->toPlainText().toUtf8());
    editor_->document()->setModified(false);
    updateScriptLabel();
    statusBar()->showMessage("Saved " + QDir::toNativeSeparators(scriptPath_), 3000);
    return true;
}

// "queries.sql *" above the editor; the star means unsaved changes
void MainWindow::updateScriptLabel() {
    QString name = scriptPath_.isEmpty() ? "unsaved script" : QFileInfo(scriptPath_).fileName();
    scriptLabel_->setText(editor_->document()->isModified() ? name + " *" : name);
}

// asks before throwing away unsaved SQL; false = the user cancelled
bool MainWindow::maybeSave() {
    if (!editor_->document()->isModified()) return true;
    auto answer = QMessageBox::question(this, "Unsaved script", "The script has unsaved changes. Save them?",
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveScript();
    return true;
}

void MainWindow::loadSample() {
    if (!maybeSave()) return;
    editor_->setPlainText(readResource(":/examples/getting_started.sql"));
    editor_->document()->setModified(true);
    checkNow();
}

// ---------------------------------------------------------------- checking and running

// parse the whole script without running it and mark what's wrong
void MainWindow::checkNow() {
    if (!cat_.isOpen()) return;
    DynArray<Problem> problems;
    checkSql(toStr(editor_->toPlainText()), &cat_, problems);

    DynArray<EditorMark> marks;
    problemsList_->clear();
    for (int i = 0; i < problems.size(); i++) {
        const Problem& p = problems[i];
        EditorMark m;
        m.line = p.line;
        m.col = p.col;
        m.length = p.length;
        m.message = qs(p.message);
        marks.push(m);
        QListWidgetItem* item = new QListWidgetItem(QString("Line %1, col %2:  %3").arg(p.line).arg(p.col).arg(m.message), problemsList_);
        item->setData(Qt::UserRole, p.line);
        item->setForeground(editorColors().error);
    }
    editor_->setMarks(marks);

    int n = problems.size();
    output_->setTabText(2, n ? QString("Problems (%1)").arg(n) : "Problems");
    problemsLabel_->setText(n ? QString("%1 problem%2").arg(n).arg(n == 1 ? "" : "s") : "No problems");
    problemsLabel_->setStyleSheet(QString("color: %1;").arg((n ? editorColors().error : editorColors().termOk).name()));
}

void MainWindow::run(bool all) {
    if (!cat_.isOpen()) return;
    QString text = editor_->toPlainText();
    QTextCursor cursor = editor_->textCursor();
    int start = 0, end = text.size();
    if (all) {
        if (cursor.hasSelection()) {   // just the selected text
            start = cursor.selectionStart();
            end = cursor.selectionEnd();
        }
    } else {
        // the statement the cursor is in (or the last one before it)
        int pos = cursor.position();
        bool found = false;
        DynArray<TextRange> parts = splitStatements(text);
        for (int i = 0; i < parts.size(); i++) {
            if (!hasSql(text.mid(parts[i].start, parts[i].end - parts[i].start))) continue;
            if (parts[i].start <= pos || !found) {
                start = parts[i].start;
                end = parts[i].end;
                found = true;
            }
            if (pos >= parts[i].start && pos <= parts[i].end) break;
        }
        if (!found) return;
    }
    QString sql = text.mid(start, end - start);
    if (!hasSql(sql)) {
        statusBar()->showMessage("Nothing to run", 3000);
        return;
    }
    runSql(sql, editor_->document()->findBlock(start).blockNumber() + 1, true);
}

void MainWindow::runSql(const QString& sql, int firstLine, bool fromEditor) {
    DynArray<Result> results;
    exec_.run(toStr(sql), results, firstLine);
    try {
        cat_.checkpoint();   // write dirty pages and the catalog to disk
    } catch (DbError& e) {
        QMessageBox::warning(this, "Saving", qs(e.msg));
    }

    const Result* lastGrid = nullptr;
    const Result* last = nullptr;
    bool changed = false;
    for (int i = 0; i < results.size(); i++) {
        const Result& r = results[i];
        terminal_->printCommand(qs(r.sql));
        if (r.ok) {
            terminal_->printOutput(qs(formatTable(r, MAX_ROWS_IN_TERMINAL)));
            terminal_->printSuccess(qs(formatSummary(r)));
        } else {
            QString where = fromEditor && r.errorLine > 0 ? QString(" (line %1)").arg(r.errorLine) : QString();
            terminal_->printError("ERROR" + where + ": " + qs(r.message));
        }
        if (r.ok && !r.columns.empty()) lastGrid = &r;
        if (r.changed) changed = true;
        last = &r;
    }
    if (!last) return;

    if (lastGrid) showResult(*lastGrid);
    explain(*last);
    if (changed) refreshTables();
    output_->setCurrentWidget(terminal_);
    checkNow();   // new or dropped tables change what counts as an error

    if (!last->ok) {
        statusBar()->showMessage("Error: " + qs(last->message), 8000);
        if (fromEditor && last->errorLine > 0) {   // mark the line and put the cursor there
            EditorMark m;
            m.line = last->errorLine;
            m.message = qs(last->message);
            editor_->addMark(m);
            editor_->goToLine(last->errorLine);
        }
    } else {
        statusBar()->showMessage(qs(last->message), 4000);
    }
}

void MainWindow::showResult(const Result& r) {
    int rows = r.rows.size() < MAX_ROWS_SHOWN ? r.rows.size() : MAX_ROWS_SHOWN;
    resultTable_->clear();
    resultTable_->setColumnCount(r.columns.size());
    resultTable_->setRowCount(rows);
    for (int c = 0; c < r.columns.size(); c++)
        resultTable_->setHorizontalHeaderItem(c, new QTableWidgetItem(qs(r.columns[c])));
    for (int i = 0; i < rows; i++) {
        for (int c = 0; c < r.columns.size(); c++) {
            QTableWidgetItem* item = new QTableWidgetItem(qs(r.rows[i][c].toString()));
            if (r.types[c] == Type::Int || r.types[c] == Type::Float)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            resultTable_->setItem(i, c, item);
        }
    }
    resultTable_->resizeColumnsToContents();
}

// the right-hand panel: plan, structures used, then the engine's own step list
void MainWindow::explain(const Result& r) {
    QString html = QString("<p><b>%1</b> &nbsp;%2 &nbsp;(%3 ms)</p>")
                       .arg(qs(r.kind).toHtmlEscaped(), qs(r.message).toHtmlEscaped())
                       .arg(r.ms, 0, 'f', 3);
    html += "<pre>" + qs(r.sql).toHtmlEscaped() + "</pre>";
    if (!r.plan.empty()) html += "<p><b>Plan:</b> " + qs(r.plan).toHtmlEscaped() + "</p>";

    const DsStats& s = r.stats;
    QString used;
    auto add = [&used](const char* name, const QString& what) { used += QString("<li><b>%1</b>: %2</li>").arg(name, what); };
    if (s.queuePop) add("Queue", count(s.queuePop, "token", "tokens") + " passed from the tokenizer to the parser");
    if (s.hashProbes) add("Hash Table", count(s.hashProbes, "chain entry", "chain entries") + " checked (catalog, page table, joins, GROUP BY)");
    if (s.bptVisits) add("B+ Tree", count(s.bptVisits, "node", "nodes") + " visited, " + count(s.bptSplits, "split", "splits") + ", " + count(s.bptMerges, "merge", "merges"));
    if (s.avlVisits) add("AVL Tree", count(s.avlVisits, "node", "nodes") + " visited, " + count(s.avlLL + s.avlRR + s.avlLR + s.avlRL, "rotation", "rotations"));
    if (s.stackPush) add("Stack", count(s.stackPush, "push", "pushes") + " while evaluating WHERE / SET");
    if (s.poolHits || s.poolMisses) add("Buffer Pool", count(s.poolHits, "page hit", "page hits") + ", " + count(s.poolMisses, "miss", "misses"));
    if (s.listMoves) add("Linked List", count(s.listMoves, "move", "moves") + " in the LRU list");
    if (s.sortCompares) add("Merge Sort", count(s.sortCompares, "comparison", "comparisons"));
    if (s.arrayGrows) add("Dynamic Array", "grew " + count(s.arrayGrows, "time", "times"));
    if (!used.isEmpty()) html += "<p><b>Data structures used</b></p><ul>" + used + "</ul>";

    if (r.trace.steps.size()) {
        html += "<p><b>Step by step</b></p><ol>";
        for (int i = 0; i < r.trace.steps.size(); i++) {
            const TraceStep& st = r.trace.steps[i];
            html += QString("<li><b>[%1] %2</b>%3<br>%4</li>")
                        .arg(dsName(st.ds), qs(st.title).toHtmlEscaped(),
                             st.repeat > 1 ? QString(" (x%1)").arg(st.repeat) : QString(), qs(st.detail).toHtmlEscaped());
        }
        html += "</ol>";
    }
    explain_->setHtml(html);
}

void MainWindow::refreshTables() {
    tables_->clear();
    for (int i = 0; i < cat_.tableCount(); i++) {
        Table* t = cat_.tableAt(i);
        QTreeWidgetItem* item = new QTreeWidgetItem(tables_);
        item->setText(0, qs(t->name));
        item->setText(1, QString("%1 rows").arg(t->rowCount));
        for (int c = 0; c < t->cols.size(); c++) {
            const Column& col = t->cols[c];
            QTreeWidgetItem* ci = new QTreeWidgetItem(item);
            QString note = c == t->pkCol ? "  (primary key, B+ tree)" : t->indexOn(c) ? "  (AVL index)" : "";
            ci->setText(0, qs(col.name) + note);
            ci->setText(1, col.type == Type::Varchar ? QString("VARCHAR(%1)").arg(col.size) : QString(typeName(col.type)));
        }
    }
    if (cat_.tableCount() == 0) {
        QTreeWidgetItem* none = new QTreeWidgetItem(tables_);
        none->setText(0, "(no tables yet)");
        none->setDisabled(true);
    }
}

void MainWindow::tableDoubleClicked(QTreeWidgetItem* item) {
    if (item->parent() || item->isDisabled()) return;
    runSql("SELECT * FROM " + item->text(0) + ";", 1, false);
}

// ---------------------------------------------------------------- settings

void MainWindow::setDarkMode(bool on) {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p = qApp->style()->standardPalette();
    if (on) {
        p.setColor(QPalette::Window, QColor(45, 45, 48));
        p.setColor(QPalette::WindowText, Qt::white);
        p.setColor(QPalette::Base, QColor(30, 30, 30));
        p.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        p.setColor(QPalette::Text, QColor(212, 212, 212));
        p.setColor(QPalette::Button, QColor(55, 55, 58));
        p.setColor(QPalette::ButtonText, Qt::white);
        p.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
        p.setColor(QPalette::ToolTipText, Qt::white);
        p.setColor(QPalette::Highlight, QColor(38, 79, 120));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(90, 160, 240));
        p.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 130, 130));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 130, 130));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(130, 130, 130));
    }
    qApp->setPalette(p);
    setEditorColorsDark(on);
    editor_->refreshColors();
    terminal_->refreshColors();
    checkNow();   // recolours the problem list and count
    QSettings().setValue("darkMode", on);

    // keep the menu item and the checkbox in step
    if (darkAct_->isChecked() != on) darkAct_->setChecked(on);
    if (darkBox_->isChecked() != on) darkBox_->setChecked(on);
}

void MainWindow::rememberFolder(const QString& path) {
    DynArray<QString> next;
    next.push(path);
    for (int i = 0; i < recent_.size() && next.size() < 8; i++)
        if (recent_[i] != path) next.push(recent_[i]);
    recent_ = std::move(next);
    QString list;
    for (int i = 0; i < recent_.size(); i++) list += recent_[i] + "\n";
    QSettings().setValue("recent", list);
}

void MainWindow::refreshRecent() {
    recentList_->clear();
    for (int i = 0; i < recent_.size(); i++) {
        QListWidgetItem* item = new QListWidgetItem(QDir::toNativeSeparators(recent_[i]), recentList_);
        item->setData(Qt::UserRole, recent_[i]);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    try {
        cat_.close();
    } catch (DbError&) {
    }
    e->accept();
}
