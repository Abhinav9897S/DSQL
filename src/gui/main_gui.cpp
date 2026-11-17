// MiniSQL desktop app.
//
//   MiniSQL [folder] [--frames N] [--order N]
//
// --frames sets the buffer pool size (default 64 pages), --order the B+ tree
// order (default 4).

#include <QApplication>
#include <QFileInfo>
#include <cstring>
#include <cstdlib>
#include "gui/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("MiniSQL");
    QApplication::setOrganizationName("MiniSQL");

    int frames = 64, order = 4;
    QString folder;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--order") && i + 1 < argc) order = atoi(argv[++i]);
        else if (argv[i][0] != '-') folder = QString::fromLocal8Bit(argv[i]);
    }

    Catalog cat(frames, order);
    Executor exec(cat);
    MainWindow window(cat, exec);
    window.show();
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) window.openFolder(folder);
    return app.exec();
}
