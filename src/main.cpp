#include "chip.h"
#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QCoreApplication::setOrganizationName("XTWStudio");
    QCoreApplication::setApplicationName("xtw5-linux");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("原生 Linux XTW-5 GUI · Qt/libusb");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "选择不访问 USB 的模拟设备"});
    parser.addOption({"probe-info", "仅查询真实编程器信息并输出 USB 报文，不访问芯片"});
    parser.addOption({"probe-detect", "查询设备信息和芯片 ID 并输出原始报文，不读写或擦除芯片"});
    parser.addOption({"probe-read", "只读验证：按 --chip 指定型号读取整片并保存 BIN", "file"});
    parser.addOption({"chip", "只读验证使用的精确芯片型号（仅限 3.3 V SPI NOR）", "name"});
    parser.addOption({"import-vendor", "从 mdb.yg 导入芯片库（同时指定 --vendor-exe）", "file"});
    parser.addOption({"vendor-exe", "已解包的 XTW-5.exe", "file"});
    parser.addOption({"screenshot", "启动后保存 GUI 截图并退出", "file"});
    parser.process(app);
    try {
        if (parser.isSet("probe-info") || parser.isSet("probe-detect") || parser.isSet("probe-read")) {
            Programmer programmer;
            programmer.trace = true;
            programmer.log = [](const QString &s) { QTextStream(stdout) << s << Qt::endl; };
            programmer.connectDevice(false, ChipDatabase::demoChip());
            for (const auto &s : programmer.info())
                QTextStream(stdout) << s << Qt::endl;
            if (parser.isSet("probe-detect"))
                QTextStream(stdout) << "Raw detection payload: " << programmer.command(0x31).toHex(' ')
                                    << Qt::endl;
            if (parser.isSet("probe-read")) {
                const auto output = parser.value("probe-read");
                if (QFile::exists(output))
                    fail("输出文件已存在，请指定新文件名");
                ChipDatabase db;
                db.loadJson(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                            "/chips.json");
                QVector<Chip> matches;
                for (const auto &c : db.chips)
                    if (c.name == parser.value("chip"))
                        matches << c;
                if (matches.size() != 1 || matches[0].type != 1 || matches[0].voltage != 3300)
                    fail("请指定库中唯一的 3.3 V SPI NOR 型号");
                const auto chip = matches.first();
                if ((programmer.detect() & 0xffffff) != chip.jedec)
                    fail("实机 ID 与所选型号不匹配");
                programmer.configure(chip);
                const auto data = programmer.read(chip, 0, 0, chip.size);
                saveFile(output, data);
                QTextStream(stdout) << "Saved " << data.size() << " bytes: " << output << Qt::endl;
            }
            return 0;
        }
        if (parser.isSet("import-vendor")) {
            ChipDatabase db;
            db.importVendor(parser.value("import-vendor"), parser.value("vendor-exe"));
            auto dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
            QDir().mkpath(dir);
            db.saveJson(dir + "/chips.json");
            QTextStream(stdout) << "Imported " << db.chips.size() << " chips to " << dir << "/chips.json\n";
            return 0;
        }
        MainWindow window(parser.isSet("demo"));
        window.show();
        if (parser.isSet("screenshot"))
            QTimer::singleShot(500, &window, [&] {
                const bool ok = window.grab().save(parser.value("screenshot"));
                app.exit(ok ? 0 : 1);
            });
        return app.exec();
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << "\n";
        return 1;
    }
}
