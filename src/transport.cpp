#include "transport.h"
#include "protocol.h"
#include <QThread>
#include <QtEndian>
#include <libusb.h>

namespace {
class Usb final : public Transport {
    libusb_context *ctx = nullptr;
    libusb_device_handle *handle = nullptr;
    bool claimed = false;

  public:
    ~Usb() override {
        if (claimed)
            libusb_release_interface(handle, 0);
        if (handle)
            libusb_close(handle);
        if (ctx)
            libusb_exit(ctx);
    }
    void open(const Log &log) {
        int r = libusb_init(&ctx);
        if (r)
            fail(libusb_error_name(r));
        libusb_device **devices = nullptr;
        const auto n = libusb_get_device_list(ctx, &devices);
        if (n < 0)
            fail("USB 枚举失败");
        int found = 0;
        libusb_device *target = nullptr;
        for (ssize_t i = 0; i < n; i++) {
            libusb_device_descriptor d{};
            if (!libusb_get_device_descriptor(devices[i], &d) && d.idVendor == 0x1a86 &&
                d.idProduct == 0x1001) {
                target = devices[i];
                ++found;
            }
        }
        if (found != 1) {
            libusb_free_device_list(devices, 1);
            fail(QString("需要连接一台 XTW-5，当前发现 %1 台").arg(found));
        }
        r = libusb_open(target, &handle);
        libusb_free_device_list(devices, 1);
        if (r == LIBUSB_ERROR_ACCESS)
            fail(
                "USB 访问被拒绝。请安装随软件提供的 udev 规则，然后重新插拔编程器（无需以 root 运行 GUI）。");
        if (r)
            fail(QString("打开 USB 失败：%1").arg(libusb_error_name(r)));
        libusb_config_descriptor *cfg = nullptr;
        r = libusb_get_active_config_descriptor(libusb_get_device(handle), &cfg);
        if (r)
            fail("无法读取 USB 配置");
        bool valid = false;
        for (int i = 0; i < cfg->bNumInterfaces; i++)
            for (int j = 0; j < cfg->interface[i].num_altsetting; j++) {
                const auto &d = cfg->interface[i].altsetting[j];
                if (d.bInterfaceNumber != 0 || d.bAlternateSetting != 0)
                    continue;
                int flags = 0;
                for (int k = 0; k < d.bNumEndpoints; k++) {
                    const auto &e = d.endpoint[k];
                    if ((e.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK)
                        continue;
                    if (e.bEndpointAddress == 2)
                        flags |= 1;
                    if (e.bEndpointAddress == 0x81)
                        flags |= 2;
                }
                valid |= flags == 3;
            }
        libusb_free_config_descriptor(cfg);
        if (!valid)
            fail("USB 接口布局与 XTW-5 不符");
        r = libusb_claim_interface(handle, 0);
        if (r)
            fail(QString("无法占用接口 0：%1").arg(libusb_error_name(r)));
        claimed = true;
        log("已打开 1a86:1001 · 接口 0 · OUT 02 / IN 81");
    }
    bool simulated() const override { return false; }
    void write(const QByteArray &b) override {
        int n = 0;
        int r = libusb_bulk_transfer(handle, 2,
                                     reinterpret_cast<unsigned char *>(const_cast<char *>(b.constData())),
                                     int(b.size()), &n, 2000);
        if (r || n != b.size())
            fail(QString("USB 发送失败：%1（%2/%3 字节）").arg(libusb_error_name(r)).arg(n).arg(b.size()));
    }
    QByteArray read(int max, int timeout) override {
        QByteArray b(max, '\0');
        int n = 0;
        int r =
            libusb_bulk_transfer(handle, 0x81, reinterpret_cast<unsigned char *>(b.data()), max, &n, timeout);
        if (r)
            fail(QString("USB 接收失败：%1（收到 %2 字节）").arg(libusb_error_name(r)).arg(n));
        if (n == 0)
            fail("设备返回空数据");
        b.resize(n);
        return b;
    }
};
class Simulator final : public Transport {
    Chip chip;
    QByteArray memory, pending;
    bool erasePrepared = false;
    int speed = 3;

  public:
    explicit Simulator(const Chip &c) : chip(c), memory(c.size, char(0xff)) {}
    bool simulated() const override { return true; }
    void write(const QByteArray &wire) override {
        if (!pending.isEmpty())
            fail("模拟器：上一条响应尚未读取");
        auto r = Protocol::parse(wire);
        auto p = r.payload;
        QByteArray result;
        auto need = [&](int n) {
            if (p.size() != n)
                fail("模拟器：参数长度错误");
        };
        switch (r.command) {
        case 0x10:
            need(0);
            result = "XTW-5 DEMO";
            break;
        case 0x11:
            need(0);
            result = "SIMULATOR-1";
            break;
        case 0x12:
            need(0);
            result = "VIRTUAL-0001";
            break;
        case 0x13:
            need(0);
            result = "Offline simulation";
            break;
        case 0x20:
            need(2);
            result.append(char(0x55));
            break;
        case 0x39:
            need(10);
            if (Protocol::get32(QByteArrayView(p).sliced(6)) != chip.size || quint8(p[0]) != chip.type)
                fail("模拟芯片容量或类型不匹配");
            result.append(char(0x55));
            break;
        case 0x31:
            need(0);
            Protocol::be32(result, (chip.type == 2 ? 0x24000000 : 0) | chip.jedec);
            result.append(char(0));
            result.append(char(0));
            break;
        case 0x3a:
            need(1);
            if (quint8(p[0]) == 10) {
            } else if (p[0] >= 1 && p[0] <= 5)
                speed = p[0];
            else
                fail("模拟器：无效速率");
            result.append(char(speed));
            break;
        case 0x32:
            need(3);
            if (quint8(p[2]) == 255)
                erasePrepared = true;
            else if (p[2] == 0) {
                if (!erasePrepared)
                    fail("未准备擦除");
                memory.fill(char(0xff));
                erasePrepared = false;
            } else if (p[2] != 1)
                fail("无效擦除命令");
            result.append(p[2] == 1 ? char(0x55) : char(0));
            break;
        case 0x34:
        case 0x35: {
            if (p.size() < 12)
                fail("模拟器：传输头截断");
            auto address = Protocol::get32(QByteArrayView(p).sliced(2));
            auto size = qFromBigEndian<quint16>(p.constData() + 10);
            if (!size || address > quint32(memory.size()) || size > quint32(memory.size()) - address)
                fail("模拟器：越界");
            if (r.command == 0x34) {
                need(12);
                pending = memory.mid(address, size);
                return;
            }
            need(12 + size);
            if (address % chip.page || size % chip.page)
                fail("模拟器：写入未按页对齐");
            if (chip.type == 1)
                for (int i = 0; i < size; i++)
                    memory[address + i] = char(quint8(memory[address + i]) & quint8(p[12 + i]));
            else
                memory.replace(address, size, p.mid(12));
            result.append(char(0x55));
            break;
        }
        default:
            fail("模拟器不支持该命令");
        }
        if (r.command == 0x20 || r.command == 0x39)
            pending = QByteArray::fromHex("0200000100000000");
        else if (r.command == 0x35)
            pending = QByteArray::fromHex("023500010001000000");
        else if (r.command == 0x32) {
            pending = Protocol::frame(r.command, result);
            pending[pending.size() - 2] = 0;
            pending[pending.size() - 1] = 0;
        } else
            pending = Protocol::frame(r.command, result);
    }
    QByteArray read(int maximum, int) override {
        if (pending.isEmpty())
            fail("模拟器：无响应");
        auto b = pending.left(maximum);
        pending.remove(0, b.size());
        QThread::usleep(200);
        return b;
    }
};
} // namespace
std::unique_ptr<Transport> openUsb(const Log &log) {
    auto p = std::make_unique<Usb>();
    p->open(log);
    return p;
}
std::unique_ptr<Transport> openSimulator(const Chip &chip) {
    chip.validate();
    if (chip.size > 16 * 1024 * 1024)
        fail("模拟芯片容量限制为 16 MiB");
    return std::make_unique<Simulator>(chip);
}
QStringList listUsbDevices() {
    QStringList result;
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx))
        fail("USB 枚举初始化失败");
    libusb_device **devs = nullptr;
    const auto n = libusb_get_device_list(ctx, &devs);
    if (n < 0) {
        libusb_exit(ctx);
        fail("USB 枚举失败");
    }
    for (ssize_t i = 0; i < n; i++) {
        libusb_device_descriptor d{};
        if (!libusb_get_device_descriptor(devs[i], &d) && d.idVendor == 0x1a86 && d.idProduct == 0x1001)
            result << QString("XTW-5 · 总线 %1 / 设备 %2")
                          .arg(libusb_get_bus_number(devs[i]))
                          .arg(libusb_get_device_address(devs[i]));
    }
    if (devs)
        libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return result;
}
