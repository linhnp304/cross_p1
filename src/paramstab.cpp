#include "paramstab.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
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

    rangeLay->addWidget(m_autoStatus);
    rangeLay->addWidget(m_autoRange);
    rangeLay->addWidget(m_rmax);

    m_apply = new QPushButton(tr("Áp dụng"), this);

    root->addWidget(box);
    root->addWidget(rangeBox);
    root->addWidget(m_apply, 0, Qt::AlignLeft);
    root->addStretch(1);

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

    emit paramsApplied(m_params);
}
