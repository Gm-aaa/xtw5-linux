#include "buffer.h"
#include "programmer.h"
#include "protocol.h"
#include <QTemporaryDir>
#include <QtTest>

class Fragmented final : public Transport {
  public:
    bool hardware = false;
    QByteArray reply = QByteArray::fromHex("02100001000003b4");
    QByteArray sent;
    bool simulated() const override { return !hardware; }
    void write(const QByteArray &b) override { sent = b; }
    QByteArray read(int, int) override {
        if (reply.isEmpty())
            fail("fixture exhausted");
        auto b = reply.left(1);
        reply.remove(0, 1);
        return b;
    }
};
class CoreTest : public QObject {
    Q_OBJECT
  private slots:
    void fixedProtocolVectors() {
        QCOMPARE(Protocol::crc16("123456789"), quint16(0x4b37));
        QCOMPARE(Protocol::frame(0x10).toHex(), QByteArray("02100001000003b4"));
        QCOMPARE(Protocol::frame(0x12).toHex(), QByteArray("021200010000c3cd"));
        auto c = ChipDatabase::demoChip();
        c.size = 0x1000000;
        c.voltage = 1800;
        c.flags = 4;
        QCOMPARE(Protocol::config(c).toHex(), QByteArray("01010400010001000000"));
        QCOMPARE(Protocol::transferHeader(c, 0, 0x123400, 0x1000, 256).toHex(),
                 QByteArray("000100123400000010000100"));
    }
    void rejectBadFrames() {
        auto f = Protocol::frame(0x10, "hello");
        QCOMPARE(Protocol::parse(f).payload, QByteArray("hello"));
        f[6] ^= 1;
        QVERIFY_EXCEPTION_THROWN(Protocol::parse(f), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Protocol::parse(f.left(5)), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Protocol::parse(Protocol::frame(0x10) + "x"), std::runtime_error);
    }
    void fragmentedReply() {
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        auto raw = fixture.get();
        p.useTransport(std::move(fixture));
        QCOMPARE(p.command(0x10), QByteArray());
        QCOMPARE(raw->sent.toHex(), QByteArray("02100001000003b4"));
    }
    void genericConfigurationAndControlAck() {
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        auto raw = fixture.get();
        p.useTransport(std::move(fixture));
        const auto ack = QByteArray::fromHex("0200000100000000");
        raw->reply = ack;
        p.configure(ChipDatabase::demoChip());
        QCOMPARE(raw->sent, Protocol::frame(0x39, Protocol::config(ChipDatabase::demoChip())));
        raw->reply = ack;
        p.control(1);
        QCOMPARE(raw->sent, Protocol::frame(0x20, QByteArray::fromHex("0001")));
        raw->reply = ack;
        p.control(0);
        QCOMPARE(raw->sent, Protocol::frame(0x20, QByteArray::fromHex("0000")));
        raw->reply = ack;
        QCOMPARE(p.command(0x35), QByteArray());
        for (quint8 cmd : {0x10, 0x31, 0x32, 0x3a}) {
            raw->reply = ack;
            QVERIFY_EXCEPTION_THROWN(p.command(cmd), std::runtime_error);
        }
        for (int offset : {1, 2, 3, 6, 7}) {
            auto corrupt = ack;
            corrupt[offset] ^= 1;
            raw->reply = corrupt;
            QVERIFY_EXCEPTION_THROWN(p.configure(ChipDatabase::demoChip()), std::runtime_error);
        }
    }
    void realDeviceInfoResponses() {
        const QList<QByteArray> captures = {
            QByteArray::fromHex("02100001000e58545735462d312e312e352e52310000"),
            QByteArray::fromHex("021100010011585457352d302e312e30002e52310000000000"),
            QByteArray::fromHex("02120001000f3030302d3030302d3030302d3030300000"),
            QByteArray::fromHex("0213000100055854572d350000")};
        const QStringList expected = {"XTW5F-1.1.5.R1", "XTW5-0.1.0", "000-000-000-000", "XTW-5"};
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        fixture->reply.clear();
        for (const auto &packet : captures) {
            fixture->reply += packet;
            QVERIFY(!Protocol::parse(packet, true).crcPresent);
            QVERIFY_EXCEPTION_THROWN(Protocol::parse(packet), std::runtime_error);
            auto bad = packet;
            bad[bad.size() - 1] = 1;
            QVERIFY_EXCEPTION_THROWN(Protocol::parse(bad, true), std::runtime_error);
            bad = packet;
            bad[1] = 0x35;
            QVERIFY_EXCEPTION_THROWN(Protocol::parse(bad, true), std::runtime_error);
            bad = packet;
            bad[3] = 2;
            QVERIFY_EXCEPTION_THROWN(Protocol::parse(bad, true), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(Protocol::parse(packet.chopped(1), true), std::runtime_error);
        }
        p.useTransport(std::move(fixture));
        QCOMPARE(p.info(), expected);
    }
    void norLifecycle() {
        auto c = ChipDatabase::demoChip();
        Programmer p;
        p.connectDevice(true, c);
        QCOMPARE(p.info().size(), 4);
        p.configure(c);
        QCOMPARE(p.detect() & 0xffffff, c.jedec);
        p.speed(5);
        p.blank(c, 0, 0, c.size);
        QByteArray data(4096, 0);
        for (int i = 0; i < data.size(); i++)
            data[i] = char(i * 17 + 5);
        p.write(c, 0, 256, data);
        QCOMPARE(p.read(c, 0, 256, data.size()), data);
        p.verify(c, 0, 256, data);
        QVERIFY_EXCEPTION_THROWN(p.blank(c, 0, 0, c.size), std::runtime_error);
        auto different = data;
        different[7] ^= 1;
        QVERIFY_EXCEPTION_THROWN(p.verify(c, 0, 256, different), std::runtime_error);
        // NOR cannot turn zero bits back to one without an erase: mandatory verify must fail.
        QVERIFY_EXCEPTION_THROWN(p.write(c, 0, 256, QByteArray(data.size(), char(0xff))), std::runtime_error);
        p.erase(c, 0);
        p.blank(c, 0, 0, c.size);
        p.write(c, 0, 0, data);
        QCOMPARE(p.read(c, 0, 0, data.size()), data);
    }
    void detectWithoutConfiguration() {
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        auto raw = fixture.get();
        // Synthetic 31 reply in the vendor response layout, not a hardware capture.
        fixture->reply = QByteArray::fromHex("02310001000625ef401100000000");
        p.useTransport(std::move(fixture));
        QCOMPARE(p.detect(), quint32(0x25ef4011));
        QCOMPARE(raw->sent, Protocol::frame(0x31));
        for (quint32 prefix : {0U, 0x01000000U, 0x25000000U, 0xff000000U}) {
            QByteArray payload;
            Protocol::be32(payload, prefix | 0xef4011);
            payload.append(QByteArray(2, '\0'));
            raw->reply = Protocol::frame(0x31, payload);
            QCOMPARE(p.detect(), prefix | 0xef4011);
        }
        raw->reply = Protocol::frame(0x31, QByteArray::fromHex("240000000000"));
        QCOMPARE(p.detect(), quint32(0x24000000));
        for (const auto &payload : {"250000000000", "25ffffff0000", "000000000000", "25ef4011"}) {
            raw->reply = Protocol::frame(0x31, QByteArray::fromHex(payload));
            QVERIFY_EXCEPTION_THROWN(p.detect(), std::runtime_error);
        }
        raw->reply = Protocol::frame(0x32, QByteArray::fromHex("25ef40110000"));
        QVERIFY_EXCEPTION_THROWN(p.detect(), std::runtime_error);
    }
    void detectionMatching() {
        ChipDatabase db;
        auto a = ChipDatabase::demoChip();
        auto b = a;
        b.name = "Same ID, different model";
        auto e = a;
        e.type = 2;
        e.jedec = 0;
        db.chips = {a, b, e};
        QCOMPARE(db.detectionCandidates(0x25000000 | a.jedec), QVector<int>({0, 1}));
        QCOMPARE(db.detectionCandidates(a.jedec), QVector<int>({0, 1}));
        QCOMPARE(db.detectionCandidates(0x01000000 | a.jedec), QVector<int>({0, 1}));
        QCOMPARE(db.detectionCandidates(0xff000000 | a.jedec), QVector<int>({0, 1}));
        QCOMPARE(db.detectionCandidates(0x24000000), QVector<int>({2}));
        QVERIFY(db.detectionCandidates(0x25abcdef).isEmpty());
        QVERIFY(db.detectionCandidates(0x25ffffff).isEmpty());
        QVERIFY(db.detectionCandidates(0).isEmpty());
    }
    void eepromLifecycle() {
        auto c = ChipDatabase::demoChip();
        c.type = 2;
        c.size = 256;
        c.page = 8;
        Programmer p;
        p.connectDevice(true, c);
        p.configure(c);
        p.write(c, 0, 0, QByteArray(256, char(0x2a)));
        p.write(c, 0, 0, QByteArray(256, char(0xa5)));
        p.erase(c, 0);
        p.blank(c, 0, 0, 256);
    }
    void customPageWriteAndLastByteMismatch() {
        auto c = ChipDatabase::demoChip();
        c.size = 3072;
        c.page = 768;
        Programmer p;
        p.connectDevice(true, c);
        p.configure(c);
        QByteArray data(1536, char(0xa5));
        p.write(c, 0, 768, data);
        QCOMPARE(p.read(c, 0, 768, data.size()), data);
        data[data.size() - 1] ^= 1;
        try {
            p.verify(c, 0, 768, data);
            QFAIL("Verification incorrectly accepted changed final byte");
        } catch (const std::exception &e) {
            QVERIFY(QString::fromUtf8(e.what()).contains("0x000008ff"));
        }
    }
    void cancelledWritePreservesUnwrittenRegion() {
        const auto c = ChipDatabase::demoChip();
        Programmer p;
        p.connectDevice(true, c);
        p.configure(c);
        p.progress = [&](int, const QString &phase) {
            if (phase == "写入")
                p.cancelled = true;
        };
        QVERIFY_EXCEPTION_THROWN(p.write(c, 0, 0, QByteArray(4096, char(0x5a))), std::runtime_error);
        p.cancelled = false;
        p.progress = [](int, const QString &) {};
        QCOMPARE(p.read(c, 0, 0, 1024), QByteArray(1024, char(0x5a)));
        QCOMPARE(p.read(c, 0, 1024, 3072), QByteArray(3072, char(0xff)));
    }
    void hardwareVerifyRetryIsBoundedAndExact() {
        auto c = ChipDatabase::demoChip();
        c.size = 256;
        const auto ack = QByteArray::fromHex("0200000100000000");
        const QByteArray good(256, char(0xa5));
        auto bad = good;
        bad[255] ^= 1;
        for (bool recovers : {true, false}) {
            Programmer p;
            auto fixture = std::make_unique<Fragmented>();
            fixture->hardware = true;
            fixture->reply = ack + bad + ack + ack + ack + (recovers ? good : bad) + ack;
            auto raw = fixture.get();
            p.useTransport(std::move(fixture));
            QStringList logs;
            p.log = [&](const QString &s) { logs << s; };
            if (recovers)
                p.verify(c, 0, 0, good);
            else
                QVERIFY_EXCEPTION_THROWN(p.verify(c, 0, 0, good), std::runtime_error);
            QVERIFY(raw->reply.isEmpty());
            QVERIFY(logs.join('\n').contains("首次回读不一致"));
            QCOMPARE(logs.join('\n').contains("逐字节校验通过"), recovers);
        }
    }
    void realEraseStatusSequence() {
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        const auto ack = QByteArray::fromHex("0200000100000000");
        const auto pending = QByteArray::fromHex("023200010001000000");
        const auto done = QByteArray::fromHex("023200010001550000");
        fixture->reply =
            ack + pending + pending + pending + done + ack + ack + QByteArray(256, char(0xff)) + ack;
        p.useTransport(std::move(fixture));
        auto c = ChipDatabase::demoChip();
        c.size = 256;
        p.erase(c, 0);
    }
    void rejectEraseAndWriteStatuses() {
        Programmer p;
        auto fixture = std::make_unique<Fragmented>();
        auto raw = fixture.get();
        p.useTransport(std::move(fixture));
        const auto controlAck = QByteArray::fromHex("0200000100000000");
        const auto writeFailure = QByteArray::fromHex("023500010001aa0000");
        raw->reply = controlAck + writeFailure;
        QVERIFY_EXCEPTION_THROWN(p.write(ChipDatabase::demoChip(), 0, 0, QByteArray(256, 'x')),
                                 std::runtime_error);
        QCOMPARE(quint8(raw->sent[1]), quint8(0x35));
        raw->reply = controlAck + QByteArray::fromHex("023200010001aa0000");
        QVERIFY_EXCEPTION_THROWN(p.erase(ChipDatabase::demoChip(), 0), std::runtime_error);
        QCOMPARE(quint8(raw->sent[1]), quint8(0x32));
        QCOMPARE(quint8(raw->sent[8]), quint8(0xff)); // Rejected prepare never launches erase.
    }
    void boundsAndCancellation() {
        auto c = ChipDatabase::demoChip();
        Programmer p;
        p.connectDevice(true, c);
        p.configure(c);
        QVERIFY_EXCEPTION_THROWN(p.read(c, 0, c.size - 10, 11), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(p.write(c, 0, 1, QByteArray(256, 'x')), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(p.read(c, 3, 0, 256), std::runtime_error);
        p.cancelled = true;
        QVERIFY_EXCEPTION_THROWN(p.read(c, 0, 0, 256), std::runtime_error);
        p.cancelled = false;
        QCOMPARE(p.read(c, 0, 0, 1), QByteArray(1, char(0xff)));
        p.progress = [&](int, const QString &) { p.cancelled = true; };
        QVERIFY_EXCEPTION_THROWN(p.read(c, 0, 0, 8192), std::runtime_error);
    }
    void intelHexRoundTrip() {
        QByteArray b(70003, '\0');
        for (int i = 0; i < b.size(); i++)
            b[i] = char(i * 29);
        auto text = Buffer::intelHex(b);
        QCOMPARE(Buffer::parseIntelHex(text), b);
        auto bad = text;
        bad[10] = '9';
        QVERIFY_EXCEPTION_THROWN(Buffer::parseIntelHex(bad), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Buffer::parseIntelHex(":00000001FF\n"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Buffer::parseIntelHex(":0100000000FF\n:0100000000FF\n:00000001FF\n"),
                                 std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Buffer::parseIntelHex(":02000004FFFFFC\n:0100000000FF\n:00000001FF\n"),
                                 std::runtime_error);
        QCOMPARE(Buffer::parseIntelHex(":0100020042BB\n:00000001FF\n"), QByteArray::fromHex("ffff42"));
    }
    void databaseRoundTrip() {
        QTemporaryDir dir;
        ChipDatabase db;
        db.chips = {ChipDatabase::demoChip()};
        db.source = "test";
        db.saveJson(dir.filePath("chips.json"));
        ChipDatabase other;
        other.loadJson(dir.filePath("chips.json"));
        QCOMPARE(other.chips.size(), 1);
        QCOMPARE(other.chips[0].json(), db.chips[0].json());
        auto bad = db.chips[0].json();
        bad["page"] = 65537;
        QVERIFY_EXCEPTION_THROWN(Chip::fromJson(bad), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ChipDatabase::parseVendor(QByteArray(96, '\0')), std::runtime_error);
    }
    void atomicFileAndHex() {
        QTemporaryDir dir;
        QByteArray data(1024, char(0x55));
        Buffer::save(dir.filePath("dump.hex"), data);
        QCOMPARE(Buffer::load(dir.filePath("dump.hex")), data);
        Buffer::save(dir.filePath("dump.bin"), data);
        QCOMPARE(Buffer::load(dir.filePath("dump.bin")), data);
        QVERIFY_EXCEPTION_THROWN(readFile(dir.filePath("dump.bin"), 1023), std::runtime_error);
        QCOMPARE(readFile(dir.filePath("dump.bin"), 1024), data);
        // Linux procfs reports size 0 even though reading returns data.
        QVERIFY_EXCEPTION_THROWN(readFile("/proc/self/cmdline", 1), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
