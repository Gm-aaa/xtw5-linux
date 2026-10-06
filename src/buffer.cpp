#include "buffer.h"
#include "chip.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QMap>
namespace Buffer {
QString sha256(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex();
}
QByteArray parseIntelHex(const QByteArray &text) {
    QMap<quint32, QByteArray> bytes;
    quint32 base = 0;
    bool eof = false;
    for (auto line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (eof)
            fail("HEX 在 EOF 后仍有记录");
        if (!line.startsWith(':') || line.size() % 2 != 1)
            fail("Intel HEX 行格式错误");
        for (char c : line.mid(1))
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                fail("HEX 含非十六进制字符");
        auto r = QByteArray::fromHex(line.mid(1));
        if (r.size() < 5 || r.size() != quint8(r[0]) + 5)
            fail("HEX 长度错误");
        quint8 sum = 0;
        for (char c : r)
            sum += quint8(c);
        if (sum)
            fail("HEX 校验和错误");
        const auto count = quint8(r[0]);
        const quint32 address = (quint8(r[1]) << 8) | quint8(r[2]);
        const auto type = quint8(r[3]);
        if (type == 0) {
            if (quint64(base) + address + count > 256U * 1024 * 1024)
                fail("HEX 地址超过 256 MiB");
            if (count) {
                const quint32 a = base + address;
                auto next = bytes.lowerBound(a);
                if (next != bytes.end() && next.key() < a + count)
                    fail("HEX 存在重叠数据");
                if (next != bytes.begin()) {
                    auto prev = std::prev(next);
                    if (prev.key() + prev.value().size() > a)
                        fail("HEX 存在重叠数据");
                }
                bytes.insert(a, r.mid(4, count));
            }
        } else if (type == 1) {
            if (count || address)
                fail("HEX EOF 格式错误");
            eof = true;
        } else if (type == 2 || type == 4) {
            if (count != 2 || address)
                fail("HEX 扩展地址格式错误");
            base = (quint32(quint8(r[4])) << 8 | quint32(quint8(r[5]))) << (type == 4 ? 16 : 4);
        } else if (type == 3 || type == 5) {
            if (count != 4 || address)
                fail("HEX 入口记录错误");
        } else
            fail("不支持的 Intel HEX 记录");
    }
    if (!eof || bytes.isEmpty())
        fail("HEX 缺少 EOF 或数据");
    QByteArray out(bytes.lastKey() + bytes.last().size(), char(0xff));
    for (auto i = bytes.cbegin(); i != bytes.cend(); ++i)
        out.replace(i.key(), i.value().size(), i.value());
    return out;
}
QByteArray intelHex(const QByteArray &data) {
    QByteArray out;
    auto line = [&](quint8 type, quint16 address, const QByteArray &p) {
        QByteArray b;
        b.append(char(p.size()));
        b.append(char(address >> 8));
        b.append(char(address));
        b.append(char(type));
        b += p;
        quint8 sum = 0;
        for (char c : b)
            sum += quint8(c);
        b.append(char(-sum));
        out += ':';
        out += b.toHex().toUpper();
        out += '\n';
    };
    for (qsizetype a = 0; a < data.size(); a += 16) {
        if (a % 65536 == 0) {
            QByteArray p;
            p.append(char(a >> 24));
            p.append(char(a >> 16));
            line(4, 0, p);
        }
        line(0, quint16(a), data.mid(a, 16));
    }
    line(1, 0, {});
    return out;
}
QByteArray load(const QString &path) {
    auto b = readFile(path);
    if (QFileInfo(path).suffix().compare("hex", Qt::CaseInsensitive) == 0)
        return parseIntelHex(b);
    return b;
}
void save(const QString &path, const QByteArray &data) {
    saveFile(path, QFileInfo(path).suffix().compare("hex", Qt::CaseInsensitive) == 0 ? intelHex(data) : data);
}
} // namespace Buffer
