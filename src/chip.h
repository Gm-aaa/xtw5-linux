#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

struct Chip {
    QString name, manufacturer;
    quint32 size = 0, sector = 4096, jedec = 0, eraseMs = 120000;
    quint16 voltage = 3300, page = 256;
    quint8 type = 1, flags = 0, flags2 = 0;
    QString family() const;
    bool supported() const;
    void validate() const;
    QJsonObject json() const;
    static Chip fromJson(const QJsonObject &);
};
class ChipDatabase {
  public:
    QVector<Chip> chips;
    QString source;
    void loadJson(const QString &path);
    void saveJson(const QString &path) const;
    void importVendor(const QString &db, const QString &exe);
    static QVector<Chip> parseVendor(const QByteArray &plain);
    static Chip demoChip();
    QVector<int> detectionCandidates(quint32 deviceId) const;
};
[[noreturn]] void fail(const QString &message);
QByteArray readFile(const QString &path, qint64 maxBytes = 256LL * 1024 * 1024);
void saveFile(const QString &path, const QByteArray &data);
