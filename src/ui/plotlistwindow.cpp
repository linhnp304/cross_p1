#include "ui/plotlistwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

namespace {

/// Cột của bảng, đúng thứ tự mô tả trong tài liệu giai đoạn.
enum Column {
    ColIndex, ColTime, ColPos, ColNumCX, ColNumLose,
    ColAzm1, ColAzm2, ColRange1, ColDopler1, ColAmpAvg, ColAmpCentre,
    ColCount
};

QTableWidgetItem *cell(const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

} // namespace

PlotListWindow::PlotListWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Danh sách điểm dấu"));
    setWindowFlag(Qt::Window);
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({
        tr("STT"), tr("Thời gian"), tr("PV-CL"), tr("NumCX"), tr("NumLose"),
        tr("Azm1"), tr("Azm2"), tr("Range1"), tr("Dopler1"),
        tr("Amp-Avg"), tr("Amp-C"),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    m_autoClear = new QCheckBox(tr("Tự động xoá khi đủ %1 dòng").arg(kAutoClearRows),
                                this);
    m_autoClear->setChecked(true);

    auto *clearBtn = new QPushButton(tr("Xoá danh sách"), this);
    auto *closeBtn = new QPushButton(tr("Đóng"), this);
    connect(clearBtn, &QPushButton::clicked, this, &PlotListWindow::clearList);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    auto *row = new QHBoxLayout;
    row->addWidget(m_autoClear, 0);
    row->addStretch(1);
    row->addWidget(clearBtn, 0);
    row->addWidget(closeBtn, 0);
    root->addLayout(row);

    // Vị trí ban đầu: giữa mép trên màn hình, ngang đủ xem hết cột, dọc bằng
    // một nửa chiều ngang.
    const QRect avail = QGuiApplication::primaryScreen()
                            ? QGuiApplication::primaryScreen()->availableGeometry()
                            : QRect(0, 0, 1280, 800);
    const int w = qMin(1100, avail.width() - 40);
    const int h = w / 2;
    resize(w, h);
    move(avail.x() + (avail.width() - w) / 2, avail.y() + 20);
}

void PlotListWindow::addPlots(const QVector<PlotTC> &plots)
{
    if (plots.isEmpty())
        return;

    // Đang ở đáy bảng thì bám theo dòng mới; kéo lên xem dòng cũ thì để yên,
    // không giật xuống dưới ngay giữa lúc đang đọc.
    QScrollBar *bar = m_table->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;

    if (m_autoClear->isChecked()
        && m_table->rowCount() + plots.size() > kAutoClearRows)
        m_table->setRowCount(0);

    m_table->setUpdatesEnabled(false);
    for (const PlotTC &p : plots) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);

        m_table->setItem(r, ColIndex, cell(QString::number(++m_counter)));
        m_table->setItem(r, ColTime,
            cell(QTime::fromMSecsSinceStartOfDay(int(p.timeMs % 86400000u))
                     .toString(QStringLiteral("HH:mm:ss.zzz"))));
        m_table->setItem(r, ColPos,
            cell(QStringLiteral("%1° - %2 m").arg(p.azmDeg(), 0, 'f', 2)
                                             .arg(p.rangeM(), 0, 'f', 1)));
        m_table->setItem(r, ColNumCX,     cell(QString::number(p.numCX)));
        m_table->setItem(r, ColNumLose,   cell(QString::number(p.numLoseTotal)));
        m_table->setItem(r, ColAzm1,      cell(QString::number(p.azmStart)));
        m_table->setItem(r, ColAzm2,      cell(QString::number(p.azmStop)));
        m_table->setItem(r, ColRange1,    cell(QString::number(p.rangeStart)));
        m_table->setItem(r, ColDopler1,   cell(QString::number(p.doplerStart)));
        m_table->setItem(r, ColAmpAvg,    cell(QString::number(p.amplitudeAverage)));
        m_table->setItem(r, ColAmpCentre, cell(QString::number(p.amplitudeCenter)));
    }
    m_table->setUpdatesEnabled(true);

    if (atBottom)
        m_table->scrollToBottom();
}

void PlotListWindow::clearList()
{
    m_table->setRowCount(0);
}
