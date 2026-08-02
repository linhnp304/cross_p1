#include "recordtab.h"

#include "appinfo.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

/// Các mức tốc độ tái hiện, theo đúng thứ tự hiện trong ComboBox.
struct SpeedItem { const char *label; double factor; };
constexpr SpeedItem kSpeeds[] = {
    {"1/4x", 0.25}, {"1/2x", 0.5}, {"1x", 1.0},
    {"2x", 2.0}, {"4x", 4.0}, {"8x", 8.0},
};
constexpr int kDefaultSpeedIndex = 2;   // 1x

QString hhmmss(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString stamp(qint64 sec)
{
    return QDateTime::fromSecsSinceEpoch(sec)
        .toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
}

/// Số có dấu phân nhóm hàng nghìn — bản ghi đếm tới hàng triệu, đọc dãy số trần
/// thì không ai đếm nổi mấy chữ số.
QString num(quint64 v)
{
    return QLocale().toString(qulonglong(v));
}

QString bytes(quint64 v)
{
    return QLocale().formattedDataSize(qint64(v), 2, QLocale::DataSizeTraditionalFormat);
}

QLabel *makeInfoLabel(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setStyleSheet(QStringLiteral("color: #7fa8c9;"));
    l->setWordWrap(true);
    return l;
}

} // namespace

RecordTab::RecordTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // --- Group Ghi lưu ----------------------------------------------------
    auto *recBox = new QGroupBox(tr("Ghi lưu"), this);
    auto *recLay = new QVBoxLayout(recBox);

    m_rawBox = new QCheckBox(tr("Ghi dữ liệu gốc"), recBox);
    m_rawBox->setToolTip(tr("Nguyên các datagram RAW_V và RAW_P nhận được từ "
                            "đài. Phát lại loại này thì toàn bộ đường xử lý "
                            "chạy lại từ đầu, đúng như lúc thật.\n"
                            "Chú ý: ~1.7 MB mỗi giây, file đầy 2 GB sau khoảng "
                            "20 phút là tự ngắt sang file mới."));
    m_rawBox->setChecked(false);

    m_procBox = new QCheckBox(tr("Ghi dữ liệu đã xử lý"), recBox);
    m_procBox->setToolTip(tr("Góc đường quét kèm nền tạp Video[1024] đã quy về "
                             "0..255, điểm dấu PlotTC, quỹ đạo Track, và các "
                             "gói dữ liệu khác (trạng thái hệ thống, trạng thái "
                             "lệnh điều khiển)."));
    m_procBox->setChecked(true);

    m_recBtn   = new QPushButton(tr("Ghi lưu"), recBox);
    m_recTime  = makeInfoLabel(recBox);
    m_recCount = makeInfoLabel(recBox);

    auto *recBtnRow = new QHBoxLayout;
    recBtnRow->addWidget(m_recBtn);
    recBtnRow->addStretch(1);

    recLay->addWidget(m_rawBox);
    recLay->addWidget(m_procBox);
    recLay->addLayout(recBtnRow);
    recLay->addWidget(m_recTime);
    recLay->addWidget(m_recCount);

    // --- Group Phát lại ---------------------------------------------------
    auto *playBox = new QGroupBox(tr("Phát lại (tái hiện) dữ liệu"), this);
    auto *playLay = new QVBoxLayout(playBox);

    // Hai câu hỏi độc lập nhau, nên phải là **hai** nhóm loại trừ. Cùng một
    // widget cha thì Qt gộp mọi radio thành một nhóm duy nhất — chọn "Dữ liệu
    // gốc" sẽ bỏ chọn luôn "Danh sách đang quản lý". QButtonGroup là chỗ nói rõ
    // ranh giới đó.
    m_typeRaw  = new QRadioButton(tr("Dữ liệu gốc"), playBox);
    m_typeProc = new QRadioButton(tr("Dữ liệu đã qua xử lý"), playBox);
    auto *typeGroup = new QButtonGroup(this);
    typeGroup->addButton(m_typeRaw);
    typeGroup->addButton(m_typeProc);
    m_typeRaw->setChecked(true);

    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel(tr("Loại dữ liệu:"), playBox));
    typeRow->addWidget(m_typeRaw);
    typeRow->addWidget(m_typeProc);
    typeRow->addStretch(1);

    m_srcList = new QRadioButton(tr("Danh sách đang quản lý"), playBox);
    m_srcFile = new QRadioButton(tr("Mở tệp ghi lưu khác"), playBox);
    auto *srcGroup = new QButtonGroup(this);
    srcGroup->addButton(m_srcList);
    srcGroup->addButton(m_srcFile);
    m_srcFile->setToolTip(tr("File ghi lưu chép từ máy khác sang. Phân loại đọc "
                             "từ chính header của file, nên đổi tên file cũng "
                             "không nhận nhầm loại."));
    m_srcList->setChecked(true);
    auto *srcRow = new QHBoxLayout;
    srcRow->addWidget(new QLabel(tr("Nguồn ghi lưu:"), playBox));
    srcRow->addWidget(m_srcList);
    srcRow->addWidget(m_srcFile);
    srcRow->addStretch(1);

    m_sessionBox = new QComboBox(playBox);
    m_sessionBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sessionBox->setMinimumContentsLength(20);
    m_openBtn = new QPushButton(tr("Mở tệp ghi lưu"), playBox);
    auto *listRow = new QHBoxLayout;
    listRow->addWidget(m_sessionBox, 1);
    listRow->addWidget(m_openBtn, 0);

    m_info = makeInfoLabel(playBox);

    m_playBtn    = new QPushButton(tr("Bắt đầu phát lại"), playBox);
    m_speedLabel = new QLabel(tr("Tốc độ tái hiện:"), playBox);
    m_speedBox   = new QComboBox(playBox);
    for (const SpeedItem &s : kSpeeds)
        m_speedBox->addItem(QString::fromLatin1(s.label), s.factor);
    m_speedBox->setCurrentIndex(kDefaultSpeedIndex);
    m_speedBox->setToolTip(tr("Chỉ áp dụng với dữ liệu đã xử lý. Dữ liệu gốc "
                              "luôn phát lại ở tốc độ 1x."));

    auto *playBtnRow = new QHBoxLayout;
    playBtnRow->addWidget(m_playBtn);
    playBtnRow->addStretch(1);
    playBtnRow->addWidget(m_speedLabel);
    playBtnRow->addWidget(m_speedBox);

    m_playTime  = makeInfoLabel(playBox);
    m_playCount = makeInfoLabel(playBox);

    playLay->addLayout(typeRow);
    playLay->addLayout(srcRow);
    playLay->addLayout(listRow);
    playLay->addWidget(m_info);
    playLay->addLayout(playBtnRow);
    playLay->addWidget(m_playTime);
    playLay->addWidget(m_playCount);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #7fa8c9;"));

    root->addWidget(recBox);
    root->addWidget(playBox);
    root->addWidget(m_status);
    root->addStretch(1);

    // --- Nối tín hiệu -----------------------------------------------------
    connect(m_recBtn, &QPushButton::clicked, this, [this] {
        if (m_recording)
            emit recordStopRequested();
        else
            emit recordStartRequested(m_rawBox->isChecked(), m_procBox->isChecked());
    });

    const auto onTypeChanged = [this] {
        // Tệp mở ngoài thuộc về đúng một loại; đổi loại là nó không còn liên
        // quan nữa, giữ lại chỉ gây hiểu nhầm.
        m_hasExternal = false;
        refreshSessionList();
        refreshInfo();
        refreshEnabled();
    };
    connect(m_typeRaw, &QRadioButton::toggled, this, onTypeChanged);

    connect(m_srcList, &QRadioButton::toggled, this, [this] {
        refreshInfo();
        refreshEnabled();
    });
    connect(m_openBtn, &QPushButton::clicked, this, &RecordTab::openExternalFile);
    connect(m_sessionBox, &QComboBox::currentIndexChanged, this, [this] {
        refreshInfo();
        refreshEnabled();
    });

    connect(m_playBtn, &QPushButton::clicked, this, [this] {
        if (m_replaying) {
            emit replayStopRequested();
            return;
        }
        RecSession s;
        if (!currentSession(s)) {
            setStatusText(tr("Chưa chọn phiên nào để phát lại"), true);
            return;
        }
        const bool raw = s.kind == rec::kKindRaw;
        // Dữ liệu gốc luôn 1x — ComboBox tốc độ lúc này đang ẩn.
        const double speed = raw ? 1.0 : m_speedBox->currentData().toDouble();
        emit replayStartRequested(s.path, raw, s.total, speed);
    });

    connect(m_speedBox, &QComboBox::currentIndexChanged, this, [this] {
        emit replaySpeedChanged(m_speedBox->currentData().toDouble());
    });

    refreshSessionList();
    refreshInfo();
    refreshEnabled();
}

bool RecordTab::wantsRaw() const        { return m_rawBox->isChecked(); }
bool RecordTab::wantsProcessed() const  { return m_procBox->isChecked(); }
bool RecordTab::replayRawSelected() const { return m_typeRaw->isChecked(); }

void RecordTab::setSessions(const QVector<RecSession> &all)
{
    m_sessions = all;
    refreshSessionList();
    refreshInfo();
    refreshEnabled();
}

void RecordTab::setStatusText(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? QStringLiteral("color: #d47b6a;")
                                    : QStringLiteral("color: #7fa8c9;"));
}

// ----------------------------------------------------------- ghi lưu -------

void RecordTab::setRecording(bool on)
{
    m_recording = on;
    m_recBtn->setText(on ? tr("Dừng ghi lưu") : tr("Ghi lưu"));
    if (!on) {
        m_recTime->clear();
        m_recCount->clear();
    }
    refreshEnabled();
}

void RecordTab::setRecordStats(const RecorderStats &s)
{
    if (!s.running)
        return;

    m_recTime->setText(tr("Thời gian ghi: %1").arg(hhmmss(s.elapsedMs)));

    QStringList lines;
    lines << tr("Tổng số bản ghi: %1").arg(num(s.total()));
    if (s.raw)
        lines << tr("    %1 Dữ liệu gốc (%2)").arg(num(s.rawRecords), bytes(s.rawBytes));
    if (s.proc)
        lines << tr("    %1 Dữ liệu đã xử lý (%2)")
                     .arg(num(s.procRecords), bytes(s.procBytes));
    if (s.dropped > 0) {
        lines << tr("    bỏ mất %1 bản ghi vì đĩa không theo kịp")
                     .arg(num(s.dropped));
    }
    m_recCount->setText(lines.join(QLatin1Char('\n')));
}

// ---------------------------------------------------------- phát lại -------

void RecordTab::setReplaying(bool on)
{
    m_replaying = on;
    m_playBtn->setText(on ? tr("Dừng phát lại") : tr("Bắt đầu phát lại"));
    if (!on) {
        m_playTime->clear();
        m_playCount->clear();
    }
    refreshEnabled();
}

void RecordTab::setReplayStats(const PlayerStats &s)
{
    if (!s.running)
        return;

    m_playTime->setText(tr("Thời gian tái hiện: %1").arg(stamp(s.virtualMs / 1000)));
    m_playCount->setText(s.total > 0
        ? tr("Số bản ghi: %1 / %2").arg(num(s.played), num(s.total))
        : tr("Số bản ghi: %1").arg(num(s.played)));
}

// ------------------------------------------------------- danh sách phiên ---

void RecordTab::refreshSessionList()
{
    const quint32 want = m_typeRaw->isChecked() ? rec::kKindRaw : rec::kKindProc;

    const QString keep = m_sessionBox->currentData().toString();
    QSignalBlocker block(m_sessionBox);
    m_sessionBox->clear();

    for (const RecSession &s : m_sessions) {
        if (s.kind != want)
            continue;
        m_sessionBox->addItem(s.label(), s.path);
    }

    // Giữ nguyên phiên đang chọn khi danh sách được dựng lại (vừa ghi xong một
    // file là cả danh sách đổ lại) — nếu không thì mỗi lần ngắt file là ô chọn
    // nhảy về đầu danh sách.
    const int at = m_sessionBox->findData(keep);
    if (at >= 0)
        m_sessionBox->setCurrentIndex(at);
}

bool RecordTab::currentSession(RecSession &out) const
{
    if (m_srcFile->isChecked()) {
        if (!m_hasExternal)
            return false;
        out = m_external;
        return true;
    }

    const QString path = m_sessionBox->currentData().toString();
    if (path.isEmpty())
        return false;
    for (const RecSession &s : m_sessions) {
        if (s.path == path) {
            out = s;
            return true;
        }
    }
    return false;
}

void RecordTab::refreshInfo()
{
    RecSession s;
    if (!currentSession(s)) {
        m_info->setText(m_srcFile->isChecked()
            ? tr("Chưa mở tệp ghi lưu nào")
            : tr("Danh sách trống — chưa có phiên nào thuộc loại này"));
        return;
    }

    QStringList lines;
    lines << tr("Bắt đầu ghi:  %1").arg(stamp(s.startSec));
    lines << tr("Kết thúc ghi: %1").arg(stamp(s.endSec));
    lines << tr("Tổng số bản ghi: %1  (%2)").arg(num(s.total), bytes(quint64(s.bytes)));

    if (s.kind == rec::kKindRaw) {
        lines << tr("    RAW_V: %1").arg(num(s.rawV));
        lines << tr("    RAW_P: %1").arg(num(s.rawP));
    } else {
        lines << tr("    Nền tạp: %1").arg(num(s.video));
        lines << tr("    Plot: %1").arg(num(s.plot));
        lines << tr("    Track: %1").arg(num(s.track));
        lines << tr("    Khác: %1").arg(num(s.other));
    }
    if (m_srcFile->isChecked())
        lines << s.path;

    m_info->setText(lines.join(QLatin1Char('\n')));
}

void RecordTab::openExternalFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Mở tệp ghi lưu"), RecordIndex::rootDir(),
        tr("File ghi lưu (*.rec);;Tất cả các tệp (*)"));
    if (path.isEmpty())
        return;

    RecSession s;
    QString error;
    if (!RecordIndex::probe(path, s, error)) {
        m_hasExternal = false;
        refreshInfo();
        refreshEnabled();
        setStatusText(error, true);
        return;
    }

    m_external    = s;
    m_hasExternal = true;

    // Phân loại lấy từ header chứ không từ ô người dùng đang chọn — đó chính là
    // lý do phân loại nằm trong file. Lệch thì tự chuyển ô cho khớp và nói ra.
    const bool fileIsRaw = s.kind == rec::kKindRaw;
    if (fileIsRaw != m_typeRaw->isChecked()) {
        QSignalBlocker b1(m_typeRaw), b2(m_typeProc);
        m_typeRaw->setChecked(fileIsRaw);
        m_typeProc->setChecked(!fileIsRaw);
        setStatusText(tr("Tệp vừa mở là %1 — đã chuyển ô \"Loại dữ liệu\" cho khớp")
                          .arg(fileIsRaw ? tr("dữ liệu gốc")
                                         : tr("dữ liệu đã qua xử lý")));
        refreshSessionList();
    } else {
        setStatusText(QString());
    }

    refreshInfo();
    refreshEnabled();
}

void RecordTab::refreshEnabled()
{
    // Đang ghi thì không đổi loại dữ liệu ghi giữa chừng: hai ô đó quyết định
    // file nào đang mở, đổi giữa chừng chỉ sinh ra file dở dang.
    m_rawBox->setEnabled(!m_recording);
    m_procBox->setEnabled(!m_recording);

    const bool raw = m_typeRaw->isChecked();
    m_typeRaw->setEnabled(!m_replaying);
    m_typeProc->setEnabled(!m_replaying);
    m_srcList->setEnabled(!m_replaying);
    m_srcFile->setEnabled(!m_replaying);
    m_sessionBox->setEnabled(!m_replaying && m_srcList->isChecked());
    m_openBtn->setEnabled(!m_replaying && m_srcFile->isChecked());

    // Dữ liệu gốc mặc định phát lại ở tốc độ 1x, nên cả hàng đó ẩn đi.
    m_speedLabel->setVisible(!raw);
    m_speedBox->setVisible(!raw);

    RecSession s;
    m_playBtn->setEnabled(m_replaying || currentSession(s));
}
