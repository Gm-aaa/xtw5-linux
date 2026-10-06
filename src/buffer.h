#pragma once
#include <QByteArray>
#include <QString>
namespace Buffer {
QByteArray load(const QString &path);
void save(const QString &path, const QByteArray &);
QByteArray parseIntelHex(const QByteArray &);
QByteArray intelHex(const QByteArray &);
QString sha256(const QByteArray &);
} // namespace Buffer
