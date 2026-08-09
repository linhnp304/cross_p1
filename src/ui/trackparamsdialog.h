#pragma once

#include "proc/procparams.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

/// Cửa sổ "Tham số quỹ đạo" — bộ lọc Kalman và quản lý danh sách quỹ đạo.
class TrackParamsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrackParamsDialog(QWidget *parent = nullptr);

    void setParams(const TrackParams &p);

signals:
    void applied(const TrackParams &p);

private:
    void apply();

    TrackParams m_params;

    QComboBox      *m_initRule    = nullptr;
    QSpinBox       *m_coastScans  = nullptr;
    QDoubleSpinBox *m_vMin        = nullptr;
    QDoubleSpinBox *m_vMax        = nullptr;
    QDoubleSpinBox *m_gateRange   = nullptr;
    QDoubleSpinBox *m_gateAzm     = nullptr;
    QDoubleSpinBox *m_gateSigma   = nullptr;
    QDoubleSpinBox *m_gateMax     = nullptr;
    QDoubleSpinBox *m_sigmaRange  = nullptr;
    QDoubleSpinBox *m_sigmaAzm    = nullptr;
    QDoubleSpinBox *m_sigmaAccel  = nullptr;
    QSpinBox       *m_staleSec    = nullptr;
    QCheckBox      *m_drawWindow  = nullptr;
};
