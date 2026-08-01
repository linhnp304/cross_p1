#pragma once

#include "procparams.h"

#include <QDialog>

class QCheckBox;
class QSpinBox;

/// Cửa sổ "Tham số chùm xung" — hiệu chỉnh thuật toán tính tâm chùm khi đang chạy.
///
/// Không chặn cửa sổ chính (không modal): trắc thủ vừa sửa tham số vừa nhìn
/// điểm dấu trên panel 1 đổi theo, đó mới là cách chỉnh cho đúng.
class BeamParamsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BeamParamsDialog(QWidget *parent = nullptr);

    /// Nạp giá trị đang dùng vào các ô nhập.
    void setParams(const BeamParams &p);

signals:
    /// Người dùng bấm "Áp dụng".
    void applied(const BeamParams &p);

private:
    void apply();

    BeamParams m_params;

    QSpinBox  *m_cxMin       = nullptr;
    QSpinBox  *m_cxMax       = nullptr;
    QSpinBox  *m_doplerMin   = nullptr;
    QSpinBox  *m_doplerMax   = nullptr;
    QSpinBox  *m_deltaRange  = nullptr;
    QSpinBox  *m_deltaDopler = nullptr;
    QSpinBox  *m_numWait     = nullptr;
    QSpinBox  *m_numLose     = nullptr;
    QCheckBox *m_useAmp      = nullptr;
    QSpinBox  *m_showSec     = nullptr;
};
