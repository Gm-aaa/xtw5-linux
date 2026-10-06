#pragma once
#include "transport.h"
#include <atomic>
using Progress = std::function<void(int, const QString &)>;
class Programmer {
    std::unique_ptr<Transport> io;
    QByteArray receiveFrame();
    void checkCancelled();
    void range(const Chip &, quint32 address, quint32 size);
    QByteArray rawRead(int size);

  public:
    std::atomic_bool cancelled{false};
    Log log = [](const QString &) {};
    Progress progress = [](int, const QString &) {};
    bool trace = false;
    bool connected() const { return bool(io); }
    bool simulated() const { return io && io->simulated(); }
    void connectDevice(bool simulation, const Chip &demo);
    void disconnect() { io.reset(); }
    void useTransport(std::unique_ptr<Transport> t) { io = std::move(t); }
    QByteArray command(quint8 cmd, const QByteArray &payload = {});
    QStringList info();
    void configure(const Chip &chip);
    quint32 detect(bool allowMissing = false);
    void speed(int code);
    void control(quint16 value);
    QByteArray read(const Chip &, quint8 socket, quint32 address, quint32 size);
    void write(const Chip &, quint8 socket, quint32 address, const QByteArray &);
    void verify(const Chip &, quint8 socket, quint32 address, const QByteArray &);
    void blank(const Chip &, quint8 socket, quint32 address, quint32 size);
    void erase(const Chip &, quint8 socket);
};
