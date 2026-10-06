#pragma once
#include "chip.h"
#include "programmer.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QProgressBar;
class QPlainTextEdit;
class QTableView;
class HexModel;
class QAction;
class QTimer;
struct UsbServices {
    std::function<QStringList()> enumerate = listUsbDevices;
    std::function<std::unique_ptr<Transport>(const Log &)> open = openUsb;
};
struct TaskResult {
    bool success = false, connected = false, hasBuffer = false;
    QString message;
    QByteArray buffer;
    quint32 detected = 0;
    QStringList info;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
    ChipDatabase database;
    Programmer engine;
    QFutureWatcher<TaskResult> watcher;
    QElapsedTimer elapsed;
    QComboBox *mode, *chips, *socket, *spiSpeed, *familyFilter, *manufacturerFilter;
    QLineEdit *search, *startAddress, *byteCount;
    QLabel *connection, *chipInfo, *bufferInfo, *digest, *phase, *validation;
    QPushButton *connectButton, *cancelButton;
    QCheckBox *trace, *adapter, *autoSelect;
    QProgressBar *progressBar;
    QPlainTextEdit *logView;
    QTableView *table;
    HexModel *hex;
    QList<QWidget *> operationWidgets;
    QList<QAction *> fileActions;
    bool busy = false, online = false, dirty = false;
    QString currentFile, lastSearch;
    qsizetype findOffset = 0;
    quint32 detectedFilter = 0;
    UsbServices usb;
    QTimer *usbTimer;
    QStringList observedUsb;
    QString activeUsb;
    int connectAttempts = 0;
    bool manualDisconnect = false, deviceRemoved = false;
    void scanUsb();
    void clearDetectedChip();
    QString configDirectory() const;
    Chip selectedChip() const;
    void buildUi();
    void loadDatabase();
    void rebuildChips();
    void refreshManufacturers();
    void updateChip();
    void updateBuffer();
    void setBusy(bool);
    void runTask(const QString &, std::function<TaskResult()>);
    void appendLog(const QString &);
    void finishTask();
    void connectDevice();
    void operation(const QString &);
    void openBuffer();
    void saveBuffer(bool as);
    void newBuffer();
    void fillBuffer();
    void padBuffer();
    void findBytes();
    void gotoOffset();
    void importDatabase();
    void addChip();
    void exportLog();
    bool mayDiscard();
    bool prepareHardware(const QString &);
    quint32 number(QLineEdit *, const QString &) const;

  protected:
    void closeEvent(QCloseEvent *) override;

  public:
    explicit MainWindow(bool demo = false, QWidget *parent = nullptr, UsbServices services = {});
    ~MainWindow() override;
};
