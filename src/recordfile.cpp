#include "recordfile.h"

#include <cstring>

namespace rec {

void FileHeader::count(RecType t)
{
    ++total;
    switch (t) {
    case RecType::RawV:  ++rawV;  break;
    case RecType::RawP:  ++rawP;  break;
    case RecType::Video: ++video; break;
    case RecType::Plot:  ++plot;  break;
    case RecType::Track: ++track; break;
    case RecType::Other: ++other; break;
    }
}

QByteArray packHeader(const FileHeader &h)
{
    QByteArray out(kHeaderBytes, '\0');
    char *p = out.data();

    // Thứ tự đúng theo bảng mô tả giai đoạn; hai ô cuối lấy từ phần dự phòng.
    const quint32 words[] = {
        kMagic, h.kind, h.startSec, h.endSec, h.total,
        h.rawV, h.rawP, h.plot, h.track, h.other,
        h.video, h.version,
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        putU32(p + i * 4, words[i]);
    return out;
}

bool parseHeader(const char *data, int size, FileHeader &out)
{
    if (size < kHeaderBytes || getU32(data) != kMagic)
        return false;

    FileHeader h;
    h.kind = getU32(data + 4);
    if (!h.isRaw() && !h.isProc())
        return false;

    h.startSec = getU32(data + 8);
    h.endSec   = getU32(data + 12);
    h.total    = getU32(data + 16);
    h.rawV     = getU32(data + 20);
    h.rawP     = getU32(data + 24);
    h.plot     = getU32(data + 28);
    h.track    = getU32(data + 32);
    h.other    = getU32(data + 36);
    h.video    = getU32(data + 40);
    h.version  = getU32(data + 44);

    out = h;
    return true;
}

// ------------------------------------------------------------- hàng đợi -----

void RecordSpool::setWants(bool raw, bool proc)
{
    m_wantRaw.store(raw, std::memory_order_relaxed);
    m_wantProc.store(proc, std::memory_order_relaxed);
}

void RecordSpool::push(RecType type, qint64 timeMs, const QByteArray &payload)
{
    QMutexLocker lock(&m_mutex);
    m_queue.push_back({type, timeMs, payload});
    m_bytes += payload.size() + kRecHeadBytes;

    while (m_bytes > m_capacityBytes && !m_queue.empty()) {
        m_bytes -= m_queue.front().payload.size() + kRecHeadBytes;
        m_queue.pop_front();
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordSpool::drain(std::deque<RecItem> &out)
{
    QMutexLocker lock(&m_mutex);
    // Hoán đổi rồi mới xoá: cả lô đi ra trong một thao tác, và bộ nhớ mà lô
    // trước đã cấp phát quay lại làm chỗ cho lô sau.
    out.swap(m_queue);
    m_queue.clear();
    m_bytes = 0;
}

void RecordSpool::clear()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
    m_bytes = 0;
    // Bộ đếm bỏ mất cũng về 0: nó nói về phiên ghi đang chạy, cộng dồn từ phiên
    // trước thì con số hiện trên giao diện không còn nghĩa gì.
    m_dropped.store(0, std::memory_order_relaxed);
}

// ------------------------------------------------------------ bản ghi Video -

QByteArray packVideo(quint32 azimuth, const quint8 *video)
{
    QByteArray out(kVideoBytes, Qt::Uninitialized);
    putU32(out.data(), azimuth);
    std::memcpy(out.data() + 4, video, kVideoBins);
    return out;
}

bool unpackVideo(const QByteArray &payload, quint32 &azimuth, quint8 *video)
{
    if (payload.size() < kVideoBytes)
        return false;
    azimuth = getU32(payload.constData());
    std::memcpy(video, payload.constData() + 4, kVideoBins);
    return true;
}

} // namespace rec
