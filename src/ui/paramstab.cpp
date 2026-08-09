#include "ui/paramstab.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QHeaderView>
#include <QTableWidget>

#include <cmath>

namespace {

enum SectorColumn { SectorOn, SectorStart, SectorStop, SectorColCount };
enum ZoneColumn { ZoneOn, ZoneAzm1, ZoneAzm2, ZoneRange1, ZoneRange2,
                  ZoneColCount };

const QColor kBadRow(90, 30, 34);

/// Ô chỉ để chứa widget: khoá sửa và khoá chọn, nếu không thì kích vào ô là mở
/// một ô nhập chữ chồng lên chính widget đang nằm đó.
QTableWidgetItem *makeHostItem()
{
    auto *it = new QTableWidgetItem;
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

/// Đặt một widget vào giữa ô.
QWidget *centred(QWidget *inner, QWidget *parent)
{
    auto *host = new QWidget(parent);
    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setAlignment(Qt::AlignCenter);
    lay->addWidget(inner);
    return host;
}

/// Ô nhập số trong bảng. Một chữ số thập phân — đúng độ làm tròn 0.1 mà mô tả
/// giai đoạn yêu cầu cho cả góc lẫn cự ly.
QDoubleSpinBox *cellSpin(QWidget *parent, double max, double value)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(1);
    s->setRange(0.0, max);
    s->setSingleStep(1.0);
    s->setValue(value);
    s->setFrame(false);
    return s;
}

double spinAt(const QTableWidget *t, int row, int col)
{
    auto *s = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, col));
    return s ? s->value() : 0.0;
}

bool checkAt(const QTableWidget *t, int row, int col)
{
    QWidget *host = t->cellWidget(row, col);
    const QCheckBox *b = host ? host->findChild<QCheckBox *>() : nullptr;
    return b && b->isChecked();
}

} // namespace

ParamsTab::ParamsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // --- Tham số kỹ thuật -------------------------------------------------
    auto *box = new QGroupBox(tr("Tham số kỹ thuật"), this);
    auto *form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignLeft);

    m_fs = new QDoubleSpinBox(box);
    m_fs->setDecimals(3);
    m_fs->setRange(AppParams::kFsMin, AppParams::kFsMax);
    m_fs->setSingleStep(0.1);
    form->addRow(tr("Tần số ADC (MHz) — Fs"), m_fs);

    m_b = new QSpinBox(box);
    m_b->setRange(AppParams::kBMin, AppParams::kBMax);
    form->addRow(tr("Giải thông điều tần (MHz) — B"), m_b);

    m_tc = new QSpinBox(box);
    m_tc->setRange(AppParams::kTcMin, AppParams::kTcMax);
    m_tc->setSingleStep(100);
    form->addRow(tr("Chu kỳ kích (µs) — Tc"), m_tc);

    // ZFbeat không còn nhập ở đây: từ giai đoạn này nó là một trường của lệnh
    // CMD_DSP_R, nhập trong tab "Điều khiển". Còn lại đúng một hệ số của riêng
    // phần mềm — chỉnh độ sáng nền tạp mà không đụng gì tới đài.
    m_multiV = new QDoubleSpinBox(box);
    m_multiV->setDecimals(3);
    m_multiV->setRange(AppParams::kMultiVMin, AppParams::kMultiVMax);
    m_multiV->setSingleStep(0.1);
    m_multiV->setToolTip(tr("Nhân thêm vào Video[1024] sau khi đã chia cho "
                            "ZFbeat (hoặc GainU ở chế độ Doppler)"));
    form->addRow(tr("Hệ số nhân video — Multi_V"), m_multiV);

    // --- Thang cự ly ------------------------------------------------------
    auto *rangeBox = new QGroupBox(tr("Thang cự ly"), this);
    auto *rangeLay = new QVBoxLayout(rangeBox);

    m_autoStatus = new QCheckBox(tr("Tự động nhận từ trạng thái lệnh điều khiển"),
                                 rangeBox);
    m_autoStatus->setToolTip(tr("Lấy Fs, B, Tc từ gói trạng thái lệnh điều "
                                "khiển thay vì nhập tay"));
    m_autoRange = new QCheckBox(tr("Tự động cập nhật thang cự ly"), rangeBox);
    m_autoRange->setToolTip(tr("Tính cự ly tối đa từ Fs, B, Tc rồi cập nhật "
                               "sang tab Cài đặt"));

    m_rmax = new QLabel(rangeBox);
    m_rmax->setStyleSheet(QStringLiteral("color: #7fa8c9;"));

    // Nút "Áp dụng" nằm trong chính group thang cự ly, góc dưới bên phải: nó
    // chỉ có tác dụng với nhóm tham số này, để rời ra ngoài thì trông như nó
    // áp dụng cho cả tab.
    m_apply = new QPushButton(tr("Áp dụng"), rangeBox);
    auto *applyRow = new QHBoxLayout;
    applyRow->addStretch(1);
    applyRow->addWidget(m_apply, 0);

    rangeLay->addWidget(m_autoStatus);
    rangeLay->addWidget(m_autoRange);
    rangeLay->addWidget(m_rmax);
    rangeLay->addLayout(applyRow);

    // --- Xử lý theo rẻ quạt ----------------------------------------------
    auto *sectorBox = new QGroupBox(tr("Xử lý theo rẻ quạt"), this);
    auto *sectorLay = new QVBoxLayout(sectorBox);

    m_sectors = new QTableWidget(0, SectorColCount, sectorBox);
    m_sectors->setHorizontalHeaderLabels({tr("Áp dụng"), tr("Góc bắt đầu (°)"),
                                          tr("Góc kết thúc (°)")});
    m_sectors->verticalHeader()->setVisible(false);
    m_sectors->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sectors->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sectors->horizontalHeader()->setStretchLastSection(true);
    m_sectors->setMinimumHeight(120);
    m_sectors->setToolTip(tr("Bỏ qua dữ liệu RAW_P ngoài mọi rẻ quạt đang bật. "
                             "Không dòng nào bật thì xử lý cả vòng tròn.\n"
                             "Góc tính theo chiều kim đồng hồ từ góc bắt đầu "
                             "tới góc kết thúc, nên bắt đầu > kết thúc là rẻ "
                             "quạt vắt qua hướng bắc."));
    sectorLay->addWidget(m_sectors);

    m_sectorAdd    = new QPushButton(tr("Thêm dòng"), sectorBox);
    m_sectorRemove = new QPushButton(tr("Xoá dòng"), sectorBox);
    auto *sectorBtnRow = new QHBoxLayout;
    sectorBtnRow->addWidget(m_sectorAdd);
    sectorBtnRow->addWidget(m_sectorRemove);
    sectorBtnRow->addStretch(1);
    sectorLay->addLayout(sectorBtnRow);

    m_sectorWarn = new QLabel(sectorBox);
    m_sectorWarn->setWordWrap(true);
    m_sectorWarn->setStyleSheet(QStringLiteral("color: #d47b6a;"));
    m_sectorWarn->hide();
    sectorLay->addWidget(m_sectorWarn);

    // --- Vùng cấm khởi tạo -------------------------------------------------
    auto *zoneBox = new QGroupBox(tr("Vùng cấm khởi tạo"), this);
    auto *zoneLay = new QVBoxLayout(zoneBox);

    m_showZones = new QCheckBox(tr("Hiện vùng cấm khởi tạo"), zoneBox);
    m_showZones->setToolTip(tr("Vẽ các vùng đang bật lên nền bản đồ"));
    zoneLay->addWidget(m_showZones);

    m_zones = new QTableWidget(0, ZoneColCount, zoneBox);
    m_zones->setHorizontalHeaderLabels({tr("Áp dụng"), tr("Phương vị đầu (°)"),
                                        tr("Phương vị cuối (°)"),
                                        tr("Cự ly đầu (m)"), tr("Cự ly cuối (m)")});
    m_zones->verticalHeader()->setVisible(false);
    m_zones->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_zones->setSelectionMode(QAbstractItemView::SingleSelection);
    m_zones->horizontalHeader()->setStretchLastSection(true);
    m_zones->setMinimumHeight(120);
    m_zones->setToolTip(tr("Không mở quỹ đạo mới cho điểm dấu rơi vào vùng "
                           "đang bật. Quỹ đạo đã có vẫn được bám tiếp khi bay "
                           "qua vùng."));
    zoneLay->addWidget(m_zones);

    m_zoneAdd    = new QPushButton(tr("Thêm dòng"), zoneBox);
    m_zoneRemove = new QPushButton(tr("Xoá dòng"), zoneBox);
    m_zoneDraw   = new QPushButton(tr("Vẽ trên bản đồ"), zoneBox);
    m_zoneDraw->setToolTip(tr("Bấm chuột trái hai lần trên bản đồ để chọn hai "
                              "góc của vùng; phương vị và cự ly tự tính ra và "
                              "thêm thành một dòng mới"));
    auto *zoneBtnRow = new QHBoxLayout;
    zoneBtnRow->addWidget(m_zoneAdd);
    zoneBtnRow->addWidget(m_zoneRemove);
    zoneBtnRow->addStretch(1);
    zoneBtnRow->addWidget(m_zoneDraw);
    zoneLay->addLayout(zoneBtnRow);

    // --- Tham số thuật toán xử lý ----------------------------------------
    auto *algoBox = new QGroupBox(tr("Tham số thuật toán xử lý"), this);
    auto *algoLay = new QVBoxLayout(algoBox);

    auto *beamBtn = new QPushButton(tr("Tham số chùm xung..."), algoBox);
    beamBtn->setToolTip(tr("Các tham số tính tâm chùm xung (điểm dấu)"));
    auto *trackBtn = new QPushButton(tr("Tham số quỹ đạo..."), algoBox);
    trackBtn->setToolTip(tr("Bộ lọc Kalman và quản lý danh sách quỹ đạo"));
    algoLay->addWidget(beamBtn);
    algoLay->addWidget(trackBtn);

    root->addWidget(box);
    root->addWidget(rangeBox);
    root->addWidget(sectorBox);
    root->addWidget(zoneBox);
    root->addWidget(algoBox);
    root->addStretch(1);

    connect(beamBtn, &QPushButton::clicked, this, &ParamsTab::beamParamsRequested);
    connect(trackBtn, &QPushButton::clicked, this, &ParamsTab::trackParamsRequested);

    // --- Nối tín hiệu -----------------------------------------------------
    // Dòng cự ly tính được đi theo ô nhập ngay, để thấy trước kết quả rồi mới
    // quyết định có bấm "Áp dụng" hay không.
    connect(m_fs, &QDoubleSpinBox::valueChanged, this, &ParamsTab::onEdited);
    connect(m_b,  &QSpinBox::valueChanged,       this, &ParamsTab::onEdited);
    connect(m_tc, &QSpinBox::valueChanged,       this, &ParamsTab::onEdited);

    // Hệ số nhân video không dính gì tới thang cự ly nên không chờ nút "Áp
    // dụng": vặn tới đâu nền tạp sáng tối theo tới đó, đúng cách người ta chỉnh
    // một cái núm độ sáng.
    connect(m_multiV, &QDoubleSpinBox::valueChanged, this, &ParamsTab::apply);

    connect(m_autoStatus, &QCheckBox::toggled, this, [this](bool on) {
        // Nhận tự động thì thang cự ly cũng phải tự động, nếu không giá trị
        // nhận về sẽ không khớp với vòng tròn cự ly đang vẽ. Khoá luôn ô đó
        // lại để ràng buộc này nhìn là thấy.
        if (on)
            m_autoRange->setChecked(true);
        m_autoRange->setEnabled(!on);
        refreshDerived();
        apply();
    });
    connect(m_autoRange, &QCheckBox::toggled, this, &ParamsTab::apply);
    connect(m_apply, &QPushButton::clicked, this, &ParamsTab::apply);

    // Rẻ quạt và vùng cấm có hiệu lực ngay, không chờ nút "Áp dụng": nút đó
    // thuộc về nhóm thang cự ly, mà hai bảng này lại là thứ hay phải chỉnh đi
    // chỉnh lại lúc đang theo dõi một mục tiêu.
    connect(m_sectorAdd, &QPushButton::clicked, this, [this] {
        m_params.sectors.push_back(Sector{});
        rebuildSectorTable();
        apply();
    });
    connect(m_sectorRemove, &QPushButton::clicked, this, [this] {
        const int r = m_sectors->currentRow();
        // Bảng rỗng thì không thêm lại dòng nào được, nên giữ tối thiểu một dòng.
        if (r < 0 || r >= m_params.sectors.size() || m_params.sectors.size() <= 1)
            return;
        m_params.sectors.remove(r);
        rebuildSectorTable();
        apply();
    });

    connect(m_showZones, &QCheckBox::toggled, this, &ParamsTab::apply);
    connect(m_zoneAdd, &QPushButton::clicked, this, [this] {
        m_params.noInitZones.push_back(NoInitZone{});
        rebuildZoneTable();
        apply();
    });
    connect(m_zoneRemove, &QPushButton::clicked, this, [this] {
        const int r = m_zones->currentRow();
        if (r < 0 || r >= m_params.noInitZones.size())
            return;
        m_params.noInitZones.remove(r);
        rebuildZoneTable();
        apply();
    });
    connect(m_zoneDraw, &QPushButton::clicked, this, &ParamsTab::drawZoneRequested);

    setParams(m_params);
}

double ParamsTab::previewRmaxKm() const
{
    return std::round(AppParams::rmaxMeters(m_fs->value(), m_b->value(),
                                            m_tc->value())) / 1000.0;
}

void ParamsTab::refreshDerived()
{
    m_rmax->setText(tr("Cự ly tối đa tính được: %1 km (%2 m)")
                        .arg(previewRmaxKm(), 0, 'f', 3)
                        .arg(std::round(AppParams::rmaxMeters(
                            m_fs->value(), m_b->value(), m_tc->value()))));

    // Nhận tự động thì không cần nút bấm nữa: mọi thay đổi vào thẳng.
    m_apply->setEnabled(!m_autoStatus->isChecked());
}

void ParamsTab::onEdited()
{
    refreshDerived();

    // Không có nút "Áp dụng" để bấm thì phải tự áp dụng.
    if (!m_loading && m_autoStatus->isChecked())
        apply();
}

void ParamsTab::rebuildSectorTable()
{
    m_loading = true;
    m_sectors->setRowCount(0);
    m_sectors->setRowCount(m_params.sectors.size());

    for (int r = 0; r < m_params.sectors.size(); ++r) {
        const Sector &s = m_params.sectors[r];

        auto *on = new QCheckBox(m_sectors);
        on->setChecked(s.on);
        m_sectors->setItem(r, SectorOn, makeHostItem());
        m_sectors->setCellWidget(r, SectorOn, centred(on, m_sectors));
        connect(on, &QCheckBox::toggled, this, &ParamsTab::apply);

        for (const auto &[col, value] : {std::pair{SectorStart, s.startDeg},
                                         std::pair{SectorStop,  s.stopDeg}}) {
            auto *spin = cellSpin(m_sectors, 360.0, value);
            m_sectors->setItem(r, col, makeHostItem());
            m_sectors->setCellWidget(r, col, spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, &ParamsTab::apply);
        }
    }
    m_sectors->resizeColumnsToContents();
    m_loading = false;
}

bool ParamsTab::readSectorTable()
{
    QVector<Sector> rows;
    rows.reserve(m_sectors->rowCount());
    for (int r = 0; r < m_sectors->rowCount(); ++r) {
        Sector s;
        s.on       = checkAt(m_sectors, r, SectorOn);
        s.startDeg = spinAt(m_sectors, r, SectorStart);
        s.stopDeg  = spinAt(m_sectors, r, SectorStop);
        rows.push_back(s);
    }

    // Chồng lấn xét trên **mọi** dòng, không chỉ dòng đang bật: một bảng có hai
    // rẻ quạt lồng nhau là cấu hình khó hiểu dù hôm nay dòng nào đang tắt, và
    // bật lên rồi mới báo lỗi thì người dùng đã quên mình nhập gì.
    QSet<int> bad;
    for (int i = 0; i < rows.size(); ++i) {
        for (int j = i + 1; j < rows.size(); ++j) {
            if (sectorsOverlap(rows.at(i), rows.at(j))) {
                bad.insert(i);
                bad.insert(j);
            }
        }
    }

    for (int r = 0; r < m_sectors->rowCount(); ++r) {
        const QBrush brush = bad.contains(r) ? QBrush(kBadRow) : QBrush();
        for (int c = 0; c < SectorColCount; ++c) {
            if (QTableWidgetItem *it = m_sectors->item(r, c))
                it->setBackground(brush);
        }
    }

    if (!bad.isEmpty()) {
        // Nói ra chỗ sai rồi để người dùng tự sửa, không hiện hộp thoại: họ còn
        // đang gõ dở, cắt ngang từng lần chỉnh một thì không nhập nổi.
        m_sectorWarn->setText(tr("Các rẻ quạt tô đỏ chồng lấn lên nhau — sửa "
                                 "lại cho chúng tách rời rồi mới có hiệu lực"));
        m_sectorWarn->show();
        return false;
    }

    m_sectorWarn->hide();
    m_params.sectors = rows;
    return true;
}

void ParamsTab::rebuildZoneTable()
{
    m_loading = true;
    m_zones->setRowCount(0);
    m_zones->setRowCount(m_params.noInitZones.size());

    for (int r = 0; r < m_params.noInitZones.size(); ++r) {
        const NoInitZone &z = m_params.noInitZones[r];

        auto *on = new QCheckBox(m_zones);
        on->setChecked(z.on);
        m_zones->setItem(r, ZoneOn, makeHostItem());
        m_zones->setCellWidget(r, ZoneOn, centred(on, m_zones));
        connect(on, &QCheckBox::toggled, this, &ParamsTab::apply);

        // Cự ly để trần rộng (1000 km) chứ không theo cự ly tối đa hiện hành:
        // đổi thang cự ly mà các vùng đã nhập bị kẹp lại thì mất số người dùng gõ.
        const std::pair<int, std::pair<double, double>> cols[] = {
            {ZoneAzm1,   {360.0,     z.azm1}},
            {ZoneAzm2,   {360.0,     z.azm2}},
            {ZoneRange1, {1000000.0, z.range1}},
            {ZoneRange2, {1000000.0, z.range2}},
        };
        for (const auto &[col, spec] : cols) {
            auto *spin = cellSpin(m_zones, spec.first, spec.second);
            m_zones->setItem(r, col, makeHostItem());
            m_zones->setCellWidget(r, col, spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, &ParamsTab::apply);
        }
    }
    m_zones->resizeColumnsToContents();
    m_loading = false;
}

void ParamsTab::readZoneTable()
{
    QVector<NoInitZone> rows;
    rows.reserve(m_zones->rowCount());
    for (int r = 0; r < m_zones->rowCount(); ++r) {
        NoInitZone z;
        z.on     = checkAt(m_zones, r, ZoneOn);
        z.azm1   = spinAt(m_zones, r, ZoneAzm1);
        z.azm2   = spinAt(m_zones, r, ZoneAzm2);
        z.range1 = spinAt(m_zones, r, ZoneRange1);
        z.range2 = spinAt(m_zones, r, ZoneRange2);
        // Người dùng gõ tay có thể để cự ly ngược; đổi chỗ chứ không báo lỗi,
        // ý định thì đã rõ ràng.
        if (z.range1 > z.range2)
            std::swap(z.range1, z.range2);
        rows.push_back(z);
    }
    m_params.noInitZones = rows;
}

void ParamsTab::addZoneFromMap(const NoInitZone &z)
{
    m_params.noInitZones.push_back(z);
    rebuildZoneTable();
    apply();
}

void ParamsTab::setDrawingZone(bool on)
{
    m_zoneDraw->setText(on ? tr("Huỷ vẽ") : tr("Vẽ trên bản đồ"));
}

void ParamsTab::setParams(const AppParams &p)
{
    m_loading = true;
    m_params = p;

    m_fs->setValue(p.fs);
    m_b->setValue(p.b);
    m_tc->setValue(p.tc);
    m_multiV->setValue(p.multiV);
    m_autoStatus->setChecked(p.autoFromStatus);
    m_autoRange->setChecked(p.autoRange || p.autoFromStatus);
    m_autoRange->setEnabled(!p.autoFromStatus);

    m_showZones->setChecked(p.showNoInitZones);

    m_loading = false;
    rebuildSectorTable();
    rebuildZoneTable();
    refreshDerived();
}

void ParamsTab::setFs(double fs)
{
    m_loading = true;
    m_params.fs = fs;
    m_fs->setValue(fs);
    m_loading = false;
    refreshDerived();
}

void ParamsTab::apply()
{
    if (m_loading)
        return;

    m_params.fs     = m_fs->value();
    m_params.b      = m_b->value();
    m_params.tc     = m_tc->value();
    m_params.multiV = m_multiV->value();
    m_params.autoFromStatus = m_autoStatus->isChecked();
    m_params.autoRange      = m_autoRange->isChecked();

    // Bảng rẻ quạt sai thì **không** áp dụng gì cả: nửa số dòng vào mà nửa kia
    // bị chặn thì thứ đang chạy không còn khớp với thứ đang hiện trên bảng.
    if (!readSectorTable())
        return;
    readZoneTable();
    m_params.showNoInitZones = m_showZones->isChecked();

    emit paramsApplied(m_params);
}
