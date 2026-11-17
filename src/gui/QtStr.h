#pragma once
#include <QString>
#include <QByteArray>
#include "ds/Str.h"

// Our Str <-> Qt's QString. Qt widgets only speak QString, so text crosses
// between the engine and the GUI through these two functions.
inline QString qs(const Str& s) { return QString::fromUtf8(s.c_str(), s.size()); }

inline Str toStr(const QString& q) {
    QByteArray bytes = q.toUtf8();
    return Str(bytes.constData(), (int)bytes.size());
}

// File paths go to fopen / the OS in the local 8-bit encoding, which on
// Windows is not UTF-8, so paths get their own pair of helpers.
inline Str toPath(const QString& p) {
    QByteArray bytes = p.toLocal8Bit();
    return Str(bytes.constData(), (int)bytes.size());
}

inline QString fromPath(const Str& p) { return QString::fromLocal8Bit(p.c_str(), p.size()); }
