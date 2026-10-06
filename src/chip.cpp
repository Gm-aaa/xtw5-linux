#include "chip.h"

QVector<int> ChipDatabase::detectionCandidates(quint32 deviceId) const {
    QVector<int> matches;
    const auto family = deviceId >> 24;
    const auto id = deviceId & 0xffffff;
    for (int i = 0; i < chips.size(); ++i) {
        const auto &c = chips[i];
        // Vendor caller 00414685 masks the ID before 0043fc2c; only 24 is special.
        if ((family == 0x24 && c.type == 2) || (family != 0x24 && id != 0 && id != 0xffffff && c.jedec == id))
            matches.append(i);
    }
    return matches;
}
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>

void fail(const QString &s) {
    throw std::runtime_error(s.toStdString());
}
QByteArray readFile(const QString &path, qint64 max) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(f.errorString());
    if (f.size() > max)
        fail("文件超过允许大小");
    // size() can be zero for sequential/proc files, and ordinary files may grow.
    auto b = f.read(max + 1);
    if (b.size() > max)
        fail("文件超过允许大小");
    if (f.error() != QFileDevice::NoError)
        fail(f.errorString());
    return b;
}
void saveFile(const QString &path, const QByteArray &b) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size() || !f.commit())
        fail(f.errorString());
}
QString Chip::family() const {
    switch (type) {
    case 1:
        return "SPI NOR / 25";
    case 2:
        return "I²C EEPROM / 24";
    case 4:
        return "AT45 DataFlash";
    case 8:
        return "SPI NAND";
    default:
        return QString("类型 %1").arg(type);
    }
}
bool Chip::supported() const {
    return (type == 1 || type == 2) && size > 0 && size <= 128U * 1024 * 1024 && page > 0 && page <= 1024 &&
           (voltage == 1800 || voltage == 3300 || voltage == 5000);
}
void Chip::validate() const {
    if (name.isEmpty() || manufacturer.isEmpty() || !size || size > 256U * 1024 * 1024 || !page ||
        page > 8192 || size % page || (voltage < 1000 || voltage > 5500) || !type || type > 8 || !eraseMs ||
        eraseMs > 3600000)
        fail("芯片参数不合法：" + name);
}
QJsonObject Chip::json() const {
    return {{"name", name},           {"manufacturer", manufacturer},
            {"size", double(size)},   {"sector", double(sector)},
            {"jedec", double(jedec)}, {"erase_ms", double(eraseMs)},
            {"voltage_mv", voltage},  {"page", page},
            {"type", type},           {"flags", flags},
            {"flags2", flags2}};
}
Chip Chip::fromJson(const QJsonObject &j) {
    Chip c;
    c.name = j["name"].toString();
    c.manufacturer = j["manufacturer"].toString();
    auto number = [&](const char *key, quint32 max) -> quint32 {
        const auto v = j[key];
        const double d = v.toDouble(-1);
        if (!v.isDouble() || d < 0 || d > max || double(quint32(d)) != d)
            fail(QString("无效参数：%1").arg(key));
        return quint32(d);
    };
    c.size = number("size", 256U * 1024 * 1024);
    c.sector = number("sector", 256U * 1024 * 1024);
    c.jedec = number("jedec", 0xffffffffU);
    c.eraseMs = number("erase_ms", 3600000);
    c.voltage = number("voltage_mv", 65535);
    c.page = number("page", 8192);
    c.type = number("type", 8);
    c.flags = number("flags", 255);
    c.flags2 = number("flags2", 255);
    c.validate();
    return c;
}
void ChipDatabase::loadJson(const QString &path) {
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(readFile(path, 16 * 1024 * 1024), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject() || doc["schema"].toInt() != 1 ||
        !doc["chips"].isArray())
        fail("芯片库 JSON 格式错误");
    QVector<Chip> parsed;
    const auto entries = doc["chips"].toArray();
    if (entries.isEmpty() || entries.size() > 20000)
        fail("芯片条目数量不合法");
    for (auto entry : entries)
        parsed.append(Chip::fromJson(entry.toObject()));
    chips = parsed;
    source = doc["source"].toString();
}
void ChipDatabase::saveJson(const QString &path) const {
    QJsonArray a;
    for (const auto &c : chips)
        a.append(c.json());
    saveFile(path, QJsonDocument(QJsonObject{{"schema", 1}, {"source", source}, {"chips", a}}).toJson());
}
QVector<Chip> ChipDatabase::parseVendor(const QByteArray &b) {
    if (b.size() < 96 || b.left(4) != QByteArray::fromHex("55aaaa55"))
        fail("芯片库解码校验失败");
    const auto n = qFromLittleEndian<quint32>(b.constData() + 4);
    if (n == 0 || n > 20000 || b.size() < 96 + qint64(n) * 96)
        fail("芯片库截断或条目数错误");
    QVector<Chip> out;
    for (quint32 i = 0; i < n; i++) {
        const auto r = b.mid(96 + qint64(i) * 96, 96);
        auto u32 = [&](int o) { return qFromLittleEndian<quint32>(r.constData() + o); };
        auto u16 = [&](int o) { return qFromLittleEndian<quint16>(r.constData() + o); };
        auto text = [&](int o) { return QString::fromLatin1(r.mid(o, 16).split('\0').first()).trimmed(); };
        Chip c;
        c.name = text(0);
        c.manufacturer = text(16);
        c.size = u32(32);
        c.sector = u32(36);
        c.jedec = u32(48);
        c.voltage = u16(52);
        c.page = u16(54);
        c.type = quint8(r[56]);
        c.flags = quint8(r[57]);
        c.flags2 = quint8(r[58]);
        c.eraseMs = u32(60);
        c.validate();
        out.append(c);
    }
    return out;
}
void ChipDatabase::importVendor(const QString &db, const QString &exe) {
    const auto program = readFile(exe, 32 * 1024 * 1024);
    if (QCryptographicHash::hash(program, QCryptographicHash::Sha256).toHex() !=
        "fe9768d278ce56a4dcd04689b86390b9ae43889956bf4788f0ef7283ad2d1faf")
        fail("目前只验证了 20250524 版 XTW-5.exe；请选择已解包的主程序，不是安装包");
    const auto encrypted = readFile(db, 4 * 1024 * 1024);
    if (encrypted.isEmpty() || encrypted.size() % 8)
        fail("芯片库长度错误");
    const auto key = program.mid(0x7dd034 - 0x7cb000 + 0x3c9e00, 24);
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);
    if (!ctx ||
        EVP_DecryptInit_ex(ctx.get(), EVP_des_ede3_ecb(), nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()), nullptr) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1)
        fail("不能初始化芯片库解码器");
    QByteArray plain(encrypted.size() + 8, '\0');
    int n = 0, last = 0;
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(plain.data()), &n,
                          reinterpret_cast<const unsigned char *>(encrypted.constData()),
                          int(encrypted.size())) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(plain.data()) + n, &last) != 1)
        fail("芯片库解码失败");
    plain.resize(n + last);
    auto parsed = parseVendor(plain);
    chips = parsed;
    source = "XTW-5 20250524 · 用户导入";
}
Chip ChipDatabase::demoChip() {
    Chip c;
    c.name = "DEMO-25-1M";
    c.manufacturer = "模拟设备";
    c.size = 128 * 1024;
    c.jedec = 0xef4011;
    c.eraseMs = 1000;
    return c;
}
