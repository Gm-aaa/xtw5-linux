#include "protocol.h"
#include <QtEndian>
namespace Protocol {
quint16 crc16(QByteArrayView b) {
    quint16 c = 0xffff;
    for (char v : b) {
        c ^= quint8(v);
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ ((c & 1) ? 0xa001 : 0);
    }
    return c;
}
void be16(QByteArray &b, quint16 v) {
    b.append(char(v >> 8));
    b.append(char(v));
}
void be32(QByteArray &b, quint32 v) {
    be16(b, v >> 16);
    be16(b, v);
}
quint32 get32(QByteArrayView b) {
    if (b.size() < 4)
        fail("32 位字段截断");
    return qFromBigEndian<quint32>(b.data());
}
QByteArray frame(quint8 cmd, QByteArrayView p) {
    if (p.size() > 65535)
        fail("请求过大");
    QByteArray b;
    b.append(char(2));
    b.append(char(cmd));
    be16(b, 1);
    be16(b, p.size());
    b.append(p);
    be16(b, crc16(QByteArrayView(b).sliced(1)));
    return b;
}
Response parse(QByteArrayView b, bool allowDeviceZeroTrailer) {
    if (b.size() < 8 || quint8(b[0]) != 2)
        fail("设备响应帧头或长度不正确；请导出日志供协议验证");
    const int n = qFromBigEndian<quint16>(b.data() + 4);
    if (b.size() != n + 8)
        fail(QString("设备响应长度不匹配：收到 %1，预期 %2").arg(b.size()).arg(n + 8));
    const auto trailer = qFromBigEndian<quint16>(b.data() + 6 + n);
    const bool crcValid = trailer == crc16(b.sliced(1, 5 + n));
    // Observed on XTW5F-1.1.5.R1: information, detection, erase and write replies
    // have a zero trailer. Payload shapes and caller status checks remain enforced.
    const bool knownCommand = (quint8(b[1]) >= 0x10 && quint8(b[1]) <= 0x13) ||
                              (quint8(b[1]) == 0x31 && n == 6) ||
                              ((quint8(b[1]) == 0x32 || quint8(b[1]) == 0x35) && n == 1);
    const bool infoPadding = allowDeviceZeroTrailer && knownCommand && b[2] == 0 && b[3] == 1 && trailer == 0;
    if (!crcValid && !infoPadding)
        fail("设备响应 CRC 不匹配，操作已停止");
    return {quint8(b[1]), QByteArray(b.data() + 6, n), crcValid};
}
QByteArray config(const Chip &c) {
    c.validate();
    QByteArray p;
    p.append(char(c.type));
    p.append(char(c.voltage == 1800 ? 1 : c.voltage == 5000 ? 2 : 0));
    p.append(char(c.flags));
    p.append(char(c.flags2));
    be16(p, c.page);
    be32(p, c.size);
    return p;
}
QByteArray transferHeader(const Chip &c, quint8 socket, quint32 address, quint32 total, quint16 count) {
    if (socket > 1 || !count || address > c.size || count > c.size - address || !total || total > c.size)
        fail("传输范围不合法");
    QByteArray p;
    p.append(char(socket));
    p.append(char(c.type));
    be32(p, address);
    be32(p, total);
    be16(p, count);
    return p;
}
} // namespace Protocol
