#pragma once

#include <QString>

namespace theme {

/// Bảng màu nền tối, phong cách kỹ thuật. Dùng chung cho toàn bộ giao diện.
inline QString styleSheet()
{
    return QStringLiteral(R"(
QWidget {
    background: #0d1117;
    color: #c3ccd6;
    font-family: "DejaVu Sans", "Segoe UI", "Noto Sans", sans-serif;
    font-size: 12px;
}

QGroupBox {
    border: 1px solid #23303d;
    border-radius: 3px;
    margin-top: 14px;
    padding: 10px 8px 8px 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 8px;
    padding: 0 4px;
    color: #7fa8c9;
    font-weight: bold;
    text-transform: uppercase;
}

QTabWidget::pane {
    border: 1px solid #23303d;
    background: #0f151c;
}
QTabBar::tab {
    background: #131b24;
    color: #8b98a5;
    border: 1px solid #23303d;
    border-bottom: none;
    padding: 6px 16px;
    margin-right: 2px;
}
QTabBar::tab:selected {
    background: #1b2733;
    color: #d7e3ef;
    border-bottom: 2px solid #4e9ad4;
}
QTabBar::tab:hover:!selected { background: #172029; }

QLineEdit, QDoubleSpinBox, QSpinBox {
    background: #070b0f;
    border: 1px solid #2a3a49;
    border-radius: 2px;
    padding: 4px 6px;
    color: #d7e3ef;
    selection-background-color: #2f6d9e;
}
QDoubleSpinBox:focus, QSpinBox:focus, QLineEdit:focus { border-color: #4e9ad4; }

/* Ô đang bị khoá phải nhìn ra là đang bị khoá. Bảng màu đặt màu chữ cho từng
   loại widget, mà màu đó đè lên cả bảng màu mặc định của Qt cho trạng thái
   disabled — không có mấy dòng dưới đây thì setEnabled(false) chẳng đổi gì trên
   màn hình, người dùng cứ bấm vào một nhóm ô đã chết mà không hiểu vì sao. */
QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled,
QComboBox:disabled { color: #59646f; }
QLineEdit:disabled, QDoubleSpinBox:disabled, QSpinBox:disabled {
    background: #0a0e12; color: #59646f; border-color: #1d2731;
}
QPushButton:disabled {
    background: #141c24; color: #59646f; border-color: #22303d;
}
QGroupBox:disabled::title { color: #4a5f73; }

QPushButton {
    background: #1b2733;
    border: 1px solid #2f4358;
    border-radius: 2px;
    padding: 5px 18px;
    color: #d7e3ef;
}
QPushButton:hover  { background: #23323f; border-color: #4e9ad4; }
QPushButton:pressed { background: #14202b; }

QCheckBox, QRadioButton { spacing: 6px; padding: 2px 0; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox::indicator {
    border: 1px solid #3a4d61; background: #070b0f; border-radius: 2px;
}
QCheckBox::indicator:checked { background: #4e9ad4; border-color: #4e9ad4; }
QRadioButton::indicator {
    border: 1px solid #3a4d61; background: #070b0f; border-radius: 7px;
}
QRadioButton::indicator:checked { background: #4e9ad4; border-color: #4e9ad4; }

QSlider::groove:horizontal {
    height: 3px; background: #23303d; border-radius: 1px;
}
QSlider::handle:horizontal {
    background: #4e9ad4; width: 12px; margin: -5px 0; border-radius: 6px;
}
QSlider::groove:vertical {
    width: 3px; background: #23303d; border-radius: 1px;
}
QSlider::handle:vertical {
    background: #4e9ad4; height: 12px; margin: 0 -5px; border-radius: 6px;
}

QSplitter::handle { background: #1a232c; }
QSplitter::handle:hover { background: #2f6d9e; }

/* Chú giải nhỏ hơn chữ thường một nấc: nó là chú thích, không phải nội dung
   chính, mà mấy chú giải trong phần mềm này lại dài vài dòng — để cùng cỡ chữ
   thì cái hộp vàng che mất đúng chỗ người dùng đang định bấm. */
QToolTip {
    background: #131b24;
    color: #c3ccd6;
    border: 1px solid #2f4358;
    padding: 3px 5px;
    font-size: 11px;
}
)");
}

} // namespace theme
