#pragma once
#include "chip.h"
#include <functional>
#include <memory>
using Log = std::function<void(const QString &)>;
class Transport {
  public:
    virtual ~Transport() = default;
    virtual void write(const QByteArray &) = 0;
    virtual QByteArray read(int maximum, int timeoutMs = 2000) = 0;
    virtual bool simulated() const = 0;
};
std::unique_ptr<Transport> openUsb(const Log &);
std::unique_ptr<Transport> openSimulator(const Chip &chip);
QStringList listUsbDevices();
