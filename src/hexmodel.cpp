#include "hexmodel.h"
#include <QBrush>
#include <QColor>
QVariant HexModel::data(const QModelIndex &i, int role) const {
    if (!i.isValid())
        return {};
    const qsizetype offset = qsizetype(i.row()) * 16 + i.column();
    if (role == Qt::TextAlignmentRole)
        return int(i.column() == 16 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
    if (role == Qt::ForegroundRole && i.column() < 16 && offset < bytes.size() &&
        quint8(bytes[offset]) == 255)
        return QBrush(QColor("#8995a5"));
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (i.column() == 16) {
        QString text;
        for (auto b : bytes.mid(qsizetype(i.row()) * 16, 16))
            text += quint8(b) >= 32 && quint8(b) < 127 ? QChar(quint8(b)) : QChar('.');
        return text;
    }
    if (offset >= bytes.size())
        return {};
    return QString("%1").arg(quint8(bytes[offset]), 2, 16, QChar('0')).toUpper();
}
QVariant HexModel::headerData(int section, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    if (o == Qt::Vertical)
        return QString("%1").arg(quint64(section) * 16, 8, 16, QChar('0')).toUpper();
    if (section == 16)
        return "ASCII";
    return QString("%1").arg(section, 2, 16, QChar('0')).toUpper();
}
Qt::ItemFlags HexModel::flags(const QModelIndex &i) const {
    auto f = QAbstractTableModel::flags(i);
    if (editable && i.column() < 16 && qsizetype(i.row()) * 16 + i.column() < bytes.size())
        f |= Qt::ItemIsEditable;
    return f;
}
bool HexModel::setData(const QModelIndex &i, const QVariant &v, int role) {
    if (role != Qt::EditRole || !editable || !i.isValid() || i.column() >= 16)
        return false;
    const auto off = qsizetype(i.row()) * 16 + i.column();
    if (off >= bytes.size())
        return false;
    bool ok;
    auto s = v.toString().trimmed();
    auto n = s.toUInt(&ok, 16);
    if (!ok || s.size() > 2 || n > 255)
        return false;
    bytes[off] = char(n);
    emit dataChanged(index(i.row(), 0), index(i.row(), 16));
    emit modified();
    return true;
}
