#include "paramstab.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

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

    // ZFbeat toàn dải unsigned int, vượt quá dải của QSpinBox (int 32 bit).
    // QDoubleSpinBox với 0 chữ số thập phân hiện ra vẫn là số nguyên, mà double
    // thì thừa sức giữ nguyên vẹn số 32 bit.
    m_zfbeat = new QDoubleSpinBox(box);
    m_zfbeat->setDecimals(0);
    m_zfbeat->setRange(1.0, 4294967295.0);
    m_zfbeat->setSingleStep(256.0);
    form->addRow(tr("Hệ số căn chỉnh — ZFbeat"), m_zfbeat);

    // --- Thang cự ly ------------------------------------------------------
    auto *rangeBox = new QGroupBox(tr("Thang cự ly"), this);
    auto *rangeLay = new QVBoxLayout(rangeBox);

    m_autoStatus = new QCheckBox(tr("Tự động nhận từ trạng thái lệnh điều khiển"),
                                 rangeBox);
    m_autoStatus->setToolTip(tr("Lấy Fs, B, Tc, ZFbeat từ gói trạng thái lệnh "
                                "điều khiển thay vì nhập tay"));
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

    m_sectorOn = new QCheckBox(tr("Chỉ xử lý điểm dấu trong một góc rẻ quạt"),
                               sectorBox);
    m_sectorOn->setToolTip(tr("Bỏ qua dữ liệu RAW_P ngoài rẻ quạt. Góc tính "
                              "theo chiều kim đồng hồ từ góc bắt đầu tới góc "
                              "kết thúc, nên bắt đầu > kết thúc là rẻ quạt vắt "
                              "qua hướng bắc"));

    auto *sectorForm = new QFormLayout;
    m_sectorStart = new QDoubleSpinBox(sectorBox);
    m_sectorStart->setDecimals(2);
    m_sectorStart->setRange(0.0, 360.0);
    m_sectorStart->setSingleStep(5.0);
    m_sectorStop = new QDoubleSpinBox(sectorBox);
    m_sectorStop->setDecimals(2);
    m_sectorStop->setRange(0.0, 360.0);
    m_sectorStop->setSingleStep(5.0);
    sectorForm->addRow(tr("Góc bắt đầu (°)"), m_sectorStart);
    sectorForm->addRow(tr("Góc kết thúc (°)"), m_sectorStop);

    sectorLay->addWidget(m_sectorOn);
    sectorLay->addLayout(sectorForm);

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
    connect(m_zfbeat, &QDoubleSpinBox::valueChanged, this, &ParamsTab::onEdited);

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

    // Rẻ quạt có hiệu lực ngay, không chờ nút "Áp dụng": nút đó thuộc về nhóm
    // thang cự ly, mà rẻ quạt lại là thứ hay phải chỉnh đi chỉnh lại lúc đang
    // theo dõi một mục tiêu.
    connect(m_sectorOn, &QCheckBox::toggled, this, [this](bool on) {
        m_sectorStart->setEnabled(on);
        m_sectorStop->setEnabled(on);
        apply();
    });
    connect(m_sectorStart, &QDoubleSpinBox::valueChanged, this, &ParamsTab::apply);
    connect(m_sectorStop, &QDoubleSpinBox::valueChanged, this, &ParamsTab::apply);

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

void ParamsTab::setParams(const AppParams &p)
{
    m_loading = true;
    m_params = p;

    m_fs->setValue(p.fs);
    m_b->setValue(p.b);
    m_tc->setValue(p.tc);
    m_zfbeat->setValue(double(p.zfbeat));
    m_autoStatus->setChecked(p.autoFromStatus);
    m_autoRange->setChecked(p.autoRange || p.autoFromStatus);
    m_autoRange->setEnabled(!p.autoFromStatus);

    m_sectorOn->setChecked(p.sectorOn);
    m_sectorStart->setValue(p.sectorStart);
    m_sectorStop->setValue(p.sectorStop);
    m_sectorStart->setEnabled(p.sectorOn);
    m_sectorStop->setEnabled(p.sectorOn);

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
    m_params.zfbeat = quint32(m_zfbeat->value());
    m_params.autoFromStatus = m_autoStatus->isChecked();
    m_params.autoRange      = m_autoRange->isChecked();

    m_params.sectorOn    = m_sectorOn->isChecked();
    m_params.sectorStart = m_sectorStart->value();
    m_params.sectorStop  = m_sectorStop->value();

    emit paramsApplied(m_params);
}
