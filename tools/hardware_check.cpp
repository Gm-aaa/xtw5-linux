#include "buffer.h"
#include "programmer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QStandardPaths>
#include <QTextStream>

// Explicit diagnostic utility, never registered with CTest or invoked by the GUI.
// Requires authorization for erase/write. It does not automatically recover after failure.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("XTWStudio");
    app.setApplicationName("xtw5-linux");
    auto args = app.arguments();
    if (args.size() != 4) {
        QTextStream(stderr) << "Usage: xtw5-hardware-check verify|negative|erase|write FILE CHIP\n";
        return 2;
    }
    try {
        const auto action = args[1];
        if (!QStringList{"verify", "negative", "erase", "write"}.contains(action))
            fail("Unknown diagnostic action");
        ChipDatabase db;
        db.loadJson(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/chips.json");
        QVector<Chip> matches;
        for (const auto &c : db.chips)
            if (c.name == args[3])
                matches << c;
        if (matches.size() != 1 || matches[0].type != 1 || matches[0].voltage != 3300)
            fail("Diagnostic requires one exact 3.3 V NOR chip profile");
        const auto chip = matches.first();
        auto bytes = readFile(args[2]);
        if (bytes.size() != chip.size)
            fail("Diagnostic file must cover the entire chip");
        QTextStream(stdout) << "Input SHA256 " << Buffer::sha256(bytes) << Qt::endl;
        Programmer p;
        p.trace = true;
        p.log = [](const QString &s) {
            // Keep firmware contents out of protocol logs; retain the write header.
            QTextStream(stdout) << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " "
                                << (s.startsWith("TX 02 35") ? s.left(56) + " [data omitted]" : s)
                                << Qt::endl;
        };
        int last = -1;
        p.progress = [&](int n, const QString &phase) {
            if (n / 10 != last) {
                last = n / 10;
                QTextStream(stdout) << phase << " " << n << "%" << Qt::endl;
            }
        };
        p.connectDevice(false, chip);
        if ((p.detect() & 0xffffff) != chip.jedec)
            fail("Device ID mismatch");
        p.configure(chip);
        if (action == "erase")
            p.erase(chip, 0);
        else if (action == "write")
            p.write(chip, 0, 0, bytes);
        else if (action == "verify")
            p.verify(chip, 0, 0, bytes);
        else {
            // Deliberately wrong expectation in RAM only; chip contents are never modified.
            bytes[chip.size - 1] ^= 1;
            try {
                p.verify(chip, 0, 0, bytes);
            } catch (const std::exception &e) {
                const auto message = QString::fromUtf8(e.what());
                const auto expected = QString("校验失败 @ 0x%1").arg(chip.size - 1, 8, 16, QChar('0'));
                if (!message.startsWith(expected))
                    throw;
                QTextStream(stdout) << "EXPECTED MISMATCH: " << message << Qt::endl;
                return 0;
            }
            fail("BUG: changed expected byte was not detected");
        }
        QTextStream(stdout) << "PASS " << action << Qt::endl;
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << "FAILED: " << e.what() << Qt::endl;
        return 1;
    }
}
