// Công cụ điều khiển giao diện bằng kịch bản — để kiểm thử được phần nhìn thấy
// mà không cần ai ngồi bấm chuột.
//
// Máy phát triển chạy Wayland và không có xdotool/wmctrl/công cụ chụp màn hình
// nào, nên không thể điều khiển cửa sổ từ bên ngoài. Cách này đi vòng qua toàn
// bộ chuyện đó: dựng thẳng MainWindow trong tiến trình của mình, gửi sự kiện
// chuột/bàn phím bằng QTest, rồi chụp lại bằng QWidget::grab(). Không đụng tới
// màn hình thật của người dùng, không cần quyền quản trị, không cài thêm gì —
// và chạy được cả trên máy dựng tự động không có màn hình.
//
// Mặc định chạy trên nền tảng "offscreen". Muốn xem tận mắt thì đặt biến môi
// trường QT_QPA_PLATFORM=wayland (hoặc xcb) trước khi chạy.
//
// Cách dùng:  ./ar0101-guidrv kich-ban.txt
// Lệnh của kịch bản: xem hàm help() bên dưới, hoặc chạy không tham số.

#include "app/appinfo.h"
#include "app/mainwindow.h"
#include "ui/theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTextStream>

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

void help()
{
    out() << R"(Lệnh của kịch bản (mỗi dòng một lệnh, # là ghi chú):

  wait <ms>                 chờ, vẫn chạy vòng lặp sự kiện
  resize <w> <h>            đổi cỡ cửa sổ chính
  dump [<đích>|all]         liệt kê widget đang thấy được, kèm số thứ tự;
                            "all" thì kể cả widget đang khuất ở tab khác
  windows                   liệt kê các cửa sổ đang mở
  shot <file.png> [<đích>]  chụp cửa sổ chính, hoặc chụp riêng một widget
  click <đích>              bấm chuột trái vào giữa widget
  mouse <đích> <x> <y>      bấm chuột trái vào toạ độ trong widget
  rclick <đích> <x> <y>     bấm chuột phải
  wheel <đích> <x> <y> <n>  lăn chuột n nấc (âm là lăn xuống)
  tab <nhãn>                chọn tab theo nhãn
  check <đích> on|off       tích / bỏ tích, chỉ bấm khi trạng thái đang khác
  set <đích> <giá trị>      đặt giá trị cho ô nhập, ô số, hộp chọn, thanh trượt
  key <đích> <phím>         gửi tổ hợp phím, ví dụ Ctrl+S hay Return
  type <đích> <chuỗi>       gõ chuỗi vào widget
  text <đích>               in ra chữ hiện có của widget
  table <đích>              in ra nội dung một bảng
  quit                      thoát sớm

Cách chỉ đích (đều ưu tiên widget đang nhìn thấy được):
  #tênĐốiTượng   theo objectName
  @12            theo số thứ tự mà lệnh dump in ra
  .RadarView     theo tên lớp
  "Thêm dòng"    theo chữ hiện trên nút / nhãn, hoặc tiêu đề cửa sổ con

Chỉ vào nhãn của một ô nhập cũng được: lệnh set / type / check / key tự lần
sang ô nhập đứng cạnh nhãn đó.
)";
    out().flush();
}

// --------------------------------------------------------------- tìm widget --

/// Mọi widget của mọi cửa sổ, theo thứ tự dựng — nên số thứ tự mà dump() in ra
/// giữ nguyên giữa các lần chạy, miễn là giao diện không đổi.
QList<QWidget *> allWidgets()
{
    QList<QWidget *> list;
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        list << w;
        list << w->findChildren<QWidget *>();
    }
    return list;
}

/// Chữ hiện trên một widget, bỏ dấu & của phím tắt.
QString widgetText(const QWidget *w)
{
    QString t;
    if (const auto *b = qobject_cast<const QAbstractButton *>(w))
        t = b->text();
    else if (const auto *l = qobject_cast<const QLabel *>(w))
        t = l->text();
    else if (const auto *e = qobject_cast<const QLineEdit *>(w))
        t = e->text();
    else if (const auto *c = qobject_cast<const QComboBox *>(w))
        t = c->currentText();
    else if (const auto *s = qobject_cast<const QAbstractSpinBox *>(w))
        t = s->text();
    return t.remove(QLatin1Char('&'));
}

QWidget *resolve(const QString &sel)
{
    const QList<QWidget *> list = allWidgets();

    if (sel.startsWith(QLatin1Char('@'))) {
        bool ok = false;
        const int i = sel.mid(1).toInt(&ok);
        return ok && i >= 0 && i < list.size() ? list.at(i) : nullptr;
    }

    // Mọi cách chỉ đích đều **ưu tiên widget đang nhìn thấy được**: cùng một
    // chữ, cùng một lớp có mặt ở nhiều tab, mà chỉ tab đang mở mới bấm được.
    // Lấy nhầm cái đang khuất thì lệnh chạy êm ru mà không có tác dụng gì.
    const auto pick = [&list](auto match) -> QWidget * {
        QWidget *hidden = nullptr;
        for (QWidget *w : list) {
            if (!match(w))
                continue;
            if (w->isVisible())
                return w;
            if (!hidden)
                hidden = w;
        }
        return hidden;
    };

    if (sel.startsWith(QLatin1Char('#'))) {
        const QString name = sel.mid(1);
        return pick([&name](QWidget *w) { return w->objectName() == name; });
    }

    if (sel.startsWith(QLatin1Char('.'))) {
        const QByteArray cls = sel.mid(1).toUtf8();
        return pick([&cls](QWidget *w) {
            return cls == w->metaObject()->className();
        });
    }

    // Chữ trên widget, hoặc tiêu đề cửa sổ — để chỉ được cả cửa sổ con
    // ("Tham số quỹ đạo") lẫn cái nút bên trong nó.
    return pick([&sel](QWidget *w) {
        return widgetText(w) == sel
               || (w->isWindow() && w->windowTitle() == sel);
    });
}

/// Widget nhận được lệnh set/check/key: chính nó, hoặc ô nhập đầu tiên bên
/// trong nó.
///
/// Tab "Điều khiển" gói mỗi ô nhập trong một QWidget rỗng cùng với cái nhãn đỏ
/// báo lệch trạng thái, nên thứ nằm ở ô "field" của QFormLayout là cái bọc chứ
/// không phải ô nhập. Lần xuống một tầng thì kịch bản vẫn viết theo nhãn được.
QWidget *inputInside(QWidget *w)
{
    if (!w || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w)
        || qobject_cast<QAbstractButton *>(w) || qobject_cast<QLineEdit *>(w)
        || qobject_cast<QSlider *>(w))
        return w;

    for (QWidget *c : w->findChildren<QWidget *>()) {
        if (qobject_cast<QAbstractSpinBox *>(c) || qobject_cast<QComboBox *>(c)
            || qobject_cast<QLineEdit *>(c) || qobject_cast<QSlider *>(c)
            || qobject_cast<QAbstractButton *>(c))
            return c;
    }
    return w;
}

/// Ô nhập đi kèm một cái nhãn.
///
/// Kịch bản viết `set "Lớn nhất (m/s)" 20` cho dễ đọc, nhưng chữ đó nằm trên
/// QLabel chứ không nằm trên ô nhập — hai widget khác nhau. Hàm này bắc cầu:
/// hỏi buddy trước, không có thì lấy widget đứng ngay sau nhãn trong cùng bố
/// cục. QFormLayout đặt nhãn và ô nhập cạnh nhau nên cách sau luôn đúng với các
/// cửa sổ tham số của phần mềm này.
QWidget *fieldFor(QWidget *w)
{
    auto *label = qobject_cast<QLabel *>(w);
    if (!label)
        return w;
    if (QWidget *b = label->buddy())
        return b;

    QWidget *parent = label->parentWidget();
    QLayout *lay = parent ? parent->layout() : nullptr;
    if (!lay)
        return w;

    if (auto *form = qobject_cast<QFormLayout *>(lay)) {
        int row = -1;
        QFormLayout::ItemRole role = QFormLayout::LabelRole;
        form->getWidgetPosition(label, &row, &role);
        if (row >= 0) {
            if (QLayoutItem *it = form->itemAt(row, QFormLayout::FieldRole)) {
                if (QWidget *f = it->widget())
                    return inputInside(f);
            }
        }
        return w;
    }

    for (int i = 0; i < lay->count() - 1; ++i) {
        if (lay->itemAt(i)->widget() != label)
            continue;
        if (QWidget *f = lay->itemAt(i + 1)->widget())
            return inputInside(f);
    }
    return w;
}

// ------------------------------------------------------------------ các lệnh --

void doDump(const QString &sel, bool all)
{
    const QList<QWidget *> list = allWidgets();
    QWidget *root = (sel.isEmpty() || all) ? nullptr : resolve(sel);
    if (!sel.isEmpty() && !all && !root) {
        out() << "  (không tìm thấy " << sel << ")\n";
        return;
    }

    for (int i = 0; i < list.size(); ++i) {
        QWidget *w = list.at(i);
        if (root && w != root && !root->isAncestorOf(w))
            continue;

        // Mặc định chỉ liệt kê thứ đang nhìn thấy được — tức thứ bấm được thật.
        // Các trang tab chưa mở lần nào còn chưa được dàn trang, kích thước in
        // ra là số mặc định vô nghĩa, mà số lượng thì gấp năm lần.
        if (!all && !w->isVisible())
            continue;

        // Widget ruột của Qt (ô nhập bên trong hộp số, danh sách xổ của hộp
        // chọn): điều khiển chúng trực tiếp là sai cách, chỉ tổ rối mắt.
        if (w->objectName().startsWith(QLatin1String("qt_")))
            continue;

        // Chỉ liệt kê thứ bấm được hoặc đọc được — liệt kê hết thì mỗi lần dump
        // ra vài trăm dòng khung với ô đệm, không đọc nổi.
        const bool useful = qobject_cast<QAbstractButton *>(w)
                            || qobject_cast<QComboBox *>(w)
                            || qobject_cast<QAbstractSpinBox *>(w)
                            || qobject_cast<QLineEdit *>(w)
                            || qobject_cast<QSlider *>(w)
                            || qobject_cast<QTabBar *>(w)
                            || qobject_cast<QAbstractItemView *>(w)
                            || qobject_cast<QLabel *>(w);
        if (!useful)
            continue;

        out() << QStringLiteral("@%1 %2%3 %4 [%5x%6]")
                     .arg(i, -4)
                     .arg(QString::fromUtf8(w->metaObject()->className()), -20)
                     .arg(w->isVisible() ? QString() : QStringLiteral(" (ẩn)"))
                     .arg(w->objectName().isEmpty()
                              ? QString()
                              : QStringLiteral("#") + w->objectName())
                     .arg(w->width())
                     .arg(w->height());

        const QString t = widgetText(w);
        if (!t.isEmpty())
            out() << "  \"" << t << '"';
        if (auto *bar = qobject_cast<QTabBar *>(w)) {
            out() << "  tab:";
            for (int k = 0; k < bar->count(); ++k)
                out() << " \"" << bar->tabText(k) << '"';
        }
        out() << '\n';
    }
    out().flush();
}

void doTable(QWidget *w)
{
    auto *view = qobject_cast<QAbstractItemView *>(w);
    if (!view || !view->model()) {
        out() << "  (không phải bảng)\n";
        return;
    }
    QAbstractItemModel *m = view->model();

    QStringList head;
    for (int c = 0; c < m->columnCount(); ++c)
        head << m->headerData(c, Qt::Horizontal).toString();
    out() << "  | " << head.join(QStringLiteral(" | ")) << " |\n";

    for (int r = 0; r < m->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < m->columnCount(); ++c)
            row << m->index(r, c).data().toString();
        out() << "  | " << row.join(QStringLiteral(" | ")) << " |\n";
    }
    out() << "  (" << m->rowCount() << " dòng)\n";
    out().flush();
}

void doSet(QWidget *w, const QString &value)
{
    if (auto *s = qobject_cast<QSpinBox *>(w))
        s->setValue(value.toInt());
    else if (auto *d = qobject_cast<QDoubleSpinBox *>(w))
        d->setValue(value.toDouble());
    else if (auto *e = qobject_cast<QLineEdit *>(w))
        e->setText(value);
    else if (auto *sl = qobject_cast<QSlider *>(w))
        sl->setValue(value.toInt());
    else if (auto *c = qobject_cast<QComboBox *>(w)) {
        const int i = c->findText(value);
        if (i >= 0)
            c->setCurrentIndex(i);
        else
            c->setCurrentIndex(value.toInt());
    } else {
        out() << "  (không đặt được giá trị cho "
              << w->metaObject()->className() << ")\n";
    }
}

/// Tách một dòng lệnh thành các từ, giữ nguyên phần trong ngoặc kép.
QStringList split(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("\"([^\"]*)\"|(\\S+)"));
    QStringList parts;
    auto it = re.globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        parts << (m.captured(1).isNull() ? m.captured(2) : m.captured(1));
    }
    return parts;
}

} // namespace

int main(int argc, char *argv[])
{
    // Mặc định chạy ngoài màn hình. Đặt sẵn biến môi trường thì tôn trọng lựa
    // chọn đó — để xem tận mắt thì QT_QPA_PLATFORM=wayland.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(appinfo::organizationName());
    QCoreApplication::setApplicationName(appinfo::displayName());
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(theme::styleSheet());

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        help();
        return 2;
    }

    QFile f(args.at(1));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out() << "không mở được kịch bản " << args.at(1) << '\n';
        out().flush();
        return 2;
    }

    MainWindow w;
    w.resize(1600, 900);
    w.show();
    QTest::qWait(800);   // bản đồ và các tab dựng xong đã

    QTextStream in(&f);
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        ++lineNo;

        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QStringList a = split(line);
        const QString cmd = a.at(0);
        const QString sel = a.size() > 1 ? a.at(1) : QString();

        out() << lineNo << "> " << line << '\n';

        // Hai lệnh không cần đích.
        if (cmd == QLatin1String("quit"))
            break;
        if (cmd == QLatin1String("wait")) {
            QTest::qWait(sel.toInt());
            continue;
        }
        if (cmd == QLatin1String("resize")) {
            w.resize(a.value(1).toInt(), a.value(2).toInt());
            QTest::qWait(200);
            continue;
        }
        if (cmd == QLatin1String("dump")) {
            doDump(sel, sel == QLatin1String("all"));
            continue;
        }
        if (cmd == QLatin1String("shot")) {
            // Tham số đầu của shot là tên file, không phải đích — nên lệnh này
            // phải nằm trước chỗ tra đích ở dưới.
            QWidget *target = a.size() > 2 ? resolve(a.at(2)) : &w;
            if (!target) {
                out() << "  (không tìm thấy " << a.at(2) << ", chụp cửa sổ chính)\n";
                target = &w;
            }
            // Chụp cả cửa sổ chứa nó nếu đích là một cửa sổ con — ảnh mới có
            // khung và tiêu đề, nhìn ra ngay đang xem cái gì.
            out() << "  " << (target->grab().save(sel) ? "đã chụp" : "chụp hỏng")
                  << " -> " << sel << '\n';
            out().flush();
            continue;
        }
        if (cmd == QLatin1String("windows")) {
            for (QWidget *t : QApplication::topLevelWidgets()) {
                if (!t->isVisible())
                    continue;
                out() << "  " << t->metaObject()->className()
                      << "  \"" << t->windowTitle() << "\"\n";
            }
            out().flush();
            continue;
        }
        if (cmd == QLatin1String("tab")) {
            // Nhãn tab không thuộc widget nào tìm được theo chữ, phải hỏi
            // thẳng thanh tab.
            bool done = false;
            for (QWidget *cand : allWidgets()) {
                auto *bar = qobject_cast<QTabBar *>(cand);
                if (!bar)
                    continue;
                for (int k = 0; k < bar->count() && !done; ++k) {
                    if (bar->tabText(k) != sel)
                        continue;
                    QTest::mouseClick(bar, Qt::LeftButton, {},
                                      bar->tabRect(k).center());
                    done = true;
                }
                if (done)
                    break;
            }
            if (!done)
                out() << "  (không thấy tab " << sel << ")\n";
            QTest::qWait(200);
            continue;
        }

        QWidget *t = resolve(sel);
        if (!t) {
            out() << "  (không tìm thấy " << sel << ")\n";
            out().flush();
            continue;
        }

        if (cmd == QLatin1String("click")) {
            QTest::mouseClick(t, Qt::LeftButton);
            QTest::qWait(200);
        } else if (cmd == QLatin1String("mouse")) {
            QTest::mouseClick(t, Qt::LeftButton, {},
                              QPoint(a.value(2).toInt(), a.value(3).toInt()));
            QTest::qWait(200);
        } else if (cmd == QLatin1String("rclick")) {
            QTest::mouseClick(t, Qt::RightButton, {},
                              QPoint(a.value(2).toInt(), a.value(3).toInt()));
            QTest::qWait(200);
        } else if (cmd == QLatin1String("wheel")) {
            const QPoint p(a.value(2).toInt(), a.value(3).toInt());
            const int n = a.value(4).toInt();
            QWheelEvent ev(p, t->mapToGlobal(p), QPoint(), QPoint(0, n * 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(t, &ev);
            QTest::qWait(200);
        } else if (cmd == QLatin1String("check")) {
            auto *b = qobject_cast<QAbstractButton *>(fieldFor(t));
            const bool want = a.value(2) == QLatin1String("on");
            if (!b)
                out() << "  (không phải ô tích)\n";
            else if (b->isChecked() != want)
                QTest::mouseClick(b, Qt::LeftButton);
            QTest::qWait(200);
        } else if (cmd == QLatin1String("set")) {
            doSet(fieldFor(t), a.value(2));
            QTest::qWait(200);
        } else if (cmd == QLatin1String("key")) {
            QTest::keySequence(fieldFor(t), QKeySequence(a.value(2)));
            QTest::qWait(200);
        } else if (cmd == QLatin1String("type")) {
            QTest::keyClicks(fieldFor(t), a.value(2));
            QTest::qWait(200);
        } else if (cmd == QLatin1String("text")) {
            // Đi qua fieldFor như set/type: chỉ vào nhãn thì cái đáng đọc là ô
            // nhập bên cạnh, chứ không phải đọc lại chính cái nhãn vừa gõ.
            out() << "  \"" << widgetText(fieldFor(t)) << "\"\n";
        } else if (cmd == QLatin1String("table")) {
            doTable(t);
        } else {
            out() << "  (lệnh lạ)\n";
        }
        out().flush();
    }

    out().flush();
    return 0;
}
