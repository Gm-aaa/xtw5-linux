#pragma once
#include <QAbstractTableModel>
class HexModel : public QAbstractTableModel {
    Q_OBJECT
    QByteArray bytes;
    bool editable = true;

  public:
    explicit HexModel(QObject *p = nullptr) : QAbstractTableModel(p) {}
    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : int((bytes.size() + 15) / 16);
    }
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : 17; }
    QVariant data(const QModelIndex &, int role) const override;
    QVariant headerData(int, Qt::Orientation, int) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    bool setData(const QModelIndex &, const QVariant &, int) override;
    void setBytes(QByteArray b) {
        beginResetModel();
        bytes = std::move(b);
        endResetModel();
    }
    const QByteArray &buffer() const { return bytes; }
    void setEditable(bool v) { editable = v; }
  signals:
    void modified();
};
