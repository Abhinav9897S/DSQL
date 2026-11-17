#include "gui/LearnView.h"
#include "gui/DsInfo.h"
#include "gui/Theme.h"
#include <QUrl>

QString dsCardHtml(const char* key, bool withTry) {
    const ThemeColors& t = theme();
    const DsInfo& d = dsInfo(key);
    QColor c(d.color);
    QString html = QString("<a name='%1'></a>"
                           "<table width='100%' cellspacing='0' cellpadding='10' style='background-color:%2; border:1px solid %3'>"
                           "<tr><td>"
                           "<span style='color:%4; font-size:13pt'>&#9679;</span>&nbsp;<span style='font-size:12pt'><b>%5</b></span>"
                           "&nbsp;&nbsp;<span style='color:%6; font-family:monospace; font-size:8pt'>%7</span>"
                           "<p style='margin-top:6px; color:%8'><b>%9</b></p>"
                           "<p style='color:%10'>%11</p>")
                       .arg(key, hex(mix(c, t.panel2, 0.06)), hex(t.border), hex(c), d.name, hex(t.muted), d.file,
                            hex(t.text))
                       .arg(QString(d.role).toHtmlEscaped(), hex(t.text2), QString(d.how).toHtmlEscaped());
    html += "<table width='100%' cellspacing='0' cellpadding='3'>";
    for (int i = 0; i < 4 && d.cost[i][0]; i++)
        html += QString("<tr><td style='color:%1'>%2</td><td align='right' style='font-family:monospace; color:%3'>%4</td></tr>")
                    .arg(hex(t.text2), d.cost[i][0], hex(c), d.cost[i][1]);
    html += "</table>";
    if (withTry)
        html += QString("<p style='margin-top:8px'><span style='color:%1'>Try:</span> "
                        "<span style='font-family:monospace; background-color:%2'>&nbsp;%3&nbsp;</span>"
                        "&nbsp; <a href='try:%4' style='color:%5'>Insert into editor</a></p>")
                    .arg(hex(t.muted), hex(t.panel3), QString(d.tryIt).toHtmlEscaped(), key, hex(t.accent));
    return html + "</td></tr></table>";
}

LearnView::LearnView(QWidget* parent) : QTextBrowser(parent) {
    setOpenLinks(false);
    setOpenExternalLinks(false);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() != "try") return;
        emit insertSnippet(dsInfo(url.path().toUtf8().constData()).tryIt);
    });
    rebuild();
}

void LearnView::rebuild() {
    const ThemeColors& t = theme();
    QString html = QString("<p style='color:%1'>Every structure below is written by hand in C++. There is no "
                           "STL vector, map or list anywhere, and the build fails if one shows up.</p>")
                       .arg(hex(t.muted));
    for (int i = 0; i < DS_INFO_COUNT; i++) html += dsCardHtml(DS_INFO[i].key, true) + "<p></p>";
    setHtml(html);
}

void LearnView::focusOn(const QString& key) { scrollToAnchor(key); }
