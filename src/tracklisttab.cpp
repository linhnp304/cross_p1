#include "tracklisttab.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

/// Cột của bảng. ColId ẩn đi, chỉ để tra ngược từ dòng ra quỹ đạo.
enum Column {
    ColId, ColWatch, ColTop, ColPos, ColSpeed, ColHeading,
    ColAltitude, ColClassify, ColRemove,
    ColCount
};

QTableWidgetItem *readOnlyCell(const QString &text = {})
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return it;
}

QTableWidgetItem *editableCell(const QString &text = {})
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    return it;
}

/// Bảng có ô đang mở ô nhập tại chỗ hay không.
///
/// QAbstractItemView::state() là protected nên không hỏi thẳng được. Ô nhập
/// tại chỗ của một ô chữ là một QLineEdit con của bảng; các widget đặt sẵn
/// trong ô (checkbox, combobox, nút bấm) đều không phải QLineEdit nên không
/// nhầm được.
bool isEditingCell(const QTableWidget *table)
{
    const QWidget *f = QApplication::focusWidget();
    return f && qobject_cast<const QLineEdit *>(f) && table->isAncestorOf(f);
}

} // namespace

TrackListTab::TrackListTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("id"), tr("Theo dõi"), tr("Tốp"), tr("VT"), tr("V"),
        tr("H"), tr("ĐC"), tr("Loại"), tr("Xoá"),
    });
    m_table->setColumnHidden(ColId, true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    auto *plotBtn        = new QPushButton(tr("Thông tin chi tiết điểm dấu"), this);
    auto *clearPlotsBtn  = new QPushButton(tr("Xoá toàn bộ điểm dấu"), this);
    auto *clearTracksBtn = new QPushButton(tr("Xoá toàn bộ quỹ đạo"), this);
    clearPlotsBtn->setToolTip(tr("Xoá lớp điểm dấu đang vẽ trên bản đồ. Điểm dấu "
                                 "mới vẫn hiện ra bình thường ngay sau đó."));
    clearTracksBtn->setToolTip(tr("Xoá mọi quỹ đạo đang có, kèm cả số đầu tốp, "
                                  "độ cao và phân loại đã nhập tay. Hệ thống "
                                  "nhận cũng được báo trạng thái xoá."));

    // Ba nút xếp dọc trong **một cột** của lưới: bề rộng cột bằng nút có chữ
    // dài nhất, và cả ba tự giãn cho đầy cột nên luôn bằng nhau — không phải đo
    // chữ bằng tay, và đổi nhãn hay đổi ngôn ngữ vẫn còn đúng. Cột thứ hai để
    // trống và nhận hết phần giãn, giữ cho nút không kéo dài hết bề ngang tab.
    auto *btnGrid = new QGridLayout;
    btnGrid->setContentsMargins(0, 0, 0, 0);
    btnGrid->addWidget(plotBtn,        0, 0);
    btnGrid->addWidget(clearPlotsBtn,  1, 0);
    btnGrid->addWidget(clearTracksBtn, 2, 0);
    btnGrid->setColumnStretch(1, 1);
    root->addLayout(btnGrid);

    connect(plotBtn, &QPushButton::clicked, this, &TrackListTab::plotListRequested);
    connect(clearPlotsBtn, &QPushButton::clicked,
            this, &TrackListTab::clearPlotsRequested);
    connect(clearTracksBtn, &QPushButton::clicked,
            this, &TrackListTab::clearTracksRequested);
    connect(m_table, &QTableWidget::itemChanged, this, &TrackListTab::onItemChanged);

    // Chỉ kích đúp vào cột **không sửa được** mới mở popup thông tin: cột sửa
    // được thì kích đúp là mở ô nhập, hai chức năng đè lên nhau.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == ColPos || c == ColSpeed || c == ColHeading) {
            if (const quint32 id = idAt(r); id != 0)
                emit trackActivated(id);
        }
    });
}

void TrackListTab::setSettings(const AppSettings &s)
{
    const bool namesChanged = s.classifyNames != m_settings.classifyNames;
    m_settings = s;
    if (namesChanged) {
        // Danh sách phân loại đổi thì các ComboBox trong bảng đã cũ — dựng lại
        // cả bảng, rẻ hơn là đi sửa từng ComboBox một.
        m_ids.clear();
        m_table->setRowCount(0);
    }
}

quint32 TrackListTab::idAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount())
        return 0;
    const QTableWidgetItem *it = m_table->item(row, ColId);
    return it ? it->text().toUInt() : 0;
}

void TrackListTab::setTracks(const QVector<Track> &tracks)
{
    // Tập quỹ đạo đổi (thêm / bớt / đổi thứ tự) thì dựng lại; còn không thì
    // chỉ ghi lại chữ, giữ nguyên các widget trong ô và ô đang được sửa dở.
    QVector<quint32> ids;
    ids.reserve(tracks.size());
    for (const Track &t : tracks)
        ids.push_back(t.id);

    if (ids != m_ids) {
        rebuild(tracks);
        return;
    }

    m_loading = true;
    for (int i = 0; i < tracks.size(); ++i)
        refreshRow(i, tracks.at(i));
    m_loading = false;
}

void TrackListTab::rebuild(const QVector<Track> &tracks)
{
    m_loading = true;

    m_ids.clear();
    m_ids.reserve(tracks.size());
    m_table->setRowCount(0);
    m_table->setRowCount(tracks.size());

    for (int i = 0; i < tracks.size(); ++i) {
        const Track &t = tracks.at(i);
        m_ids.push_back(t.id);
        const quint32 id = t.id;

        m_table->setItem(i, ColId, readOnlyCell(QString::number(id)));

        // Ô checkbox căn giữa: đặt thẳng QCheckBox vào ô thì nó dính mép trái.
        auto *watchHost = new QWidget(m_table);
        auto *watchLay = new QHBoxLayout(watchHost);
        watchLay->setContentsMargins(0, 0, 0, 0);
        watchLay->setAlignment(Qt::AlignCenter);
        auto *watch = new QCheckBox(watchHost);
        watch->setChecked(t.watched);
        watchLay->addWidget(watch);
        m_table->setCellWidget(i, ColWatch, watchHost);
        connect(watch, &QCheckBox::toggled, this,
                [this, id](bool on) { emit watchChanged(id, on); });

        m_table->setItem(i, ColTop, editableCell());
        m_table->setItem(i, ColPos, readOnlyCell());
        m_table->setItem(i, ColSpeed, readOnlyCell());
        m_table->setItem(i, ColHeading, readOnlyCell());
        m_table->setItem(i, ColAltitude, editableCell());

        auto *combo = new QComboBox(m_table);
        combo->addItem(tr("Chưa xác định"), 0);
        for (int k = 0; k < m_settings.classifyNames.size(); ++k)
            combo->addItem(m_settings.classifyNames.at(k), k + 1);
        m_table->setCellWidget(i, ColClassify, combo);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, id](int) {
            if (!m_loading)
                emit classifyChanged(id, combo->currentData().toUInt());
        });

        auto *del = new QPushButton(tr("Xoá"), m_table);
        del->setFlat(true);
        m_table->setCellWidget(i, ColRemove, del);
        connect(del, &QPushButton::clicked, this,
                [this, id] { emit removeRequested(id); });

        refreshRow(i, t);
    }

    m_loading = false;
}

void TrackListTab::refreshRow(int row, const Track &t)
{
    // Ô đang được sửa dở thì để yên: ghi đè vào giữa lúc trắc thủ đang gõ là
    // mất chữ vừa gõ.
    const QModelIndex editing = m_table->currentIndex();
    const bool editingNow = isEditingCell(m_table) && editing.row() == row;
    const bool editingTop = editingNow && editing.column() == ColTop;
    const bool editingAlt = editingNow && editing.column() == ColAltitude;

    if (!editingTop)
        m_table->item(row, ColTop)->setText(QString::number(t.top));

    m_table->item(row, ColPos)->setText(
        QStringLiteral("%1° - %2 m").arg(t.azmDeg(), 0, 'f', 2)
                                    .arg(t.rangeM(), 0, 'f', 1));
    m_table->item(row, ColSpeed)->setText(
        QStringLiteral("%1").arg(t.speedMs(), 0, 'f', 1));
    m_table->item(row, ColHeading)->setText(
        QStringLiteral("%1°").arg(t.headingDeg(), 0, 'f', 2));

    if (!editingAlt)
        m_table->item(row, ColAltitude)->setText(QString::number(t.altitude()));

    if (auto *combo = qobject_cast<QComboBox *>(m_table->cellWidget(row, ColClassify))) {
        if (const int i = combo->findData(t.classify); i >= 0 && i != combo->currentIndex())
            combo->setCurrentIndex(i);
    }

    if (auto *host = m_table->cellWidget(row, ColWatch)) {
        if (auto *box = host->findChild<QCheckBox *>()) {
            if (box->isChecked() != t.watched) {
                QSignalBlocker block(box);
                box->setChecked(t.watched);
            }
        }
    }
}

void TrackListTab::onItemChanged(QTableWidgetItem *item)
{
    if (m_loading || !item)
        return;

    const quint32 id = idAt(item->row());
    if (id == 0)
        return;

    if (item->column() == ColTop) {
        bool ok = false;
        const quint32 v = item->text().toUInt(&ok);
        // Số đầu tốp trùng lặp thì nơi nhận sẽ báo và gọi lại setTracks, dòng
        // này sẽ tự về giá trị cũ — nên ở đây chỉ cần chặn giá trị vô nghĩa.
        if (ok && v > 0)
            emit topChangeRequested(id, v);
        else
            emit topChangeRequested(id, 0);   // 0 = không hợp lệ, nơi nhận báo lỗi
    } else if (item->column() == ColAltitude) {
        bool ok = false;
        const quint32 v = item->text().toUInt(&ok);
        emit altitudeChanged(id, ok ? v : 0);
    }
}

void TrackListTab::selectTrack(quint32 id)
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (idAt(r) != id)
            continue;
        m_table->selectRow(r);
        m_table->scrollToItem(m_table->item(r, ColPos));
        return;
    }
}
