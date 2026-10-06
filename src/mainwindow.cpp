#include "mainwindow.h"
#include "buffer.h"
#include "hexmodel.h"
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QtWidgets>

MainWindow::MainWindow(bool demo, QWidget *p, UsbServices services)
    : QMainWindow(p), usb(std::move(services)) {
    QPalette light;
    light.setColor(QPalette::Window, QColor("#f3f5f8"));
    light.setColor(QPalette::WindowText, QColor("#202c3c"));
    light.setColor(QPalette::Base, Qt::white);
    light.setColor(QPalette::AlternateBase, QColor("#f7f9fc"));
    light.setColor(QPalette::Text, QColor("#202c3c"));
    light.setColor(QPalette::Button, QColor("#f3f5f8"));
    light.setColor(QPalette::ButtonText, QColor("#263d58"));
    light.setColor(QPalette::Highlight, QColor("#245dc1"));
    light.setColor(QPalette::HighlightedText, Qt::white);
    light.setColor(QPalette::ToolTipBase, Qt::white);
    light.setColor(QPalette::ToolTipText, QColor("#202c3c"));
    light.setColor(QPalette::Light, Qt::white);
    light.setColor(QPalette::Midlight, QColor("#e9edf3"));
    light.setColor(QPalette::Mid, QColor("#ccd5e2"));
    light.setColor(QPalette::Dark, QColor("#99a3b1"));
    light.setColor(QPalette::Shadow, QColor("#66768c"));
    light.setColor(QPalette::Disabled, QPalette::Text, QColor("#99a3b1"));
    light.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#99a3b1"));
    QApplication::setPalette(light);
    setObjectName("mainWindow");
    setWindowTitle("XTW-5 编程器 · Linux");
    resize(1080, 700);
    setMinimumSize(980, 620);
    buildUi();
    mode->setCurrentIndex(demo ? 1 : 0);
    loadDatabase();
    engine.log = [this](const QString &s) { QMetaObject::invokeMethod(this, [this, s] { appendLog(s); }); };
    engine.progress = [this](int value, const QString &s) {
        QMetaObject::invokeMethod(this, [this, value, s] {
            progressBar->setValue(value);
            phase->setText(s);
        });
    };
    connect(&watcher, &QFutureWatcher<TaskResult>::finished, this, &MainWindow::finishTask);
    connect(hex, &HexModel::modified, this, [this] {
        dirty = true;
        updateBuffer();
    });
    if (demo) {
        mode->setCurrentIndex(1);
        chips->setCurrentIndex(0);
    } else {
        familyFilter->setCurrentIndex(1);
        chips->setCurrentIndex(-1);
    }
    QSettings settings;
    if (settings.contains("compactGeometry"))
        restoreGeometry(settings.value("compactGeometry").toByteArray());
    appendLog("程序就绪。当前 C84017 芯片已完成读写验收，其他型号仍待验证；模拟模式不会访问 USB。");
    updateChip();
    updateBuffer();
    setBusy(false);
    usbTimer = new QTimer(this);
    usbTimer->setObjectName("usbMonitor");
    usbTimer->setInterval(1000);
    connect(usbTimer, &QTimer::timeout, this, &MainWindow::scanUsb);
    usbTimer->start();
    QTimer::singleShot(0, this, &MainWindow::scanUsb);
}
MainWindow::~MainWindow() {
    usbTimer->stop();
    engine.cancelled = true;
    watcher.waitForFinished();
}
QString MainWindow::configDirectory() const {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir;
}
void MainWindow::buildUi() {
    setStyleSheet(R"(
QWidget{font-size:13px;} QMainWindow,QDialog{background:#f0f0f0;}
QGroupBox{border:1px solid #c9cdd2;margin-top:10px;padding:10px 8px 8px;}
QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;}
QPushButton{padding:5px 9px;} QLineEdit,QComboBox{min-height:24px;}
QToolBar{spacing:4px;padding:5px;border-bottom:1px solid #c9cdd2;}
QToolButton{padding:5px 9px;} QTableView{background:white;alternate-background-color:#fafbfc;selection-background-color:#dbe8ff;selection-color:#18375d;}
QHeaderView::section{background:#eceff3;padding:4px;border:0;color:#355075;}
QPlainTextEdit{background:white;color:#304e6e;border:1px solid #c9cdd2;}
QProgressBar{max-height:18px;text-align:center;} QProgressBar::chunk{background:#3c9e50;}
)");
    auto fileMenu = menuBar()->addMenu("文件");
    auto editMenu = menuBar()->addMenu("编辑");
    auto settingsMenu = menuBar()->addMenu("设置");
    auto help = menuBar()->addMenu("帮助");
    auto tool = addToolBar("常用操作");
    tool->setObjectName("mainToolbar");
    tool->setMovable(false);
    auto addFile = [&](QMenu *menu, const QString &text, const QKeySequence &key, auto fn, bool visible) {
        auto action = menu->addAction(text);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, fn);
        fileActions << action;
        if (visible)
            tool->addAction(action);
        return action;
    };
    addFile(fileMenu, "打开", QKeySequence::Open, [this] { openBuffer(); }, true);
    addFile(fileMenu, "保存", QKeySequence::Save, [this] { saveBuffer(false); }, true);
    addFile(fileMenu, "另存为…", QKeySequence::SaveAs, [this] { saveBuffer(true); }, false);
    addFile(fileMenu, "新建…", QKeySequence::New, [this] { newBuffer(); }, false);
    addFile(editMenu, "填充…", {}, [this] { fillBuffer(); }, false);
    addFile(editMenu, "补齐整页", {}, [this] { padBuffer(); }, false);
    addFile(editMenu, "查找…", QKeySequence::Find, [this] { findBytes(); }, false);
    addFile(editMenu, "跳转…", QKeySequence("Ctrl+G"), [this] { gotoOffset(); }, false);
    tool->addSeparator();
    for (const auto &name : QStringList{"读取", "写入", "查空", "擦除", "校验", "自动"}) {
        auto button = new QPushButton(name);
        button->setObjectName("op_" + name);
        button->setToolTip(name == "自动"   ? "全片擦除 → 写入 → 回读校验"
                           : name == "写入" ? "写入后回读校验"
                                            : name);
        tool->addWidget(button);
        operationWidgets << button;
        connect(button, &QPushButton::clicked, this, [this, name] { operation(name); });
    }
    cancelButton = new QPushButton("停止");
    cancelButton->setObjectName("cancelButton");
    cancelButton->setEnabled(false);
    tool->addWidget(cancelButton);
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        engine.cancelled = true;
        cancelButton->setEnabled(false);
        phase->setText("正在停止…");
    });
    tool->addSeparator();
    connectButton = new QPushButton("连接设备");
    connectButton->setObjectName("connectButton");
    tool->addWidget(connectButton);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectDevice);

    auto settings = new QDialog(this);
    settings->setObjectName("settingsDialog");
    settings->setWindowTitle("编程器设置");
    settings->setMinimumWidth(430);
    auto sf = new QFormLayout(settings);
    mode = new QComboBox;
    mode->setObjectName("mode");
    mode->addItems({"USB · XTW-5 实机", "模拟设备 · 离线测试"});
    sf->addRow("连接方式", mode);
    socket = new QComboBox;
    socket->addItems({"插座 0（主插座）", "插座 1"});
    sf->addRow("插座", socket);
    spiSpeed = new QComboBox;
    spiSpeed->addItems({"保持设备当前 SPI 速率", "18 MHz", "9 MHz", "4.5 MHz", "2.25 MHz", "1.125 MHz"});
    sf->addRow("SPI 速率", spiSpeed);
    startAddress = new QLineEdit("0x00000000");
    startAddress->setObjectName("startAddress");
    byteCount = new QLineEdit("0x00020000");
    byteCount->setObjectName("byteCount");
    sf->addRow("起始地址", startAddress);
    sf->addRow("读取 / 查空长度", byteCount);
    auto rangeHint = new QLabel("默认从地址 0 读取整片。写入 / 校验使用缓冲区长度，擦除作用于全片。");
    rangeHint->setWordWrap(true);
    sf->addRow(rangeHint);
    trace = new QCheckBox("记录 USB 请求与响应");
    sf->addRow(trace);
    autoSelect = new QCheckBox("自动选择首个匹配型号（与 Windows 默认一致）");
    autoSelect->setObjectName("autoSelectMatch");
    autoSelect->setChecked(QSettings().value("autoSelectMatch", true).toBool());
    autoSelect->setToolTip("同一 ID 有多个型号时，按芯片库顺序选择第一项；仍可在型号列表中切换。");
    sf->addRow(autoSelect);
    operationWidgets << autoSelect;
    auto closeSettings = new QDialogButtonBox(QDialogButtonBox::Close);
    sf->addRow(closeSettings);
    connect(closeSettings, &QDialogButtonBox::rejected, settings, &QDialog::hide);
    auto showSettings = [settings] {
        settings->show();
        settings->raise();
        settings->activateWindow();
    };
    connect(settingsMenu->addAction("编程器设置…"), &QAction::triggered, this, showSettings);
    auto settingsAction = tool->addAction("设置");
    connect(settingsAction, &QAction::triggered, this, showSettings);
    addFile(settingsMenu, "导入芯片库…", {}, [this] { importDatabase(); }, false);
    addFile(settingsMenu, "自定义芯片…", {}, [this] { addChip(); }, false);
    connect(settingsMenu->addAction("导出日志…"), &QAction::triggered, this, &MainWindow::exportLog);
    connect(settingsMenu->addAction("清空日志"), &QAction::triggered, this, [this] { logView->clear(); });
    auto details = new QDialog(this);
    details->setWindowTitle("缓冲区信息");
    auto detailsLayout = new QVBoxLayout(details);
    digest = new QLabel;
    digest->setTextInteractionFlags(Qt::TextSelectableByMouse);
    digest->setWordWrap(true);
    details->setMinimumWidth(540);
    detailsLayout->addWidget(digest);
    connect(fileMenu->addAction("缓冲区信息…"), &QAction::triggered, this, [details] {
        details->show();
        details->raise();
    });
    connect(help->addAction("USB 权限配置"), &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, "USB 权限",
            "在项目目录运行：\nsudo install -m 644 resources/70-xtw5.rules "
            "/etc/udev/rules.d/70-xtw5.rules\nsudo udevadm control --reload-rules\n\n然后重新插拔编程器。");
    });
    connect(help->addAction("关于"), &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, "XTW-5 Linux",
            "原生 Linux XTW-5 编程器 · Qt / libusb\n设备连接已验证，芯片操作待实机验收。\nAT45 / "
            "NAND、脱机下载和固件升级尚未实现。\n\n界面布局参考厂家资料包中的一分钟上手指南。");
    });

    auto central = new QWidget;
    setCentralWidget(central);
    auto outer = new QHBoxLayout(central);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(8);
    auto left = new QWidget;
    left->setObjectName("chipSidebar");
    left->setFixedWidth(218);
    auto lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    auto chipBox = new QGroupBox("芯片");
    auto cv = new QVBoxLayout(chipBox);
    cv->setSpacing(6);
    familyFilter = new QComboBox;
    familyFilter->setObjectName("familyFilter");
    familyFilter->addItem("全部类型", 0);
    familyFilter->addItem("25 SPI FLASH", 1);
    familyFilter->addItem("24 EEPROM", 2);
    familyFilter->addItem("AT45（待适配）", 4);
    familyFilter->addItem("SPI NAND（待适配）", 8);
    cv->addWidget(new QLabel("类型"));
    cv->addWidget(familyFilter);
    manufacturerFilter = new QComboBox;
    manufacturerFilter->setObjectName("manufacturerFilter");
    cv->addWidget(new QLabel("厂家"));
    cv->addWidget(manufacturerFilter);
    cv->addWidget(new QLabel("型号"));
    search = new QLineEdit;
    search->setObjectName("chipSearch");
    search->setPlaceholderText("搜索型号 / ID");
    search->setClearButtonEnabled(true);
    cv->addWidget(search);
    chips = new QComboBox;
    chips->setObjectName("chipSelect");
    chips->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    chips->setMinimumContentsLength(14);
    cv->addWidget(chips);
    auto detect = new QPushButton("自动识别");
    detect->setObjectName("op_识别");
    detect->setToolTip("无需预选型号，检测主插座的 25 系列芯片。24 EEPROM 请手动选择。");
    cv->addWidget(detect);
    connect(detect, &QPushButton::clicked, this, [this] { operation("识别"); });
    lv->addWidget(chipBox);
    auto infoBox = new QGroupBox("芯片信息");
    auto iv = new QVBoxLayout(infoBox);
    chipInfo = new QLabel;
    chipInfo->setObjectName("chipInfo");
    chipInfo->setWordWrap(true);
    chipInfo->setMinimumHeight(90);
    iv->addWidget(chipInfo);
    adapter = new QCheckBox("已接好 1.8 V 适配器");
    adapter->setToolTip("1.8 V 芯片需匹配的电平 / 电源适配器");
    iv->addWidget(adapter);
    lv->addWidget(infoBox);
    auto hint = new QLabel("25 系列：检测后选择型号\n24 系列：按芯片丝印手选");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#657080;");
    lv->addWidget(hint);
    lv->addStretch();
    outer->addWidget(left);
    connect(search, &QLineEdit::textChanged, this, &MainWindow::rebuildChips);
    connect(chips, &QComboBox::currentIndexChanged, this, &MainWindow::updateChip);
    connect(familyFilter, &QComboBox::currentIndexChanged, this, [this] {
        detectedFilter = 0;
        refreshManufacturers();
        rebuildChips();
    });
    connect(manufacturerFilter, &QComboBox::currentIndexChanged, this, [this] {
        detectedFilter = 0;
        rebuildChips();
    });
    operationWidgets << search << chips << mode << socket << spiSpeed << startAddress << byteCount << adapter
                     << trace << familyFilter << manufacturerFilter << detect;

    auto right = new QVBoxLayout;
    right->setSpacing(5);
    bufferInfo = new QLabel;
    bufferInfo->setObjectName("bufferInfo");
    right->addWidget(bufferInfo);
    hex = new HexModel(this);
    table = new QTableView;
    table->setObjectName("hexTable");
    table->setModel(hex);
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->horizontalHeader()->setMinimumSectionSize(26);
    for (int i = 0; i < 16; ++i)
        table->setColumnWidth(i, 30);
    table->horizontalHeader()->setSectionResizeMode(16, QHeaderView::Stretch);
    right->addWidget(table, 1);
    logView = new QPlainTextEdit;
    logView->setObjectName("logView");
    logView->setReadOnly(true);
    logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView->setMaximumBlockCount(6000);
    logView->setFixedHeight(88);
    right->addWidget(logView);
    auto progressRow = new QHBoxLayout;
    phase = new QLabel("就绪");
    phase->setObjectName("phase");
    phase->setMinimumWidth(100);
    progressRow->addWidget(phase);
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressRow->addWidget(progressBar, 1);
    right->addLayout(progressRow);
    outer->addLayout(right, 1);
    connection = new QLabel("未连接");
    connection->setObjectName("connection");
    statusBar()->addWidget(connection, 1);
    validation = new QLabel("芯片操作待实机验证");
    statusBar()->addPermanentWidget(validation);
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int value) {
        validation->setText(value ? "模拟模式 · 不访问 USB" : "芯片操作待实机验证");
        if (!database.chips.isEmpty()) {
            refreshManufacturers();
            rebuildChips();
        }
    });
}
void MainWindow::refreshManufacturers() {
    const auto previous = manufacturerFilter->currentText();
    QSignalBlocker block(manufacturerFilter);
    manufacturerFilter->clear();
    manufacturerFilter->addItem("全部厂家", QString());
    QStringList names;
    const auto type = familyFilter->currentData().toInt();
    for (const auto &c : database.chips)
        if ((!type || c.type == type) && (mode->currentIndex() == 1 || c.manufacturer != "模拟设备") &&
            !names.contains(c.manufacturer))
            names << c.manufacturer;
    names.sort(Qt::CaseInsensitive);
    for (const auto &name : names)
        manufacturerFilter->addItem(name, name);
    const int index = manufacturerFilter->findText(previous);
    if (index >= 0)
        manufacturerFilter->setCurrentIndex(index);
}
void MainWindow::loadDatabase() {
    database.chips = {ChipDatabase::demoChip()};
    const auto path = configDirectory() + "/chips.json";
    if (QFile::exists(path))
        try {
            database.loadJson(path);
            database.chips.prepend(ChipDatabase::demoChip());
        } catch (const std::exception &e) {
            appendLog("芯片库加载失败：" + QString::fromUtf8(e.what()));
        }
    refreshManufacturers();
    rebuildChips();
}
void MainWindow::rebuildChips() {
    auto old = chips->currentData();
    QSignalBlocker block(chips);
    chips->clear();
    auto q = search->text().trimmed();
    const auto detectedQuery = QString("%1").arg(detectedFilter & 0xffffff, 6, 16, QChar('0'));
    if (detectedFilter && q != detectedQuery)
        detectedFilter = 0;
    const auto candidates = database.detectionCandidates(detectedFilter);
    for (int i = 0; i < database.chips.size(); i++) {
        const auto &c = database.chips[i];
        if (mode->currentIndex() == 0 && c.manufacturer == "模拟设备")
            continue;
        if (!detectedFilter &&
            ((familyFilter->currentData().toInt() && c.type != familyFilter->currentData().toInt()) ||
             (!manufacturerFilter->currentData().toString().isEmpty() &&
              c.manufacturer != manufacturerFilter->currentData().toString())))
            continue;
        auto text = c.name + " · " + c.manufacturer;
        auto match = text + QString(" %1").arg(c.jedec, 6, 16, QChar('0'));
        if (detectedFilter
                ? (!candidates.contains(i) || (mode->currentIndex() == 0 && c.manufacturer == "模拟设备"))
                : !match.contains(q, Qt::CaseInsensitive))
            continue;
        chips->addItem(text, i);
    }
    int found = chips->findData(old);
    if (found >= 0)
        chips->setCurrentIndex(found);
    updateChip();
}
Chip MainWindow::selectedChip() const {
    if (chips->currentIndex() < 0)
        fail("请先选择芯片");
    return database.chips.at(chips->currentData().toInt());
}
void MainWindow::updateChip() {
    if (chips->currentIndex() < 0) {
        adapter->hide();
        chipInfo->setText(chips->count() ? "请选择型号，或点击自动识别" : "没有匹配型号");
        return;
    }
    auto c = selectedChip();
    adapter->setVisible(c.voltage == 1800);
    chipInfo->setText(QString("%1\n容量：%2 字节\n页大小：%3 B\n电压：%4 V\nID：%5\n%6")
                          .arg(c.family())
                          .arg(c.size)
                          .arg(c.page)
                          .arg(c.voltage / 1000.0, 0, 'f', 1)
                          .arg(c.jedec, 6, 16, QChar('0'))
                          .arg(c.supported() ? "" : "此类型尚未适配"));
    byteCount->setText(QString("0x%1").arg(c.size, 8, 16, QChar('0')));
    adapter->setChecked(false);
}
void MainWindow::updateBuffer() {
    const auto &b = hex->buffer();
    bufferInfo->setText(QString("%1%2   ·   %3 字节")
                            .arg(currentFile.isEmpty() ? "未命名缓冲区" : QFileInfo(currentFile).fileName(),
                                 dirty ? " *" : "")
                            .arg(b.size()));
    digest->setText(b.isEmpty() ? "打开 BIN / ROM / Intel HEX 文件，或从芯片读取。"
                                : "SHA-256  " + Buffer::sha256(b));
}
void MainWindow::appendLog(const QString &s) {
    logView->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + s);
}
void MainWindow::setBusy(bool b) {
    busy = b;
    for (auto w : operationWidgets)
        w->setEnabled(!b);
    for (auto a : fileActions)
        a->setEnabled(!b);
    hex->setEditable(!b);
    connectButton->setEnabled(!b);
    cancelButton->setEnabled(b);
    connectButton->setText(online ? "断开连接" : "连接设备");
    mode->setEnabled(!b && !online);
    if (online && mode->currentIndex() == 1) {
        chips->setEnabled(!b && detectedFilter && chips->count() > 1);
        search->setEnabled(false);
    }
}
void MainWindow::runTask(const QString &name, std::function<TaskResult()> fn) {
    if (busy)
        return;
    engine.cancelled = false;
    engine.trace = trace->isChecked();
    elapsed.start();
    phase->setText(name);
    progressBar->setValue(0);
    appendLog("开始：" + name);
    setBusy(true);
    watcher.setFuture(QtConcurrent::run([this, fn] {
        TaskResult r;
        try {
            r = fn();
            r.success = true;
        } catch (const std::exception &e) {
            r.message = QString::fromUtf8(e.what());
            engine.disconnect();
        }
        r.connected = engine.connected();
        return r;
    }));
}
void MainWindow::finishTask() {
    auto r = watcher.result();
    if (deviceRemoved) {
        engine.disconnect();
        r.connected = false;
        r.success = false;
        r.detected = 0;
        r.info.clear();
        r.message = "编程器已拔出，任务停止；缓冲区保留";
        deviceRemoved = false;
        clearDetectedChip();
    }
    online = r.connected;
    if (online && mode->currentIndex() == 0)
        connectAttempts = 3; // After a successful connection, retry only on replug or explicit request.
    if (r.hasBuffer && r.success) {
        hex->setBytes(r.buffer);
        dirty = true;
        currentFile.clear();
        updateBuffer();
    }
    if (!r.info.isEmpty()) {
        connection->setText(r.info.value(0) + "  ·  已连接");
        connection->setToolTip(r.info.join("\n"));
        for (int i = 0; i < r.info.size(); i++)
            appendLog(QString("设备信息 %1：%2").arg(i).arg(r.info[i]));
    }
    if (!online)
        connection->setText("未连接");
    if (r.detected) {
        const auto id = r.detected & 0xffffff;
        {
            QSignalBlocker familyBlock(familyFilter);
            familyFilter->setCurrentIndex(0);
            refreshManufacturers();
            QSignalBlocker manufacturerBlock(manufacturerFilter);
            manufacturerFilter->setCurrentIndex(0);
        }
        detectedFilter = r.detected;
        search->setText(QString("%1").arg(id, 6, 16, QChar('0')));
        rebuildChips(); // Also refresh when repeated detection returns the same ID.
        const bool eeprom = (r.detected >> 24) == 0x24;
        if (chips->count() > 0 && !eeprom && (chips->count() == 1 || autoSelect->isChecked())) {
            chips->setCurrentIndex(0);
            if (chips->count() == 1)
                appendLog("自动识别已匹配：" + selectedChip().name);
            else
                appendLog(QString("ID %1 匹配 %2 个型号，按 Windows 默认规则选中首项：%3；可在型号列表切换")
                              .arg(id, 6, 16, QChar('0')).arg(chips->count()).arg(selectedChip().name));
        } else {
            chips->setCurrentIndex(-1);
            chipInfo->setText(chips->count() == 0 ? "芯片库中没有匹配型号，可清除搜索后手动选择或导入芯片库"
                              : eeprom            ? "检测到 24 系列 EEPROM，请按丝印确认具体型号和容量"
                                                  : "同一 ID 对应多个型号，请展开列表并按芯片丝印选择");
            appendLog(QString("识别 ID %1，匹配 %2 个候选；%3")
                          .arg(r.detected, 8, 16, QChar('0'))
                          .arg(chips->count())
                          .arg(eeprom ? "24 系列不自动推断容量，请手动确认型号"
                                      : "没有自动确定唯一型号，请检查候选或芯片库"));
        }
    }
    phase->setText(r.success ? "完成" : "已停止 / 失败");
    if (r.success)
        progressBar->setValue(100);
    appendLog((r.success ? "完成：" : "失败：") + r.message +
              QString("（%1 秒）").arg(elapsed.elapsed() / 1000.0, 0, 'f', 2));
    setBusy(false);
}
void MainWindow::connectDevice() {
    if (busy)
        return;
    if (online) {
        manualDisconnect = true;
        engine.disconnect();
        online = false;
        connection->setText("未连接");
        appendLog("设备已断开");
        setBusy(false);
        return;
    }
    try {
        const bool sim = mode->currentIndex() == 1;
        manualDisconnect = false;
        deviceRemoved = false;
        if (!sim) {
            auto devices = usb.enumerate();
            if (devices.size() != 1)
                fail(QString("需要连接一台 XTW-5，当前发现 %1 台").arg(devices.size()));
            activeUsb = devices.first();
            observedUsb = devices;
            clearDetectedChip();
        }
        auto c = sim ? selectedChip() : ChipDatabase::demoChip();
        const bool detectOnConnect = !sim && socket->currentIndex() == 0;
        runTask(sim ? "连接模拟器" : "自动连接 XTW-5", [this, c, sim, detectOnConnect] {
            if (sim)
                engine.connectDevice(true, c);
            else
                engine.useTransport(usb.open(engine.log));
            TaskResult r;
            r.info = engine.info();
            if (detectOnConnect)
                r.detected = engine.detect(true);
            r.message = sim ? "模拟通信通过（不代表硬件通过）" : "设备信息查询通过";
            return r;
        });
    } catch (const std::exception &e) {
        appendLog(e.what());
    }
}
void MainWindow::clearDetectedChip() {
    detectedFilter = 0;
    chips->setCurrentIndex(-1);
    adapter->setChecked(false);
}
void MainWindow::scanUsb() {
    // Modal dialogs run a nested event loop. Do not start a worker while closeEvent
    // is asking about unsaved data, or change the selected device during confirmation.
    if (mode->currentIndex() != 0 || QApplication::activeModalWidget())
        return;
    QStringList devices;
    try {
        devices = usb.enumerate();
    } catch (const std::exception &) {
        return; // Enumeration errors are not evidence of physical removal.
    }
    devices.sort();
    if (devices != observedUsb) {
        observedUsb = devices;
        connectAttempts = 0;
        manualDisconnect = false;
    }
    if ((online || busy) && !activeUsb.isEmpty() && !devices.contains(activeUsb)) {
        if (busy) {
            if (!deviceRemoved)
                appendLog("检测到编程器拔出，正在停止任务");
            deviceRemoved = true;
            engine.cancelled = true;
            connection->setText("设备已拔出");
        } else {
            engine.disconnect();
            online = false;
            activeUsb.clear();
            clearDetectedChip();
            connection->setText("未连接 · 等待插入编程器");
            connection->setToolTip({});
            appendLog("编程器已拔出；缓冲区保留，重新插入后自动连接");
            setBusy(false);
        }
    }
    if (busy || online || manualDisconnect)
        return;
    if (devices.size() != 1) {
        connection->setText(devices.isEmpty() ? "未连接 · 等待插入编程器" : "检测到多台编程器，请只保留一台");
        return;
    }
    // Allow udev permissions a moment to settle, without endless reconnect/error loops.
    if (connectAttempts < 3) {
        ++connectAttempts;
        connectDevice();
    }
}
quint32 MainWindow::number(QLineEdit *edit, const QString &label) const {
    bool ok = false;
    auto s = edit->text().trimmed();
    auto n = s.toULongLong(&ok, s.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
    if (!ok || n > 256ULL * 1024 * 1024)
        fail(label + "无效；请输入十进制或 0x 开头的十六进制数");
    return quint32(n);
}
bool MainWindow::prepareHardware(const QString &name) {
    if (!online) {
        appendLog("请先连接设备");
        return false;
    }
    auto c = selectedChip();
    if (!c.supported()) {
        appendLog("此芯片类型尚未适配");
        return false;
    }
    if (mode->currentIndex() == 0 && c.manufacturer == "模拟设备") {
        appendLog("请为实机选择真实芯片型号");
        return false;
    }
    if (mode->currentIndex() == 0 && c.voltage == 1800 && !adapter->isChecked()) {
        QMessageBox::information(
            this, "确认 1.8 V 适配",
            "该芯片需要 1.8 V 适配器。接好后勾选电压适配确认。软件参数不能替代外部电平转换。");
        return false;
    }
    if (mode->currentIndex() == 0 && (name == "写入" || name == "擦除" || name == "自动"))
        return QMessageBox::question(
                   this, "确认修改芯片",
                   QString("%1将修改 %2 的内容。\n%3\n\n已备份需要保留的数据，继续？")
                       .arg(name, c.name,
                            name == "写入" ? "写入后自动回读校验。" : "擦除作用于全片，不能撤销。"),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    return true;
}
void MainWindow::operation(const QString &name) {
    try {
        if (busy)
            return;
        if (name == "识别") {
            if (!online)
                fail("请先连接设备，再点击自动识别；无需预选芯片型号");
            if (socket->currentIndex() != 0)
                fail("厂家识别命令没有插座参数，目前自动识别仅适用于主插座，请选择插座 0");
            runTask("自动识别", [this] {
                TaskResult r;
                r.detected = engine.detect();
                r.message = "自动识别";
                return r;
            });
            return;
        }
        if (busy || !prepareHardware(name))
            return;
        if (name == "读取" && !mayDiscard())
            return;
        const auto c = selectedChip();
        const auto sock = quint8(socket->currentIndex());
        const auto addr = number(startAddress, "地址");
        const auto count = number(byteCount, "长度");
        const auto data = hex->buffer();
        const int speed = spiSpeed->currentIndex();
        const bool sim = mode->currentIndex() == 1;
        if ((name == "写入" || name == "校验" || name == "自动") && data.isEmpty())
            fail("缓冲区为空");
        if (name == "自动" && addr != 0)
            fail("自动流程使用全片擦除，请将起始地址设置为 0");
        runTask(name, [this, name, c, sock, addr, count, data, speed, sim] {
            TaskResult r;
            engine.configure(c);
            if (speed && c.type == 1)
                engine.speed(speed);
            if (name == "识别")
                r.detected = engine.detect();
            else if (name == "读取") {
                r.buffer = engine.read(c, sock, addr, count);
                r.hasBuffer = true;
            } else if (name == "查空")
                engine.blank(c, sock, addr, count);
            else if (name == "擦除")
                engine.erase(c, sock);
            else if (name == "写入")
                engine.write(c, sock, addr, data);
            else if (name == "校验")
                engine.verify(c, sock, addr, data);
            else if (name == "自动") {
                if (data.size() > c.size || data.size() % c.page)
                    fail("数据容量或页对齐错误，请在擦除前调整缓冲区");
                engine.erase(c, sock);
                engine.write(c, sock, addr, data);
            }
            r.message = name + (sim ? "（模拟）" : "");
            return r;
        });
    } catch (const std::exception &e) {
        appendLog(e.what());
    }
}
bool MainWindow::mayDiscard() {
    if (!dirty)
        return true;
    auto answer = QMessageBox::question(this, "缓冲区尚未保存", "保存当前缓冲区的修改？",
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                        QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        saveBuffer(false);
        return !dirty;
    }
    return true;
}
void MainWindow::openBuffer() {
    if (!mayDiscard())
        return;
    auto path = QFileDialog::getOpenFileName(this, "打开固件", {}, "固件 (*.bin *.rom *.hex);;所有文件 (*)");
    if (path.isEmpty())
        return;
    try {
        auto b = Buffer::load(path);
        hex->setBytes(b);
        currentFile = path;
        dirty = false;
        findOffset = 0;
        updateBuffer();
        appendLog(QString("载入 %1（%2 字节）").arg(path).arg(b.size()));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "打开失败", e.what());
    }
}
void MainWindow::saveBuffer(bool as) {
    auto path = currentFile;
    if (as || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "保存缓冲区", path.isEmpty() ? "dump.bin" : path,
                                            "二进制 (*.bin);;Intel HEX (*.hex);;所有文件 (*)");
    if (path.isEmpty())
        return;
    try {
        Buffer::save(path, hex->buffer());
        currentFile = path;
        dirty = false;
        updateBuffer();
        appendLog("已保存：" + path);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "保存失败", e.what());
    }
}
void MainWindow::newBuffer() {
    if (!mayDiscard())
        return;
    try {
        auto n = number(byteCount, "长度");
        if (!n)
            fail("长度不能为 0");
        hex->setBytes(QByteArray(n, char(0xff)));
        dirty = true;
        currentFile.clear();
        updateBuffer();
    } catch (const std::exception &e) {
        appendLog(e.what());
    }
}
void MainWindow::fillBuffer() {
    if (hex->buffer().isEmpty())
        return;
    bool ok;
    auto s =
        QInputDialog::getText(this, "填充缓冲区", "十六进制字节（例如 FF）", QLineEdit::Normal, "FF", &ok);
    if (!ok)
        return;
    auto n = s.toUInt(&ok, 16);
    if (!ok || n > 255) {
        appendLog("填充值无效");
        return;
    }
    hex->setBytes(QByteArray(hex->buffer().size(), char(n)));
    dirty = true;
    updateBuffer();
}
void MainWindow::padBuffer() {
    try {
        auto c = selectedChip();
        auto b = hex->buffer();
        if (b.isEmpty())
            fail("缓冲区为空");
        auto n = ((b.size() + c.page - 1) / c.page) * c.page;
        if (n > c.size)
            fail("补齐后超过芯片容量");
        b.append(QByteArray(n - b.size(), char(0xff)));
        hex->setBytes(b);
        dirty = true;
        updateBuffer();
    } catch (const std::exception &e) {
        appendLog(e.what());
    }
}
void MainWindow::findBytes() {
    bool ok;
    auto s = QInputDialog::getText(this, "查找字节", "十六进制序列（例如 EF 40 18），再次查找从下一处开始",
                                   QLineEdit::Normal, lastSearch, &ok);
    if (!ok)
        return;
    auto h = s;
    h.remove(QRegularExpression("\\s"));
    if (h.isEmpty() || h.size() % 2 || h.contains(QRegularExpression("[^0-9a-fA-F]"))) {
        appendLog("查找序列无效");
        return;
    }
    if (s != lastSearch)
        findOffset = 0;
    lastSearch = s;
    auto needle = QByteArray::fromHex(h.toLatin1());
    auto pos = hex->buffer().indexOf(needle, findOffset);
    if (pos < 0 && findOffset)
        pos = hex->buffer().indexOf(needle);
    if (pos < 0) {
        appendLog("未找到字节序列");
        return;
    }
    findOffset = pos + 1;
    auto index = hex->index(int(pos / 16), int(pos % 16));
    table->setCurrentIndex(index);
    table->scrollTo(index);
    appendLog(QString("找到 @ 0x%1").arg(pos, 8, 16, QChar('0')));
}
void MainWindow::gotoOffset() {
    bool ok;
    auto s = QInputDialog::getText(this, "跳转", "缓冲区偏移（十六进制）", QLineEdit::Normal, "0", &ok);
    if (!ok)
        return;
    auto n = s.toULongLong(&ok, 16);
    if (!ok || n >= quint64(hex->buffer().size())) {
        appendLog("偏移超出缓冲区");
        return;
    }
    auto i = hex->index(int(n / 16), int(n % 16));
    table->setCurrentIndex(i);
    table->scrollTo(i);
}
void MainWindow::importDatabase() {
    auto path = QFileDialog::getOpenFileName(this, "导入芯片库", {}, "芯片库 (*.json *.yg)");
    if (path.isEmpty())
        return;
    try {
        ChipDatabase incoming;
        if (path.endsWith(".yg", Qt::CaseInsensitive)) {
            auto exe = QFileDialog::getOpenFileName(this, "选择已解包的 20250524 版 XTW-5.exe",
                                                    QFileInfo(path).absolutePath() + "/../XTW-5.exe",
                                                    "主程序 (*.exe)");
            if (exe.isEmpty())
                return;
            incoming.importVendor(path, exe);
        } else
            incoming.loadJson(path);
        incoming.saveJson(configDirectory() + "/chips.json");
        database = incoming;
        database.chips.prepend(ChipDatabase::demoChip());
        refreshManufacturers();
        search->clear();
        rebuildChips();
        appendLog(QString("已导入 %1 条记录；型号记录不代表已通过实机验证").arg(incoming.chips.size()));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "导入失败", e.what());
    }
}
void MainWindow::addChip() {
    QDialog d(this);
    d.setWindowTitle("自定义 SPI NOR / I²C EEPROM");
    auto layout = new QFormLayout(&d);
    QLineEdit name, manufacturer, size, page, voltage, jedec, flags, flags2;
    QComboBox type;
    type.addItems({"SPI NOR", "I²C EEPROM"});
    name.setText("CUSTOM");
    manufacturer.setText("自定义");
    size.setText("1048576");
    page.setText("256");
    voltage.setText("3300");
    jedec.setText("000000");
    flags.setText("00");
    flags2.setText("00");
    layout->addRow("型号", &name);
    layout->addRow("厂商", &manufacturer);
    layout->addRow("类型", &type);
    layout->addRow("容量（字节）", &size);
    layout->addRow("页大小（字节）", &page);
    layout->addRow("电压（mV）", &voltage);
    layout->addRow("JEDEC ID（十六进制）", &jedec);
    layout->addRow("厂家 flags（十六进制）", &flags);
    layout->addRow("厂家 flags2（十六进制）", &flags2);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    try {
        auto parse = [](QLineEdit &e, int base, quint64 max) {
            bool ok;
            auto n = e.text().toULongLong(&ok, base);
            if (!ok || n > max)
                fail("自定义参数不合法");
            return n;
        };
        Chip c;
        c.name = name.text().trimmed();
        c.manufacturer = manufacturer.text().trimmed();
        c.type = type.currentIndex() + 1;
        c.size = parse(size, 10, 256 * 1024 * 1024);
        c.page = parse(page, 10, 8192);
        c.voltage = parse(voltage, 10, 65535);
        c.jedec = parse(jedec, 16, 0xffffff);
        c.flags = parse(flags, 16, 255);
        c.flags2 = parse(flags2, 16, 255);
        c.validate();
        database.chips.append(c);
        ChipDatabase save = database;
        save.chips.removeFirst();
        save.saveJson(configDirectory() + "/chips.json");
        familyFilter->setCurrentIndex(0);
        refreshManufacturers();
        manufacturerFilter->setCurrentIndex(0);
        search->setText(c.name);
        rebuildChips();
        appendLog("已添加自定义芯片：" + c.name);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "参数错误", e.what());
    }
}
void MainWindow::exportLog() {
    auto path = QFileDialog::getSaveFileName(this, "导出操作日志", "xtw5-session.log", "日志 (*.log)");
    if (path.isEmpty())
        return;
    try {
        saveFile(path, logView->toPlainText().toUtf8());
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "导出失败", e.what());
    }
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (busy) {
        engine.cancelled = true;
        phase->setText("正在停止，请稍后关闭");
        e->ignore();
        return;
    }
    if (!mayDiscard()) {
        e->ignore();
        return;
    }
    usbTimer->stop();
    QSettings().setValue("compactGeometry", saveGeometry());
    QSettings().setValue("autoSelectMatch", autoSelect->isChecked());
    engine.disconnect();
    e->accept();
}
