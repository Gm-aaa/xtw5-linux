#pragma once
#include "chip.h"
#include <QByteArray>
namespace Protocol {
quint16 crc16(QByteArrayView data);
QByteArray frame(quint8 command, QByteArrayView payload = {});
struct Response {
    quint8 command;
    QByteArray payload;
    bool crcPresent;
};
Response parse(QByteArrayView bytes, bool allowDeviceZeroTrailer = false);
void be16(QByteArray &b, quint16 v);
void be32(QByteArray &b, quint32 v);
quint32 get32(QByteArrayView b);
QByteArray config(const Chip &c);
QByteArray transferHeader(const Chip &c, quint8 socket, quint32 address, quint32 total, quint16 count);
} // namespace Protocol
