#include "programmer.h"
#include "protocol.h"
#include <QElapsedTimer>
#include <QThread>
#include <QtEndian>

void Programmer::checkCancelled() {
    if (cancelled.load())
        fail("操作已取消；如已开始硬件擦除，芯片内部操作可能仍在进行");
}
void Programmer::connectDevice(bool sim, const Chip &c) {
    io.reset();
    cancelled = false;
    io = sim ? openSimulator(c) : openUsb(log);
    log(sim ? "模拟设备已连接（不会访问 USB）" : "USB 已连接，正在验证协议");
}
QByteArray Programmer::receiveFrame() {
    auto b = io->read(512);
    while (b.size() < 6)
        b += io->read(512);
    if (quint8(b[0]) != 2)
        fail("响应不是命令帧，停止操作以防接收流错位");
    const int total = 8 + qFromBigEndian<quint16>(b.constData() + 4);
    if (total > 8192)
        fail("响应过大");
    while (b.size() < total)
        b += io->read(total - b.size());
    return b;
}
QByteArray Programmer::command(quint8 cmd, const QByteArray &p) {
    checkCancelled();
    if (!io)
        fail("设备未连接");
    auto tx = Protocol::frame(cmd, p);
    if (trace)
        log("TX " + tx.toHex(' '));
    io->write(tx);
    if (!io->simulated())
        QThread::msleep(10);
    auto rx = receiveFrame();
    if (trace)
        log("RX " + rx.toHex(' '));
    // 39 config and 20 workflow control use a generic empty ACK, not a command echo.
    // 35 may also use an empty ACK; actual GD25Q64C tests return 35 with status 00.
    if ((cmd == 0x39 || cmd == 0x20 || cmd == 0x35) && rx == QByteArray::fromHex("0200000100000000"))
        return {};
    Protocol::Response r;
    try {
        r = Protocol::parse(rx, (cmd >= 0x10 && cmd <= 0x13) || cmd == 0x31 || cmd == 0x32 || cmd == 0x35);
    } catch (...) {
        if (!trace)
            log("解析失败 TX " + tx.toHex(' ') + " / RX " + rx.toHex(' '));
        throw;
    }
    if (r.command != cmd && r.command != (cmd | 0x80))
        fail(
            QString("响应命令不匹配：%1 → %2").arg(cmd, 2, 16, QChar('0')).arg(r.command, 2, 16, QChar('0')));
    return r.payload;
}
QStringList Programmer::info() {
    QStringList out;
    for (quint8 c = 0x10; c <= 0x13; c++) {
        auto r = command(c);
        if (r.size() > 64)
            fail("设备信息过长");
        out << QString::fromLatin1(r.split('\0').first());
    }
    return out;
}
void Programmer::range(const Chip &c, quint32 address, quint32 size) {
    c.validate();
    if (!c.supported())
        fail("该类型尚未完成适配");
    if (!size || address > c.size || size > c.size - address)
        fail("操作范围超出芯片容量");
}
void Programmer::configure(const Chip &c) {
    if (!c.supported())
        fail("当前只实现 SPI NOR 和 I²C EEPROM");
    command(0x39, Protocol::config(c));
    log(QString("配置：%1 / %2 字节 / %3 mV").arg(c.name).arg(c.size).arg(c.voltage));
}
quint32 Programmer::detect(bool allowMissing) {
    auto p = command(0x31);
    if (p.size() != 6)
        fail("芯片识别响应应为 6 字节");
    auto id = Protocol::get32(p);
    log(QString("芯片识别返回：%1；匹配 ID：%2；附加字节：%3")
            .arg(id, 8, 16, QChar('0'))
            .arg(id & 0xffffff, 6, 16, QChar('0'))
            .arg(QString(p.mid(4).toHex(' '))));
    const bool missing = (id >> 24) != 0x24 && ((id & 0xffffff) == 0 || (id & 0xffffff) == 0xffffff);
    if (allowMissing && missing) {
        log("编程器已连接，未识别到芯片；接好芯片后可点击自动识别，24 系列请手选型号");
        return 0;
    }
    if (missing)
        fail("未识别到有效芯片 ID；请检查接线和供电");
    return id;
}
void Programmer::speed(int code) {
    if (code < 1 || code > 5)
        fail("无效 SPI 速率");
    auto p = command(0x3a, QByteArray(1, char(code)));
    if (p.size() != 1 || quint8(p[0]) != code)
        fail("SPI 速率设置未确认");
}
void Programmer::control(quint16 value) {
    QByteArray p;
    Protocol::be16(p, value);
    command(0x20, p);
}
QByteArray Programmer::rawRead(int size) {
    QByteArray data;
    data.reserve(size);
    while (data.size() < size) {
        checkCancelled();
        data += io->read(size - data.size());
    }
    return data;
}
QByteArray Programmer::read(const Chip &c, quint8 socket, quint32 address, quint32 size) {
    range(c, address, size);
    if (socket > 1)
        fail("插座选择错误");
    control(1);
    QByteArray result;
    result.reserve(size);
    while (quint32(result.size()) < size) {
        checkCancelled();
        int n = qMin(quint32(4096), size - quint32(result.size()));
        auto p = Protocol::transferHeader(c, socket, address + result.size(), size, n);
        auto tx = Protocol::frame(0x34, p);
        if (trace)
            log("TX " + tx.toHex(' '));
        io->write(tx);
        if (!io->simulated())
            QThread::usleep(1); // Vendor 004240dc calls 0043aa9c(1), a microsecond delay.
        result += rawRead(n);
        progress(int(qint64(result.size()) * 100 / size), "读取");
    }
    control(0);
    return result;
}
void Programmer::verify(const Chip &c, quint8 socket, quint32 address, const QByteArray &expected) {
    if (expected.isEmpty())
        fail("没有可校验的数据");
    auto actual = read(c, socket, address, expected.size());
    if (actual != expected && !io->simulated()) {
        qsizetype mismatch = 0;
        while (mismatch < expected.size() && actual[mismatch] == expected[mismatch])
            ++mismatch;
        log(QString("首次回读不一致 @ 0x%1；重新配置并完整重读一次，仍须逐字节通过")
                .arg(address + mismatch, 8, 16, QChar('0')));
        checkCancelled();
        QThread::msleep(20);
        configure(c);
        actual = read(c, socket, address, expected.size());
    }
    for (qsizetype i = 0; i < expected.size(); i++)
        if (actual[i] != expected[i])
            fail(QString("校验失败 @ 0x%1：期望 %2，读取 %3")
                     .arg(address + i, 8, 16, QChar('0'))
                     .arg(quint8(expected[i]), 2, 16, QChar('0'))
                     .arg(quint8(actual[i]), 2, 16, QChar('0')));
    log(QString("逐字节校验通过：%1 字节").arg(expected.size()));
    progress(100, "校验通过");
}
void Programmer::blank(const Chip &c, quint8 socket, quint32 address, quint32 size) {
    auto b = read(c, socket, address, size);
    for (qsizetype i = 0; i < b.size(); i++)
        if (quint8(b[i]) != 255)
            fail(QString("芯片非空 @ 0x%1：%2")
                     .arg(address + i, 8, 16, QChar('0'))
                     .arg(quint8(b[i]), 2, 16, QChar('0')));
    log("空白检查通过");
    progress(100, "空白检查通过");
}
void Programmer::write(const Chip &c, quint8 socket, quint32 address, const QByteArray &data) {
    range(c, address, data.size());
    if (socket > 1 || address % c.page || data.size() % c.page)
        fail("写入范围必须按页对齐；可在缓冲区补齐到整页");
    control(1);
    const int chunk = qMin(quint32((1024 / c.page) * c.page), c.size);
    qsizetype offset = 0;
    while (offset < data.size()) {
        checkCancelled();
        const int n = qMin(qsizetype(chunk), data.size() - offset);
        auto p = Protocol::transferHeader(c, socket, address + offset, data.size(), n);
        p += data.mid(offset, n);
        auto ack = command(0x35, p);
        if (!ack.isEmpty() && ack != QByteArray(1, char(0)) && ack != QByteArray(1, char(0x55)))
            fail("设备写入应答状态异常");
        offset += n;
        if (c.type == 2 && !io->simulated())
            QThread::msleep(10);
        progress(int(offset * 100 / data.size()), "写入");
    }
    control(0);
    log("写入传输结束，开始强制回读校验");
    verify(c, socket, address, data);
}
void Programmer::erase(const Chip &c, quint8 socket) {
    range(c, 0, c.size);
    if (socket > 1)
        fail("插座选择错误");
    if (c.type == 2) {
        log("EEPROM 通过写入 FF 实现清空");
        write(c, socket, 0, QByteArray(c.size, char(0xff)));
        return;
    }
    control(1);
    QByteArray p;
    p.append(char(socket));
    p.append(char(c.type));
    p.append(char(0xff));
    auto status = command(0x32, p);
    if (status != QByteArray(1, char(0)) && status != QByteArray(1, char(0x55)))
        fail("设备未确认擦除准备");
    if (!io->simulated())
        QThread::msleep(100);
    p[2] = 0;
    status = command(0x32, p);
    if (status != QByteArray(1, char(0)) && status != QByteArray(1, char(0x55)))
        fail("设备未确认擦除启动");
    p[2] = 1;
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        checkCancelled();
        auto reply = command(0x32, p);
        if (reply.size() == 1 && quint8(reply[0]) == 0x55)
            break;
        if (reply != QByteArray(1, char(0)))
            fail("擦除状态响应异常");
        if (timer.elapsed() > qint64(c.eraseMs) + 30000)
            fail("等待擦除完成超时");
        progress(qMin(95, int(timer.elapsed() * 100 / c.eraseMs)), "等待芯片擦除");
        QThread::msleep(100);
    }
    control(0);
    log("擦除状态完成，开始全片空白检查");
    blank(c, socket, 0, c.size);
}
