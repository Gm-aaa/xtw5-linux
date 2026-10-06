#include "hexmodel.h"
#include "mainwindow.h"
#include "protocol.h"
#include <QtTest>
#include <QtWidgets>

struct HotplugFixture {
    std::atomic_int opens{0}, detections{0}, commands{0}, delayMs{0};
    std::atomic_bool noChip{false};
};
class InfoOnlyTransport final : public Transport {
    std::shared_ptr<HotplugFixture> state;
    QByteArray pending;

  public:
    explicit InfoOnlyTransport(std::shared_ptr<HotplugFixture> s) : state(std::move(s)) {}
    bool simulated() const override { return false; }
    void write(const QByteArray &bytes) override {
        const auto r = Protocol::parse(bytes);
        ++state->commands;
        QThread::msleep(state->delayMs.load());
        if (r.command >= 0x10 && r.command <= 0x13)
            pending = Protocol::frame(r.command, "XTW-test");
        else if (r.command == 0x31) {
            ++state->detections;
            pending =
                Protocol::frame(0x31, QByteArray::fromHex(state->noChip ? "000000000000" : "00ef40110000"));
        } else
            fail("Unexpected command in hotplug fixture");
    }
    QByteArray read(int n, int) override {
        auto result = pending.left(n);
        pending.remove(0, result.size());
        return result;
    }
};

class GuiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");
        QCoreApplication::setOrganizationName("XTWStudioTests");
        QCoreApplication::setApplicationName("xtw5-tests");
        QStandardPaths::setTestModeEnabled(true);
    }
    void simulatedWorkflow() {
        MainWindow w(true);
        w.show();
        auto connectButton = w.findChild<QPushButton *>("connectButton");
        QVERIFY(connectButton);
        QTest::mouseClick(connectButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        QCOMPARE(connectButton->text(), QString("断开连接"));
        auto model = w.findChild<HexModel *>();
        QVERIFY(model);
        QByteArray data(4096, '\x5a');
        model->setBytes(data);
        auto autoButton = w.findChild<QPushButton *>("op_自动");
        QTest::mouseClick(autoButton, Qt::LeftButton);
        QVERIFY(!autoButton->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(autoButton->isEnabled(), 10000);
        auto phase = w.findChild<QLabel *>("phase");
        QCOMPARE(phase->text(), QString("完成"));
        w.findChild<QLineEdit *>("byteCount")->setText("4096");
        QTest::mouseClick(w.findChild<QPushButton *>("op_读取"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        QCOMPARE(model->buffer(), data);
        QTest::mouseClick(w.findChild<QPushButton *>("op_校验"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        QCOMPARE(phase->text(), QString("完成"));
        if (qEnvironmentVariableIsSet("XTW_TEST_SCREENSHOT"))
            QVERIFY(w.grab().save(qEnvironmentVariable("XTW_TEST_SCREENSHOT")));
        QTest::mouseClick(w.findChild<QPushButton *>("op_擦除"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        QCOMPARE(phase->text(), QString("完成"));
        QTest::mouseClick(w.findChild<QPushButton *>("op_校验"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        QVERIFY(w.findChild<QPlainTextEdit *>("logView")->toPlainText().contains("校验失败"));
        QCOMPARE(connectButton->text(), QString("连接设备"));
    }
    void editHexAndBounds() {
        HexModel m;
        m.setBytes(QByteArray::fromHex("0011aaff"));
        QCOMPARE(m.rowCount(), 1);
        QVERIFY(m.setData(m.index(0, 2), "BE", Qt::EditRole));
        QCOMPARE(m.buffer().toHex(), QByteArray("0011beff"));
        QVERIFY(!m.setData(m.index(0, 2), "1FF", Qt::EditRole));
        QVERIFY(!m.setData(m.index(0, 16), "00", Qt::EditRole));
        QVERIFY(!m.setData(m.index(0, 6), "00", Qt::EditRole));
        m.setEditable(false);
        QVERIFY(!m.setData(m.index(0, 0), "AA", Qt::EditRole));
    }
    void autoDetectionWithoutSelectedChip() {
        MainWindow w(true);
        w.show();
        auto connectButton = w.findChild<QPushButton *>("connectButton");
        QTest::mouseClick(connectButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        auto search = w.findChild<QLineEdit *>("chipSearch");
        auto chips = w.findChild<QComboBox *>("chipSelect");
        search->setText("not-a-chip");
        QCOMPARE(chips->currentIndex(), -1);
        // Neither model selection nor transfer-range fields are prerequisites for ID detection.
        w.findChild<QLineEdit *>("byteCount")->setText("invalid");
        auto detect = w.findChild<QPushButton *>("op_识别");
        QCOMPARE(detect->text(), QString("自动识别"));
        for (int i = 0; i < 2; ++i) {
            QTest::mouseClick(detect, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(detect->isEnabled(), 5000);
            QCOMPARE(w.findChild<QLabel *>("phase")->text(), QString("完成"));
            QCOMPARE(chips->count(), 1);
            QCOMPARE(chips->currentIndex(), 0);
            QVERIFY(chips->currentText().contains("DEMO-25-1M"));
        }
    }
    void ambiguousDetectionRequiresSelection() {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        const auto path = dir + "/chips.json";
        const bool existed = QFile::exists(path);
        const auto backup = existed ? readFile(path) : QByteArray();
        auto cleanup = qScopeGuard([&] {
            if (existed)
                saveFile(path, backup);
            else
                QFile::remove(path);
        });
        ChipDatabase db;
        auto variant = ChipDatabase::demoChip();
        variant.name = "Other model sharing ID";
        variant.manufacturer = "Test";
        auto eeprom = variant;
        eeprom.type = 2;
        eeprom.jedec = 0;
        eeprom.name = "24C02";
        eeprom.manufacturer = "EEPROM vendor";
        db.chips = {variant, eeprom};
        db.saveJson(path);
        MainWindow w(true);
        w.show();
        auto autoSelect = w.findChild<QCheckBox *>("autoSelectMatch");
        QVERIFY(autoSelect->isChecked());
        autoSelect->setChecked(false);
        auto family = w.findChild<QComboBox *>("familyFilter");
        auto manufacturer = w.findChild<QComboBox *>("manufacturerFilter");
        auto choices = w.findChild<QComboBox *>("chipSelect");
        family->setCurrentIndex(2);
        QCOMPARE(choices->count(), 1);
        QVERIFY(choices->currentText().startsWith("24C02"));
        QCOMPARE(manufacturer->count(), 2);
        family->setCurrentIndex(1);
        QCOMPARE(choices->count(), 2);
        manufacturer->setCurrentIndex(manufacturer->findText("Test"));
        QCOMPARE(choices->count(), 1);
        family->setCurrentIndex(0);
        manufacturer->setCurrentIndex(0);
        auto connectButton = w.findChild<QPushButton *>("connectButton");
        QTest::mouseClick(connectButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(connectButton->isEnabled(), 5000);
        auto detect = w.findChild<QPushButton *>("op_识别");
        auto chips = w.findChild<QComboBox *>("chipSelect");
        for (int i = 0; i < 2; ++i) {
            QTest::mouseClick(detect, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(detect->isEnabled(), 5000);
            QCOMPARE(chips->count(), 2);
            QCOMPARE(chips->currentIndex(), -1);
            QVERIFY(chips->isEnabled());
        }
        QTest::mouseClick(w.findChild<QPushButton *>("op_读取"), Qt::LeftButton);
        QVERIFY(w.findChild<QPlainTextEdit *>("logView")->toPlainText().contains("请先选择芯片"));
        chips->setCurrentIndex(1);
        QCOMPARE(chips->currentText(), QString("Other model sharing ID · Test"));
        autoSelect->setChecked(true);
        QTest::mouseClick(detect, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(detect->isEnabled(), 5000);
        QCOMPARE(chips->count(), 2);
        QCOMPARE(chips->currentIndex(), 0);
        QVERIFY(chips->currentText().startsWith("DEMO-25-1M"));
        QVERIFY(w.findChild<QPlainTextEdit *>("logView")->toPlainText().contains("按 Windows 默认规则"));
    }
    void smallWindowLayout() {
        MainWindow w(true);
        w.resize(980, 620);
        w.show();
        QTest::qWait(20);
        auto label = w.findChild<QLabel *>("chipInfo");
        auto select = w.findChild<QComboBox *>("chipSelect");
        QVERIFY(label);
        QVERIFY(select);
        QVERIFY(label->mapTo(&w, QPoint()).y() >= select->mapTo(&w, QPoint(0, select->height())).y());
        QVERIFY(label->height() >= label->minimumHeight());
        QVERIFY(!w.findChild<QDialog *>("settingsDialog")->isVisible());
        QVERIFY(!w.findChild<QLineEdit *>("byteCount")->isVisible());
        QVERIFY(w.findChild<QPushButton *>("op_自动")->isVisible());
        QVERIFY(w.findChild<QPushButton *>("op_识别")->isVisible());
        for (auto button : label->parentWidget()->findChildren<QPushButton *>())
            QVERIFY(!label->geometry().intersects(button->geometry()));
    }
    void modalDialogDefersAutoConnect() {
        auto state = std::make_shared<HotplugFixture>();
        UsbServices services;
        services.enumerate = [] { return QStringList{"device"}; };
        services.open = [state](const Log &) {
            ++state->opens;
            return std::make_unique<InfoOnlyTransport>(state);
        };
        MainWindow w(false, nullptr, services);
        w.show();
        auto timer = w.findChild<QTimer *>("usbMonitor");
        timer->stop();
        QDialog dialog(&w);
        dialog.setModal(true);
        dialog.show();
        QCoreApplication::processEvents(); // Includes the initial singleShot USB scan.
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        QCOMPARE(state->opens.load(), 0);
        QVERIFY(w.findChild<QPushButton *>("connectButton")->isEnabled());
        dialog.accept();
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        QTRY_COMPARE_WITH_TIMEOUT(w.findChild<QPushButton *>("connectButton")->text(),
                                 QString("断开连接"), 5000);
        QCOMPARE(state->opens.load(), 1);
    }
    void startupAndUsbHotplug() {
        auto state = std::make_shared<HotplugFixture>();
        QStringList attached{"bus1-device1"};
        UsbServices services;
        services.enumerate = [&] { return attached; };
        services.open = [state](const Log &) {
            ++state->opens;
            return std::make_unique<InfoOnlyTransport>(state);
        };
        MainWindow w(false, nullptr, services);
        w.show();
        auto timer = w.findChild<QTimer *>("usbMonitor");
        timer->stop(); // Exercise exact hotplug events, without wall-clock sleeps or real USB.
        auto tick = [&] { QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection); };
        auto button = w.findChild<QPushButton *>("connectButton");
        QTRY_COMPARE_WITH_TIMEOUT(button->text(), QString("断开连接"), 5000);
        QCOMPARE(state->opens.load(), 1);
        QCOMPARE(state->detections.load(), 1);
        QCOMPARE(state->commands.load(), 5); // Four information requests, then ID; no chip writes.
        auto model = w.findChild<HexModel *>();
        model->setBytes(QByteArray("preserve me"));
        attached.clear();
        tick();
        QCOMPARE(button->text(), QString("连接设备"));
        QCOMPARE(model->buffer(), QByteArray("preserve me"));
        state->noChip = true;
        attached = {"bus1-device2"};
        tick();
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QCOMPARE(button->text(), QString("断开连接")); // No chip does not disconnect the programmer.
        QCOMPARE(state->detections.load(), 2);
        QCOMPARE(w.findChild<QComboBox *>("chipSelect")->currentIndex(), -1);
        QTest::mouseClick(button, Qt::LeftButton);
        tick();
        QCOMPARE(state->opens.load(), 2); // Manual disconnect remains disconnected.
        attached.clear();
        tick();
        attached = {"bus1-device3", "bus1-device4"};
        tick();
        QCOMPARE(state->opens.load(), 2); // Ambiguous hardware is never chosen arbitrarily.
        attached = {"bus1-device3"};
        state->delayMs = 80;
        tick();
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(state->opens.load() == 3, 2000);
        attached.clear();
        tick(); // Removal while the worker is using the transport.
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QCOMPARE(button->text(), QString("连接设备"));
        QCOMPARE(model->buffer(), QByteArray("preserve me"));
        state->delayMs = 0;
        state->noChip = false;
        attached = {"bus1-device5"};
        tick();
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QCOMPARE(button->text(), QString("断开连接"));
        QCOMPARE(state->opens.load(), 4);
    }
    void hotplugFailureRetryBound() {
        int attempts = 0;
        UsbServices services;
        services.enumerate = [] { return QStringList{"device"}; };
        services.open = [&](const Log &) -> std::unique_ptr<Transport> {
            ++attempts;
            fail("Permission denied fixture");
        };
        MainWindow w(false, nullptr, services);
        auto timer = w.findChild<QTimer *>("usbMonitor");
        timer->stop();
        auto button = w.findChild<QPushButton *>("connectButton");
        QTRY_VERIFY_WITH_TIMEOUT(
            w.findChild<QPlainTextEdit *>("logView")->toPlainText().contains("Permission denied"), 5000);
        for (int i = 0; i < 6; ++i) {
            QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
            QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        }
        QCOMPARE(attempts, 3);
    }
};
QTEST_MAIN(GuiTest)
#include "test_gui.moc"
