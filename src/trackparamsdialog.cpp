#include "trackparamsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TrackParamsDialog::TrackParamsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tham số quỹ đạo"));
    setWindowFlag(Qt::Window);
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    const auto dspin = [this](double lo, double hi, int dec, double step,
                              const QString &tip) {
        auto *s = new QDoubleSpinBox(this);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setToolTip(tip);
        return s;
    };

    // --- khởi tạo và kết thúc quỹ đạo ---
    auto *lifeBox = new QGroupBox(tr("Khởi tạo và kết thúc quỹ đạo"), this);
    auto *lifeForm = new QFormLayout(lifeBox);

    m_initRule = new QComboBox(lifeBox);
    m_initRule->addItem(tr("2 vòng liên tiếp"), int(TrackInitRule::R2of2));
    m_initRule->addItem(tr("3 vòng liên tiếp"), int(TrackInitRule::R3of3));
    m_initRule->addItem(tr("2 trong 3 vòng liên tiếp"), int(TrackInitRule::R2of3));
    m_initRule->setToolTip(tr("Bao nhiêu vòng quét có điểm dấu thì một quỹ đạo "
                              "mới được coi là thật"));
    lifeForm->addRow(tr("Tiêu chuẩn khởi tạo"), m_initRule);

    m_coastScans = new QSpinBox(lifeBox);
    m_coastScans->setRange(1, 20);
    m_coastScans->setToolTip(tr("Mất điểm dấu quá ngần này vòng thì xoá quỹ đạo"));
    lifeForm->addRow(tr("Số vòng ngoại suy"), m_coastScans);

    // --- dải vận tốc quan tâm ---
    auto *velBox = new QGroupBox(tr("Dải vận tốc quan tâm"), this);
    auto *velForm = new QFormLayout(velBox);
    m_vMin = dspin(0.0, 1000.0, 1, 0.5,
                   tr("Quỹ đạo chậm hơn mức này bị xoá — thu hẹp lại để loại "
                      "bớt quỹ đạo rác từ địa vật"));
    m_vMax = dspin(0.0, 1000.0, 1, 1.0,
                   tr("Quỹ đạo nhanh hơn mức này bị xoá. Cũng là mức dùng để "
                      "mở cửa sổ liên kết ở vòng quét đầu tiên"));
    velForm->addRow(tr("Nhỏ nhất (m/s)"), m_vMin);
    velForm->addRow(tr("Lớn nhất (m/s)"), m_vMax);

    // --- cửa sổ liên kết ---
    auto *gateBox = new QGroupBox(tr("Cửa sổ liên kết điểm dấu"), this);
    auto *gateForm = new QFormLayout(gateBox);
    m_gateRange = dspin(1.0, 5000.0, 1, 5.0,
                        tr("Nửa bề rộng cửa sổ theo cự ly, cộng thêm phần do "
                           "bộ lọc chưa chắc chắn"));
    m_gateAzm   = dspin(0.1, 90.0, 2, 0.5,
                        tr("Chỉ nới thêm hình cửa sổ dự đoán vẽ trên màn hình. "
                           "Việc ghép điểm dấu xét theo khoảng cách nên ô này "
                           "không ảnh hưởng tới kết quả bám"));
    m_gateSigma = dspin(0.0, 20.0, 1, 0.5,
                        tr("Số lần độ lệch chuẩn cộng vào cửa sổ, tính trên độ "
                           "bất định **sau khi dự đoán tới vòng quét sau**. "
                           "Lớn thì bám dai nhưng dễ bắt nhầm"));
    m_gateMax   = dspin(1.0, 10000.0, 1, 50.0,
                        tr("Trần của cửa sổ cự ly, để cửa sổ không phình vô hạn "
                           "khi quỹ đạo ngoại suy nhiều vòng"));
    gateForm->addRow(tr("Cửa sổ cự ly cơ sở (m)"), m_gateRange);
    gateForm->addRow(tr("Cửa sổ phương vị cơ sở (°)"), m_gateAzm);
    gateForm->addRow(tr("Hệ số nhân độ lệch chuẩn"), m_gateSigma);
    gateForm->addRow(tr("Cửa sổ cự ly tối đa (m)"), m_gateMax);

    // --- sai số và nhiễu ---
    auto *noiseBox = new QGroupBox(tr("Sai số đo và nhiễu quá trình"), this);
    auto *noiseForm = new QFormLayout(noiseBox);
    m_sigmaRange = dspin(0.1, 1000.0, 1, 1.0,
                         tr("Độ lệch chuẩn của phép đo cự ly. Lớn thì bộ lọc "
                            "tin phép đo ít, quỹ đạo mượt nhưng chậm bám theo"));
    m_sigmaAzm   = dspin(0.01, 30.0, 2, 0.1,
                         tr("Độ lệch chuẩn của phép đo phương vị"));
    m_sigmaAccel = dspin(0.01, 100.0, 2, 0.1,
                         tr("Gia tốc ngẫu nhiên của mục tiêu. Lớn thì bộ lọc "
                            "bám nhanh khi mục tiêu ngoặt, nhưng nhiễu hơn"));
    noiseForm->addRow(tr("Sai số cự ly (m)"), m_sigmaRange);
    noiseForm->addRow(tr("Sai số phương vị (°)"), m_sigmaAzm);
    noiseForm->addRow(tr("Gia tốc mục tiêu (m/s²)"), m_sigmaAccel);

    // --- tự xoá theo thời gian ---
    auto *staleBox = new QGroupBox(tr("Tự động xoá quỹ đạo"), this);
    auto *staleForm = new QFormLayout(staleBox);
    m_staleSec = new QSpinBox(staleBox);
    m_staleSec->setRange(1, 3600);
    m_staleSec->setSingleStep(5);
    m_staleSec->setSuffix(tr(" giây"));
    m_staleSec->setToolTip(tr("Quá ngần này giây không có điểm dấu nào ghép vào "
                              "thì xoá quỹ đạo. Chặn theo thời gian nên vẫn có "
                              "tác dụng khi ăng-ten quay chậm lại hay ngừng quay"));
    staleForm->addRow(tr("Xoá khi không cập nhật quá"), m_staleSec);

    m_drawWindow = new QCheckBox(tr("Vẽ cửa sổ dự đoán"), this);

    root->addWidget(lifeBox);
    root->addWidget(velBox);
    root->addWidget(gateBox);
    root->addWidget(noiseBox);
    root->addWidget(staleBox);
    root->addWidget(m_drawWindow);

    auto *buttons = new QDialogButtonBox(this);
    auto *applyBtn = buttons->addButton(tr("Áp dụng"), QDialogButtonBox::ApplyRole);
    auto *closeBtn = buttons->addButton(tr("Đóng"), QDialogButtonBox::RejectRole);
    connect(applyBtn, &QPushButton::clicked, this, &TrackParamsDialog::apply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(buttons);

    connect(m_vMax, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { m_vMin->setMaximum(v); });
    connect(m_vMin, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { m_vMax->setMinimum(v); });

    setParams(m_params);
}

void TrackParamsDialog::setParams(const TrackParams &p)
{
    m_params = p;
    m_params.clamp();

    m_vMin->setMaximum(1000.0);
    m_vMax->setMaximum(1000.0);

    if (const int i = m_initRule->findData(int(m_params.initRule)); i >= 0)
        m_initRule->setCurrentIndex(i);
    m_coastScans->setValue(m_params.coastScans);
    m_vMin->setValue(m_params.vMin);
    m_vMax->setValue(m_params.vMax);
    m_gateRange->setValue(m_params.gateRangeM);
    m_gateAzm->setValue(m_params.gateAzmDeg);
    m_gateSigma->setValue(m_params.gateSigma);
    m_gateMax->setValue(m_params.gateMaxM);
    m_sigmaRange->setValue(m_params.sigmaRangeM);
    m_sigmaAzm->setValue(m_params.sigmaAzmDeg);
    m_sigmaAccel->setValue(m_params.sigmaAccel);
    m_staleSec->setValue(m_params.staleSec);
    m_drawWindow->setChecked(m_params.drawWindow);

    m_vMin->setMaximum(m_params.vMax);
    m_vMax->setMinimum(m_params.vMin);
}

void TrackParamsDialog::apply()
{
    m_params.initRule    = TrackInitRule(m_initRule->currentData().toInt());
    m_params.coastScans  = m_coastScans->value();
    m_params.vMin        = m_vMin->value();
    m_params.vMax        = m_vMax->value();
    m_params.gateRangeM  = m_gateRange->value();
    m_params.gateAzmDeg  = m_gateAzm->value();
    m_params.gateSigma   = m_gateSigma->value();
    m_params.gateMaxM    = m_gateMax->value();
    m_params.sigmaRangeM = m_sigmaRange->value();
    m_params.sigmaAzmDeg = m_sigmaAzm->value();
    m_params.sigmaAccel  = m_sigmaAccel->value();
    m_params.staleSec    = m_staleSec->value();
    m_params.drawWindow  = m_drawWindow->isChecked();
    m_params.clamp();

    emit applied(m_params);
}
