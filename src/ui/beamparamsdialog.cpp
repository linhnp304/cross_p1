#include "ui/beamparamsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

BeamParamsDialog::BeamParamsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tham số chùm xung"));
    // Cửa sổ riêng, không chặn cửa sổ chính.
    setWindowFlag(Qt::Window);
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    const auto spin = [this](int lo, int hi, const QString &tip) {
        auto *s = new QSpinBox(this);
        s->setRange(lo, hi);
        s->setToolTip(tip);
        return s;
    };

    // --- tiêu chuẩn độ dài chùm ---
    auto *lenBox = new QGroupBox(tr("Tiêu chuẩn độ dài chùm"), this);
    auto *lenForm = new QFormLayout(lenBox);
    m_cxMin = spin(1, 255, tr("Chùm ngắn hơn mức này bị bỏ — CX_MIN"));
    m_cxMax = spin(1, 255, tr("Chùm dài hơn mức này bị bỏ — CX_MAX"));
    lenForm->addRow(tr("Số xung ít nhất — CX_MIN"), m_cxMin);
    lenForm->addRow(tr("Số xung nhiều nhất — CX_MAX"), m_cxMax);

    // --- tiêu chuẩn dopler ---
    auto *dopBox = new QGroupBox(tr("Tiêu chuẩn Dopler"), this);
    auto *dopForm = new QFormLayout(dopBox);
    m_doplerMin = spin(0, 31, tr("Dopler quá nhỏ thường là địa vật — CX_DOPLER_MIN"));
    m_doplerMax = spin(0, 31, tr("Dopler quá lớn thường là địa vật — CX_DOPLER_MAX"));
    dopForm->addRow(tr("Nhỏ nhất — CX_DOPLER_MIN"), m_doplerMin);
    dopForm->addRow(tr("Lớn nhất — CX_DOPLER_MAX"), m_doplerMax);

    // --- tiêu chuẩn xét duyệt plot vào chùm ---
    auto *gateBox = new QGroupBox(tr("Tiêu chuẩn xét duyệt"), this);
    auto *gateForm = new QFormLayout(gateBox);
    m_deltaRange  = spin(0, 64, tr("Lệch ô cự ly tối đa so với xung đầu chùm — "
                                   "CX_DELTA_RANGE"));
    m_deltaDopler = spin(0, 31, tr("Lệch dopler tối đa so với xung đầu chùm — "
                                   "CX_DELTA_DOPLER"));
    gateForm->addRow(tr("Lệch ô cự ly — CX_DELTA_RANGE"), m_deltaRange);
    gateForm->addRow(tr("Lệch dopler — CX_DELTA_DOPLER"), m_deltaDopler);

    // --- tiêu chuẩn khởi tạo / kết thúc chùm ---
    auto *lifeBox = new QGroupBox(tr("Tiêu chuẩn khởi tạo / kết thúc"), this);
    auto *lifeForm = new QFormLayout(lifeBox);
    m_numWait = spin(1, 64, tr("Số chu kỳ liên tiếp có plot thì mở chùm — "
                               "CX_NUM_WAIT"));
    m_numLose = spin(1, 64, tr("Số chu kỳ liên tiếp mất plot thì đóng chùm — "
                               "CX_NUM_LOSE"));
    lifeForm->addRow(tr("Số chu kỳ mở chùm — CX_NUM_WAIT"), m_numWait);
    lifeForm->addRow(tr("Số chu kỳ đóng chùm — CX_NUM_LOSE"), m_numLose);

    // --- phương án tính tâm và hiển thị ---
    auto *miscBox = new QGroupBox(tr("Tính tâm chùm và hiển thị"), this);
    auto *miscForm = new QFormLayout(miscBox);
    m_useAmp = new QCheckBox(tr("Sử dụng trọng số biên độ phản xạ"), miscBox);
    m_useAmp->setToolTip(tr("CX_USE_AMPLITUDE — tắt thì tâm chùm lấy trung bình "
                            "đơn giản, bật thì xung phản xạ mạnh kéo tâm về "
                            "phía nó (khi đó mới có năng lượng phản xạ)"));
    m_showSec = spin(1, 120, tr("Điểm dấu bị xoá khỏi màn hình sau ngần này giây"));
    miscForm->addRow(m_useAmp);
    miscForm->addRow(tr("Số giây hiển thị điểm dấu"), m_showSec);

    // --- hiệu chỉnh tâm chùm ---
    // Cộng vào sau khi thuật toán đã tính xong, để bù sai lệch lắp đặt chứ
    // không phải để sửa thuật toán — vì vậy để thành group riêng.
    auto *fixBox = new QGroupBox(tr("Hiệu chỉnh tâm chùm"), this);
    auto *fixForm = new QFormLayout(fixBox);

    m_azmOffset = new QDoubleSpinBox(fixBox);
    m_azmOffset->setDecimals(2);
    m_azmOffset->setRange(-360.0, 360.0);
    m_azmOffset->setSingleStep(0.1);
    m_azmOffset->setToolTip(tr("Cộng vào phương vị của điểm dấu sau khi đã tính "
                               "tâm chùm. Kết quả quay vòng về 0..360 độ."));

    m_rangeOffset = new QDoubleSpinBox(fixBox);
    m_rangeOffset->setDecimals(1);
    m_rangeOffset->setRange(-1000000.0, 1000000.0);
    m_rangeOffset->setSingleStep(1.0);
    m_rangeOffset->setToolTip(tr("Cộng vào cự ly của điểm dấu sau khi đã tính "
                                 "tâm chùm. Kết quả âm thì gán bằng 0."));

    fixForm->addRow(tr("Bù phương vị (độ)"), m_azmOffset);
    fixForm->addRow(tr("Bù cự ly (mét)"), m_rangeOffset);

    root->addWidget(lenBox);
    root->addWidget(dopBox);
    root->addWidget(gateBox);
    root->addWidget(lifeBox);
    root->addWidget(fixBox);
    root->addWidget(miscBox);

    auto *buttons = new QDialogButtonBox(this);
    auto *applyBtn = buttons->addButton(tr("Áp dụng"), QDialogButtonBox::ApplyRole);
    auto *closeBtn = buttons->addButton(tr("Đóng"), QDialogButtonBox::RejectRole);
    connect(applyBtn, &QPushButton::clicked, this, &BeamParamsDialog::apply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(buttons);

    // Trần trên của "số xung ít nhất" đi theo "số xung nhiều nhất": để nhập
    // được min > max thì thuật toán không bao giờ sinh ra điểm dấu nào, mà
    // nhìn giao diện lại không thấy có gì sai.
    connect(m_cxMax, &QSpinBox::valueChanged, this,
            [this](int v) { m_cxMin->setMaximum(v); });
    connect(m_cxMin, &QSpinBox::valueChanged, this,
            [this](int v) { m_cxMax->setMinimum(v); });
    connect(m_doplerMax, &QSpinBox::valueChanged, this,
            [this](int v) { m_doplerMin->setMaximum(v); });
    connect(m_doplerMin, &QSpinBox::valueChanged, this,
            [this](int v) { m_doplerMax->setMinimum(v); });

    setParams(m_params);
}

void BeamParamsDialog::setParams(const BeamParams &p)
{
    m_params = p;
    m_params.clamp();

    // Đặt trần/sàn trước rồi mới đặt giá trị, nếu không giá trị vừa nạp có thể
    // bị chính ràng buộc của lần trước cắt mất.
    m_cxMax->setMaximum(255);
    m_cxMin->setMaximum(255);
    m_doplerMax->setMaximum(31);
    m_doplerMin->setMaximum(31);

    m_cxMin->setValue(m_params.cxMin);
    m_cxMax->setValue(m_params.cxMax);
    m_doplerMin->setValue(m_params.doplerMin);
    m_doplerMax->setValue(m_params.doplerMax);
    m_deltaRange->setValue(m_params.deltaRange);
    m_deltaDopler->setValue(m_params.deltaDopler);
    m_numWait->setValue(m_params.numWait);
    m_numLose->setValue(m_params.numLose);
    m_useAmp->setChecked(m_params.useAmplitude);
    m_showSec->setValue(m_params.showSec);
    m_azmOffset->setValue(m_params.azmOffsetDeg);
    m_rangeOffset->setValue(m_params.rangeOffsetM);

    m_cxMin->setMaximum(m_params.cxMax);
    m_cxMax->setMinimum(m_params.cxMin);
    m_doplerMin->setMaximum(m_params.doplerMax);
    m_doplerMax->setMinimum(m_params.doplerMin);
}

void BeamParamsDialog::apply()
{
    m_params.cxMin        = m_cxMin->value();
    m_params.cxMax        = m_cxMax->value();
    m_params.doplerMin    = m_doplerMin->value();
    m_params.doplerMax    = m_doplerMax->value();
    m_params.deltaRange   = m_deltaRange->value();
    m_params.deltaDopler  = m_deltaDopler->value();
    m_params.numWait      = m_numWait->value();
    m_params.numLose      = m_numLose->value();
    m_params.useAmplitude = m_useAmp->isChecked();
    m_params.showSec      = m_showSec->value();
    m_params.azmOffsetDeg = m_azmOffset->value();
    m_params.rangeOffsetM = m_rangeOffset->value();
    m_params.clamp();

    emit applied(m_params);
}
