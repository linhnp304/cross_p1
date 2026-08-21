# ar0101 — AR01.01

Màn hình trắc thủ ra đa, Qt Widgets đa nền tảng (Ubuntu, Windows, macOS) — một codebase, build & chạy trên cả ba hệ điều hành.

## Yêu cầu

- CMake >= 3.16
- Qt 6 (Widgets, Network, Sql)
- Trình biên dịch hỗ trợ C++17 (GCC/Clang trên Linux/macOS, MSVC trên Windows)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Chạy binary sinh ra trong thư mục `build/` (ví dụ `build/ar0101` trên Linux/macOS, `build/Release/ar0101.exe` trên Windows).

### Bố cục mã nguồn

Mỗi thư mục con của `src/` là một tầng của phần mềm:

| Thư mục | Chứa gì |
|---|---|
| `app/` | Khởi tạo, cửa sổ chính, hai file cấu hình (`AppSettings`, `AppParams`) |
| `net/` | Giao thức gói tin (`rawpacket`, `packetio`, `cmdproto`), luồng nhận UDP, phía gửi |
| `proc/` | Tách tâm chùm xung, bám quỹ đạo, tính thanh ghi ADF4159 — thuần thuật toán, không đụng Qt Widgets |
| `record/` | Ghi lưu và phát lại: định dạng `.rec`, luồng ghi đĩa, danh mục phiên |
| `maps/` | Nền bản đồ số: tile, shapefile, chiếu toạ độ, tính phương vị / cự ly |
| `ui/` | Các tab, cửa sổ con và phần vẽ |

Include viết đủ đường dẫn (`#include "net/udplink.h"`), nên nhìn một dòng
include là biết thứ đang dùng thuộc tầng nào. Thư mục `proc/` cố ý không phụ
thuộc gì vào `ui/`: hai thuật toán nặng nhất vì thế biên dịch và chạy thử được
mà không cần dựng cả giao diện.

### Điều khiển giao diện bằng kịch bản

`tests/guidrv.cpp` dựng đúng cửa sổ chính của phần mềm rồi bấm nút, gõ số và
chụp màn hình theo một file kịch bản — để kiểm tra được phần **nhìn thấy** mà
không cần ai ngồi bấm chuột, và chạy được cả trên máy không có màn hình.

```bash
cmake --build build --target ar0101-guidrv
cd build && ./ar0101-guidrv ../tests/scripts/bam-quy-dao.txt
```

Đích này **không** nằm trong bản dựng bình thường (`EXCLUDE_FROM_ALL`), phải gọi
đúng tên mới dựng — nên máy dựng tự động không cần tới `Qt6::Test`. Chạy không
tham số thì in ra bảng lệnh của kịch bản. Mặc định chạy ngoài màn hình; đặt
`QT_QPA_PLATFORM=wayland` (hay `xcb`) nếu muốn xem tận mắt.

Kịch bản mẫu nằm ở `tests/scripts/`. Lưu ý `bam-quy-dao.txt` có bấm **Áp dụng**
trong cửa sổ tham số, tức là nó **ghi đè `build/params.json`** — sao lưu file đó
trước nếu đang giữ một bộ tham số cần dùng.

## File cấu hình

Hai file, đều nằm **ngay cạnh file chạy** và đều sinh ra ở lần chạy đầu:

| File | Chứa gì | Của ai |
|---|---|---|
| `settings.json` | Cấu hình hiển thị: nền bản đồ, tâm đài, cự ly tối đa, vòng cự ly, tốc độ mờ video, kích thước và màu các đối tượng đồ hoạ, danh sách phân loại mục tiêu | Trắc thủ |
| `params.json` | Tham số kỹ thuật (Fs, B, Tc, Multi_V), bảng rẻ quạt xử lý, bảng vùng cấm khởi tạo, tham số hai thuật toán xử lý, giá trị các [lệnh điều khiển](#lệnh-điều-khiển), tham số [kit ADF4159](#điều-khiển-kit-tạo-tín-hiệu-adf4159), và danh sách cổng UDP | Người lắp đặt |

> File cấu hình hiển thị trước đây tên là `mx01.json`. Còn file cũ mà chưa có
> `settings.json` thì phần mềm đọc file cũ rồi ghi sang tên mới ở lần lưu kế
> tiếp — đổi tên file không làm mất cài đặt. File cũ để nguyên làm bản lùi.

Cả bộ (file chạy + hai file cấu hình + bản đồ) mang sang máy khác là chạy được ngay.

Dữ liệu ghi lưu cũng nằm cạnh file chạy, trong thư mục `records/` — xem
[Ghi lưu và phát lại](#ghi-lưu-và-phát-lại).

> Lúc phát triển, file chạy nằm trong `build/` nên cấu hình cũng ở đó và sẽ mất
> khi xoá thư mục `build`. Không sao — thiếu file thì phần mềm dùng giá trị mặc định.

## Lắp đặt lên máy mới

Chép cả bộ sang máy sạch rồi làm bốn việc sau. Hai việc đầu mà thiếu thì màn
hình trống trơn.

**1. Mở tường lửa cho các cổng nhận.** Chỗ này mất thời gian nhất khi lắp đặt
thật, vì triệu chứng đánh lừa: tường lửa chặn thì Wireshark **vẫn bắt được gói**
(nó nghe trước tường lửa) nhưng phần mềm không nhận được gì.

```bash
sudo ufw allow in on <tên-card> to any port 8200,8300 proto udp
```

Windows: Windows Defender Firewall → Inbound Rules → New Rule → UDP, cổng
`8200,8300` → Allow.

**2. Điền tab Kết nối** theo đúng thực tế đấu nối:

| Cột | Điền gì | Ví dụ thực địa |
|---|---|---|
| `LocalIP` | IP của **card mạng nối với đài** trên máy này | `192.168.1.223` |
| `RemoteIP` | IP của đài | `192.168.1.225` |
| `LocalPort` | Cổng nhận RAW_V / RAW_P | `8200` / `8300` |
| `RemotePort` | Luôn để `0` — đài gửi từ cổng nguồn ngẫu nhiên | `0` |

`LocalIP` chỉ để **chọn card**, không phải địa chỉ đem đi bind: đài phát quảng bá
(tới `192.168.1.255`) nên phần mềm luôn nghe trên mọi địa chỉ rồi lọc lại theo
card. Để trống hoặc `0.0.0.0` là nghe trên mọi card. Mỗi cổng chỉ khai một dòng.

**3. Đừng chạy bằng `sudo`.** Chạy một lần bằng root là hai file cấu hình và cả
thư mục `records/` đổi chủ sang root; sau đó chạy bằng người dùng thường sẽ
**không lưu được** thay đổi nào nữa mà cũng không báo lỗi. Lỡ rồi thì
`sudo chown -R $USER params.json settings.json records`.

**4. Đối chiếu tham số cho khớp đài.** `Fs`, `B`, `Tc` bên tab **Tham số**;
`DataSend`, `ZFbeat`, `GainU` bên tab **Điều khiển**. Sai `DataSend` thì nền tạp
hoặc đen kịt hoặc trắng xoá dù dữ liệu về đủ — xem
[Quy biên độ về thang 0..255](#quy-biên-độ-về-thang-0255).

### Không thấy dữ liệu thì xem ở đâu

Dòng chữ dưới bảng cổng nói thẳng đang hỏng ở đâu: chưa nhận được gói nào, có
gói nhưng sai giao thức, hay đang nhận bình thường. Vẫn bí thì đếm xem gói chết
ở tầng nào — không cần quyền root:

```bash
nstat -az > /tmp/n1; sleep 5; nstat -az > /tmp/n2
join /tmp/n1 /tmp/n2 | awk '{d=$4-$2; if(d!=0 && ($1 ~ /^Ip/ || $1 ~ /^Udp/)) print $1, d}'
```

- `IpReasmOKs` tăng đúng nhịp gói mà `IpInDelivers` đứng im → **tường lửa chặn**.
- `IpInDelivers` tăng mà `UdpNoPorts` cũng tăng → chưa ai mở cổng đó.
- `IpExtInBcastPkts` chiếm phần lớn → đài đang phát quảng bá, đúng như dự tính.

Nhật ký `/var/log/ufw.log` chỉ ghi ~3 dòng mỗi phút, nên **không có dòng nào
không có nghĩa là không bị chặn**.

## Nhận dữ liệu ra đa

Tab **Kết nối** là tab mở sẵn khi chạy. Nó liệt kê các cổng UDP nhận dữ liệu;
mặc định có sẵn ba dòng `RAW_V` (6001), `RAW_P` (6002), `Status` (6003) trên
`127.0.0.1`.

Cột **Tên** là ComboBox với bốn loại dữ liệu — `RAW_V`, `RAW_P`, `Status`,
`Plot` — nhưng vẫn gõ được tên khác. Dòng `Plot` (điểm dấu tâm chùm do hệ thống
khác tính sẵn) **không** được tạo sẵn; bấm *Thêm dòng* là ra một dòng `Plot` ở
cổng 6004. Gói `PlotTC` nhận trên dòng đó đi thẳng vào bộ bám quỹ đạo, bỏ qua
thuật toán tách chùm xung — đây là đường dùng để
[kiểm tra riêng bộ lọc Kalman](#công-cụ-tạo-giả-dữ-liệu).

Hai chiều dữ liệu bật/tắt độc lập, mỗi nút nằm trong chính group của nó:

| Nút | Ở đâu | Tác dụng |
|---|---|---|
| *Bắt đầu / Dừng nhận dữ liệu* | group **Cổng UDP nhận dữ liệu** | mở các cổng trong bảng nhận |
| *Bắt đầu / Dừng gửi dữ liệu* | group **Cổng UDP gửi dữ liệu** | công tắc chung của phía gửi |

Cổng gửi chỉ thực sự mở khi nút chung đang bật **và** dòng đó đã tích ô *Gửi*.

- `LocalIP` là địa chỉ của **card mạng** nối với đài, không phải địa chỉ đem đi
  bind — xem [Lắp đặt lên máy mới](#lắp-đặt-lên-máy-mới). Để trống hoặc
  `0.0.0.0` là nghe trên mọi card.
- `RemoteIP` để trống hoặc `0.0.0.0`, `RemotePort` để `0` → nhận từ **mọi máy /
  mọi cổng**. Đây là mặc định, vì bên gửi thường dùng cổng nguồn ngẫu nhiên.
- Cột **Tên** chỉ là nhãn cho người đọc. Gói tin được phân loại theo header của
  chính nó, nên đổi tên hay gộp cổng cũng không làm hỏng việc giải mã.
- Bảng cổng **nhận** chỉ sửa được lúc đã dừng kết nối. Bảng cổng **gửi** thì sửa
  được bất cứ lúc nào — xem [Gửi dữ liệu đi hệ thống khác](#gửi-dữ-liệu-đi-hệ-thống-khác).

#### Cổng nhận Status

Dưới bảng có ô **Tự động cấu hình cổng nhận Status theo cổng gửi lệnh điều khiển
Command**, mặc định **bật**.

Đài trả trạng thái về **đúng địa chỉ và cổng đã gửi lệnh tới nó**, chứ không phải
tới một cổng cố định. Mà cổng nguồn ấy thường do hệ điều hành tự chọn (`LocalPort`
của dòng `Command` để `0`) nên mỗi lần chạy một số khác nhau — không có cách nào
khai trước trong bảng cổng nhận. Hai socket cũng không cùng bind được một cổng,
nên chính socket đã gửi câu hỏi phải là nơi nghe câu trả lời.

| Ô | Nghe trạng thái ở đâu | Dòng `Status` trong bảng |
|---|---|---|
| **bật** (mặc định) | ngay trên socket gửi lệnh của dòng `Command` | bị bỏ qua, hiện mờ |
| tắt | cổng của dòng `Status` trong bảng | có hiệu lực |

Bật thì đường trạng thái **không phụ thuộc nút *Bắt đầu nhận dữ liệu***, giống
như chiều gửi lệnh không phụ thuộc nút *Bắt đầu gửi dữ liệu*: vặn một nút bên tab
*Điều khiển* là thấy đài trả lời ngay. Số cổng hệ điều hành vừa chọn hiện ở cuối
dòng trạng thái của tab (`Status theo cổng lệnh 54563: 3 gói`).

Tắt thì nhớ để `LocalPort` của dòng `Status` **khác** `LocalPort` của dòng
`Command` — trùng nhau thì cổng nhận báo lỗi không mở được.

Dữ liệu `RAW_V` (1024 điểm biên độ mỗi gói, ~400 gói/giây) hiện ở hai nơi: nền
tạp trên bản đồ (panel 1) và đường biên độ trên cửa sổ biên độ (panel 2.2).

#### Quy biên độ về thang 0..255

Có **hai công thức**, chọn theo trường `DataSend` của lệnh `CMD_COMMON` (tab
[Điều khiển](#lệnh-điều-khiển)):

| `DataSend` | Công thức |
|---|---|
| 1 — Fbeat | `Video[i] = Data_V[i] / ZFbeat * 256 * Multi_V` |
| 2 — Doppler | `Video[i] = (Data_V[i] >> 16) / GainU * 256 * Multi_V` |

`ZFbeat` nằm trong `CMD_DSP_R`, `GainU` nằm trong `CMD_DSP_S` — cả hai nhập ở
tab **Điều khiển**, không phải tab Tham số. `Multi_V` (*Hệ số nhân video*) là
tham số của riêng phần mềm, nằm ở tab **Tham số**. Kết quả vượt 255 thì bão hoà;
hai số chia được chặn `> 0`. `DataSend` = 0 hoặc 3 thì dùng công thức của Fbeat.

> Đài thật đang phát ở chế độ Doppler đóng biên độ vào **bit 16..31**. Trước
> giai đoạn 9 phần mềm chỉ có công thức Fbeat nên mọi bản ghi của đài thật đều
> bão hoà trắng màn hình; chọn đúng `DataSend = 2` là hết.

Đọc số tại một điểm cụ thể:

- **Trên bản đồ** — thanh trạng thái hiện kinh/vĩ độ con trỏ, kèm **phương vị và
  cự ly** tính từ tâm đài, làm tròn tới 0.001° và 1 m.
- **Trên cửa sổ biên độ** — rê chuột vào là hiện biên độ và cự ly của ô đang
  trỏ, kèm vạch chỉ vị trí. Cự ly theo đúng công thức của cự ly tối đa, thay số
  ô vào chỗ 1024, rút gọn còn **R = Rmax·(D+1)/1024** với `D` đánh số từ 0; ô
  cuối cùng (`D = 1023`) vì thế đúng bằng cự ly tối đa. Panel hẹp thì hai nhãn
  `0` và cự ly tối đa tự ẩn để nhường chỗ.

Cùng phép quy đổi đó dùng cho ô cự ly của điểm dấu — cả điểm dấu đơn xung lẫn
điểm dấu tâm chùm, để chấm đơn xung nằm đúng trên điểm dấu mà nó sinh ra.

Một cột màn hình thường gộp vài ô cự ly (1024 ô mà panel chỉ rộng vài trăm
pixel). Cả đường biên độ lẫn số đọc đều lấy **ô cao nhất** trong nhóm, nên con
số luôn khớp với cái đỉnh đang nhìn thấy chứ không phải một ô lân cận thấp hơn.

Việc đọc socket và giải mã chạy trên một luồng riêng, đẩy vào hai bộ đệm tách
biệt: một cho hiển thị, một cho [ghi lưu](#ghi-lưu-và-phát-lại). Bộ đệm hiển thị
đầy thì bỏ bớt lượt quét cũ; bộ đệm ghi lưu là đường riêng nên không thủng vì lý
do đó.

### Tab Tham số

Cự ly tối đa suy ra từ tham số theo công thức rút gọn **Rmax = 75·Fs·Tc/B** (mét).
Với giá trị mặc định Fs=1, B=154, Tc=2500 thì Rmax = 1217.53 m → **1.218 km**.

- Bật **Tự động cập nhật thang cự ly** rồi bấm *Áp dụng* → ô "Cự ly tối đa" bên
  tab Cài đặt và các vòng cự ly tự đổi theo.
- Bật **Tự động nhận từ trạng thái lệnh điều khiển** → nút *Áp dụng* bị khoá
  (mọi thay đổi vào thẳng) và ô tự cập nhật thang cự ly bị bật cố định.

Group **Tham số kỹ thuật** có ba ô `Fs`, `B`, `Tc` và ô **Hệ số nhân video —
Multi_V** (số thực, mặc định 1.0). `Multi_V` chỉ chỉnh độ sáng nền tạp phía phần
mềm, không đụng gì tới đài; có hiệu lực ngay, không chờ *Áp dụng*.

> Ô `ZFbeat` **không còn ở đây** từ giai đoạn 9: nó là một trường của lệnh
> `CMD_DSP_R`, nhập ở tab [Điều khiển](#lệnh-điều-khiển). File `params.json` của
> bản cũ được tự chuyển đổi sang chỗ mới lúc nạp, không mất giá trị đã căn.

Hai nút ở cuối tab mở hai cửa sổ tham số của phần xử lý — xem
[Điểm dấu và quỹ đạo](#điểm-dấu-và-quỹ-đạo).

**Xử lý theo rẻ quạt** giới hạn phần dữ liệu `RAW_P` được đưa vào xử lý. Bảng
nhận nhiều rẻ quạt, mỗi dòng một góc bắt đầu và một góc kết thúc (làm tròn
0.1°). Góc tính theo chiều kim đồng hồ từ góc bắt đầu tới góc kết thúc, nên
*bắt đầu > kết thúc* là rẻ quạt vắt qua hướng bắc chứ không phải nhập ngược.

- Không dòng nào tích ô *Áp dụng* → xử lý cả vòng tròn. Có dòng bật thì chỉ xử
  lý phần nằm trong các dòng đó.
- Hai rẻ quạt chồng lấn nhau thì các dòng phạm lỗi bị tô đỏ và **cả bảng không
  có hiệu lực** cho tới khi sửa xong. Kiểm tra trên mọi dòng, kể cả dòng đang
  tắt: hai rẻ quạt lồng nhau là cấu hình khó hiểu dù hôm nay dòng nào đang tắt.
  Hai rẻ quạt chạm nhau đúng một mép (30–90 và 90–120) **không** tính là chồng.

**Vùng cấm khởi tạo** là các mảnh hình quạt mà bên trong đó **không mở quỹ đạo
mới**. Quỹ đạo đã có bay qua vùng thì vẫn được bám tiếp bình thường — cùng tinh
thần với hai [chốt an toàn](#hai-chốt-an-toàn-chỉ-có-trong-paramsjson). Mỗi dòng
gồm phương vị đầu / cuối (0.1°) và cự ly đầu / cuối (0.1 m); mặc định bảng rỗng.

Thay vì gõ số, bấm **Vẽ trên bản đồ** rồi bấm chuột trái hai lần trên panel 1 để
khoanh vùng: con trỏ đổi thành dấu thập kèm khung nhỏ, khung xem trước đi theo
chuột, và hai điểm bấm được quy ra phương vị / cự ly rồi thêm thành một dòng
mới. Quét ngược hay xuôi chiều kim đồng hồ, từ trong ra hay từ ngoài vào đều
được — giá trị luôn được sắp lại thành xuôi chiều kim đồng hồ và cự ly tăng dần.
Bấm chuột phải hoặc `Esc` để huỷ.

Ô **Hiện vùng cấm khởi tạo** vẽ các vùng đang bật lên bản đồ. Vùng đang khoanh
dở thì luôn hiện, kể cả khi ô đó đang tắt.

Cả hai bảng có hiệu lực ngay, không cần bấm *Áp dụng* (nút đó thuộc về nhóm
thang cự ly).

### Công cụ tạo giả dữ liệu

Không có đài thật vẫn thử được toàn bộ đường nhận, xử lý và hiển thị.

```bash
python3 tools/fake_radar.py
```

Phát **cả `RAW_V` và `RAW_P`** (cổng 6001 và 6002) từ **cùng một nguồn phương
vị** — điều kiện bắt buộc để thuật toán tâm chùm và bộ bám quỹ đạo chạy đúng.
Nhịp 2.5 ms, 6 vòng/phút, ba mục tiêu: bay vòng tròn ~2 m/s, bay thẳng từ tâm
đài ra ~5.5 m/s, và bay xuyên tâm đài ~15.5 m/s. Xem `--help` để đổi cổng, tốc
độ vòng quét hay tỉ lệ mất xung.

Muốn thử **riêng bộ lọc Kalman**, không lẫn với thuật toán tách chùm xung:

```bash
python3 tools/fake_plottc.py
```

Công cụ này tính sẵn điểm dấu rồi gửi thẳng dưới dạng gói `PlotTC` tới cổng
6004, đồng thời vẫn phát `RAW_V` (cổng 6001) để làm nền tạp và để đồng bộ đường
quét — phương vị ăng-ten trong `RAW_V` chính là thứ đẩy nhịp chốt sổ của bộ bám.
Không có dòng đó thì bộ bám phải tự quay đường quét bằng đồng hồ theo chu kỳ đo
được lần cuối, vẫn chạy nhưng nhịp ngoại suy không còn khớp với thực tế.
**Không** phát `RAW_P`. Ba mục tiêu, mỗi mục tiêu một chu kỳ có/mất tiêu riêng
để thấy rõ tiêu chuẩn khởi tạo, ngoại suy và xoá quỹ đạo:

| Mục tiêu | Chuyển động | Chu kỳ theo vòng quét |
|---|---|---|
| 1 | vòng tròn xuôi kim đồng hồ, từ 90° / 250 m, 2 m/s | 5 có, 1 mất, 2 có, 2 mất, 3 có, 5 mất |
| 2 | thẳng từ tâm đài ra, 135°, từ 75 m, 1.5 m/s | 1 có, 1 mất |
| 3 | thẳng vào tâm đài, 295°, 1100 m → 75 m rồi lặp lại, 1.5 m/s | 1 có, 2 mất, 5 có, 4 mất |

Phải thêm dòng `Plot` (cổng 6004) trong tab **Kết nối** một lần thì mới nhận
được — dòng đó không có sẵn.

Muốn một **bài đo** thay vì một cảnh mô phỏng — chuyển động biết trước từng mét,
chu kỳ có/mất tiêu cố định, không nhiễu:

```bash
python3 tools/fake_one_target.py
```

Phát `RAW_V` và `RAW_P` đồng bộ như `fake_radar.py`, nhưng chỉ **một** mục tiêu:
bay vòng tròn quanh tâm đài xuôi kim đồng hồ, 2.5 m/s, bắt đầu ở phương vị 30° /
cự ly 500 m, tốc độ vòng quét tiêu chuẩn 6 vòng/phút. Bay vòng tròn quanh chính
tâm đài nên **cự ly là hằng số 500 m** suốt phiên — mọi thay đổi cự ly nhìn thấy
trên màn hình đều là sai số của thuật toán chứ không phải của mục tiêu.

Chu kỳ có tiêu / mất tiêu lặp lại sau 22 vòng quét:

| Vòng | 1 | 2 | 3 – 8 | 9 – 12 | 13 – 18 | 19 – 22 |
|---|---|---|---|---|---|---|
| | có | mất | có (6) | mất (4) | có (6) | mất (4) |

Vòng "mất tiêu" là mất hẳn: không plot trong `RAW_P` và cũng không có vệt sáng
trong `RAW_V`, nhìn màn hình là thấy đúng lúc bộ bám chuyển sang ngoại suy. Thêm
`--video-always` nếu muốn giữ vệt nền tạp lại. Mặc định **không** rắc nhiễu và
**không** làm rơi xung giữa chùm; `--jitter-cell` và `--dropout` bật lại hai thứ
đó khi cần thử độ bền của thuật toán.

Cự ly quy ra ô theo `--rmax` (mặc định 1218 m, ứng với Fs=1, B=154, Tc=2500).
Tham số bên tab **Tham số** cho ra Rmax khác thì phải truyền `--rmax` cho khớp,
nếu không cự ly hiện trên màn hình sẽ không phải 500 m. Bật ô **Hiện điểm dấu
đơn xung** trong tab Cài đặt để nhìn thẳng vào đầu vào của thuật toán tâm chùm.

Chỉ cần nền tạp thì vẫn dùng công cụ cũ:

```bash
python3 tools/fake_raw_v.py
```

> Chỉ chạy **một** công cụ tạo giả tại một thời điểm. Hai nguồn cùng phát vào
> một cổng thì phương vị của chúng lệch pha nhau, phần mềm sẽ tưởng ăng-ten quay
> hết vòng liên tục và không quỹ đạo nào hình thành được.

## Điểm dấu và quỹ đạo

Dữ liệu `RAW_P` mang các điểm dấu đơn xung. Đường xử lý gồm hai bước:

1. **Tâm chùm xung** — gom các xung liên tiếp ở gần cùng một ô cự ly và cùng mức
   dopler thành một "chùm", rồi lấy tâm chùm làm **điểm dấu** (PlotTC). Ô cự ly
   quy ra mét theo đúng công thức của cự ly tối đa: **R = Rmax·n/1024**.
2. **Bám quỹ đạo** — bộ lọc Kalman vận tốc không đổi trong hệ Đề-các cục bộ
   quanh tâm đài. Mỗi vòng quay ăng-ten là một nhịp, nhưng **mỗi quỹ đạo một
   nhịp riêng**: quỹ đạo được chốt sổ ngay khi đường quét đi hết cửa sổ dự đoán
   của nó, không phải chờ ăng-ten về hướng bắc. Không được ghép điểm dấu thì
   ngoại suy ngay lúc đó, ngoại suy quá số vòng cho phép thì xoá.

Vòng đời một quỹ đạo, theo trường `track_status` của gói tin:

| Bước | Trạng thái | Trên màn hình |
| --- | --- | --- |
| Vòng đầu có điểm dấu | *(chưa có)* — mới chỉ là một cửa sổ dự đoán mở cực đại đang chờ | không vẽ gì, không gửi đi đâu |
| Đủ tiêu chuẩn khởi tạo (2/2, 3/3 hay 2/3 vòng) | 1 — khởi tạo | bắt đầu hiện hình tam giác |
| Vòng sau, có điểm dấu rơi vào cửa sổ | 3 — đang bám | tam giác **tô đặc** |
| Vòng sau, không có điểm dấu | 5 — ngoại suy | tam giác **rỗng ruột**, dịch tới vị trí dự đoán |
| Ngoại suy quá số vòng cho phép | 6 — xoá | gửi trạng thái xoá rồi biến mất |

Chưa đủ tiêu chuẩn khởi tạo thì quỹ đạo **chưa tồn tại** với phần còn lại của
phần mềm: không vẽ, không có trong bảng danh sách, không gửi ra ngoài. Bật ô
*Vẽ cửa sổ dự đoán* trong cửa sổ **Tham số quỹ đạo** thì thấy được các cửa sổ
đang chờ đó, vẽ bằng **nét chấm** để phân biệt với nét đứt của quỹ đạo thật.

Kích thước cửa sổ dự đoán = *Cửa sổ cự ly cơ sở* + *Hệ số nhân độ lệch chuẩn* ×
độ bất định của bộ lọc sau khi dự đoán tới vòng sau. Nó **tự thu nhỏ** khi quỹ
đạo bám đều và **tự nở ra** khi ngoại suy hoặc khi mục tiêu cơ động. *Cửa sổ cự
ly tối đa* chỉ chặn trường hợp đang bám đều; ba trường hợp còn lại — vòng đầu
tiên, đang ngoại suy, vừa phát hiện cơ động — cửa sổ chỉ bị chặn bởi quãng đường
xa nhất mục tiêu có thể đi được, tức *Lớn nhất (m/s)* × thời gian trôi qua.

> [!IMPORTANT]
> Vì vậy ô **Lớn nhất (m/s)** phải khai sát với loại mục tiêu đang quan tâm.
> Với đài Rmax ≈ 1218 m và vòng quét 10 giây, để mặc định 120 m/s thì cửa sổ
> vòng đầu tiên rộng 1240 m — trùm cả vùng phủ, và mọi điểm dấu đều rơi vào quỹ
> đạo đầu tiên gặp được. Để 20 m/s thì cửa sổ đó còn 240 m.

Hình vẽ trên panel 1:

| Đối tượng | Hình | Ghi chú |
| --- | --- | --- |
| Điểm dấu | hình vuông nhỏ | luôn nằm **trên** lớp quỹ đạo; tự xoá sau 8 giây |
| Điểm dấu đơn xung | chấm tím rất nhỏ | lớp **dưới cùng**; mặc định tắt, xem bên dưới |
| Quỹ đạo | tam giác cân, quay theo hướng chuyển động | để rỗng ruột khi đang ngoại suy |
| Vết lịch sử | đường nối các vết (mặc định) hoặc từng chấm tròn | xem *Dạng vết lịch sử* bên dưới |
| Vùng cấm khởi tạo | mảnh hình quạt viền đứt, nền cam nhạt | chỉ hiện khi bật ô trong tab Tham số |

Hai ô **Hiện thông tin** trong tab Cài đặt bật thêm chữ cạnh hình. Ba nhãn nằm
ba phía khác nhau vì điểm dấu và quỹ đạo của cùng một mục tiêu gần như trùng vị
trí — cùng một phía là chữ đè lên chữ:

- **Quỹ đạo** — số đầu tốp phía trên, phương vị-cự ly bên phải (chỉ số, làm tròn
  0.01° và 0.1 m).
- **Điểm dấu** — phương vị-cự ly bên trái.

Màu của mọi đối tượng đổi được trong tab **Màu sắc**; ẩn/hiện, độ dài vết, dạng
vết và kích thước chỉnh trong tab **Cài đặt**, nhóm *Điểm dấu và quỹ đạo*:

- **Dạng vết lịch sử quỹ đạo** — *Đường* (mặc định) vẽ một đường gấp khúc màu
  vàng nối các vết và kéo dài tới vị trí hiện tại của quỹ đạo; màu đổi ở mục
  *Vết — đường nối* trong tab Màu sắc. *Điểm* vẽ từng chấm tròn một, màu theo
  trạng thái lúc để lại vết: xanh biển = đang bám, đỏ = ngoại suy, cam = còn lại.
- **Kích thước điểm dấu** và **Kích thước quỹ đạo** — năm nấc 50%, 75%, 100%
  (mặc định), 150%, 200%. Vết lịch sử và vùng bấm trúng quỹ đạo đi theo nấc của
  quỹ đạo. Kích thước đo bằng điểm ảnh màn hình, không theo mức phóng bản đồ.
  Chấm đơn xung đi theo nấc của điểm dấu, nhưng nhỏ hơn hẳn.

### Hiện điểm dấu đơn xung

Ô **Hiện điểm dấu đơn xung** (tab Cài đặt, nhóm *Điểm dấu và quỹ đạo*) vẽ mỗi
xung phát hiện trong gói `RAW_P` thành một chấm tím nhỏ — tức là **đầu vào** của
thuật toán tâm chùm, trước khi gom chùm. Mở nó ra là nhìn thấy thuật toán đã gom
những xung nào và loại những xung nào: một mục tiêu để lại cả một đám chấm tím,
và điểm dấu tâm chùm (hình vuông đỏ) phải nằm đúng giữa đám đó. Màu đổi ở mục
*Điểm dấu đơn xung* trong tab **Màu sắc**.

Ba điểm cần nhớ:

- Chấm đơn xung **xoá sớm hơn điểm dấu tâm chùm đúng một giây** (mặc định 7 giây
  so với 8). Hạn của điểm dấu tâm chùm đặt ở cửa sổ *Tham số tâm chùm*; đổi nó
  là hạn của lớp đơn xung đi theo.
- Lớp này nhận **mọi** plot trong gói, kể cả plot mà thuật toán sắp loại vì
  dopler hoặc vì nằm ngoài rẻ quạt xử lý — chính chỗ đó mới nhìn ra được thuật
  toán đang loại những gì.
- Một lần chùm tia quét qua chỉ trải chừng chục mét, nên ở mức phóng vừa cả dải
  cự ly thì cả đám chấm dính thành một vệt nhỏ. **Phóng to** panel 1 lên thì
  từng xung mới tách ra.

Mặc định tắt: một mục tiêu để lại vài chục chấm mỗi vòng quét, đây là lớp để soi
thuật toán chứ không phải để trực ban hàng ngày. Tắt ô này thì phần mềm cũng
không gom chấm nữa, không chỉ là không vẽ.

### Thao tác với quỹ đạo

- **Chuột trái** vào một quỹ đạo → popup thông tin nhanh; bấm ra ngoài thì đóng.
- **Chuột phải** vào một quỹ đạo → menu đổi đầu tốp, nhập độ cao, nhận dạng, xoá.
- Tab **Danh sách** có đủ các cột đó dưới dạng bảng, kèm ô **Theo dõi**. Quỹ đạo
  được theo dõi hiện thêm ô text bám cạnh nó trên bản đồ và luôn hiện **đủ** vết
  lịch sử, kể cả khi thanh trượt đang để ít vết.
- Kích đúp vào cột **VT / V / H** trong bảng cũng mở popup thông tin. Ba cột kia
  sửa được tại chỗ nên kích đúp ở đó là mở ô nhập.
- Nút **Thông tin chi tiết điểm dấu** mở cửa sổ theo dõi từng điểm dấu sinh ra.

Hàng nút dưới cùng của tab **Danh sách** xoá hàng loạt:

| Nút | Tác dụng |
|---|---|
| Xoá toàn bộ điểm dấu | xoá lớp điểm dấu đang vẽ trên bản đồ, cả tâm chùm lẫn đơn xung. Không đụng tới cửa sổ *Thông tin chi tiết điểm dấu* — cửa sổ đó là dòng chảy riêng và đã có nút xoá của nó |
| Xoá toàn bộ quỹ đạo | xoá mọi quỹ đạo, kèm cả đầu tốp / độ cao / phân loại đã nhập tay. Có hỏi lại trước, vì thuật toán không dựng lại được phần nhập tay đó. Mỗi quỹ đạo vẫn đi qua đúng đường xoá bằng tay nên hệ thống nhận đều nhận được `track_status = 6` |

Danh sách tên phân loại mục tiêu nằm trong `settings.json`, khoá `classifyNames` —
sửa thẳng trong file, chưa có giao diện quản lý.

### Tham số hai thuật toán

Hai nút ở cuối tab **Tham số** mở hai cửa sổ chỉnh được lúc đang chạy; giá trị
lưu vào `params.json`.

- **Tham số chùm xung** — tiêu chuẩn độ dài chùm, dopler, xét duyệt xung vào
  chùm, số chu kỳ mở/đóng chùm, phương án tính tâm (đơn giản hay có trọng số
  biên độ), **hiệu chỉnh tâm chùm**, và số giây giữ điểm dấu trên màn hình.
- **Tham số quỹ đạo** — tiêu chuẩn khởi tạo (2/2, 3/3, 2/3 vòng), số vòng ngoại
  suy, dải vận tốc quan tâm, cửa sổ liên kết, sai số đo và nhiễu quá trình, thời
  gian tự xoá khi không cập nhật, và ô bật vẽ cửa sổ dự đoán.

Một quỹ đạo bị xoá khi rơi vào **một trong bốn** điều kiện: quá số vòng ngoại
suy, ra khỏi vùng phủ của đài, vận tốc ra ngoài dải cho phép hai vòng liên tiếp,
hoặc quá **40 giây** (mặc định) không có điểm dấu nào ghép vào. Điều kiện thời
gian là cái chốt cuối: ăng-ten quay chậm lại hay ngừng quay thì số vòng ngoại
suy đếm mãi không tới ngưỡng.

> Hai tham số **số vòng ngoại suy** và **thời gian tự xoá** phải thoả
> `thời gian tự xoá > số vòng ngoại suy × chu kỳ vòng quét`. Ở 6 vòng/phút thì
> 3 × 10 = 30 giây < 40 giây, đúng. Nếu đài quay chậm hơn (4 vòng/phút → 45
> giây) thì ô 40 giây bắn trước và số vòng ngoại suy thành vô nghĩa.

Ba chỗ đáng chú ý khi chỉnh:

- **Gia tốc mục tiêu** là tham số nhạy nhất cả bộ: nó vào ma trận nhiễu theo
  `dt⁴`, mà `dt` là cả một vòng quay ăng-ten. Nó cũng là thứ quyết định bề rộng
  cửa sổ liên kết. Với chu kỳ 10 giây, đo được: 0.3 m/s² → cửa sổ 94 m,
  1 m/s² → 165 m, 3 m/s² → 366 m. Rộng thì bám dai qua chỗ mục tiêu ngoặt,
  nhưng dễ bắt nhầm sang mục tiêu bên cạnh.
- **Dải vận tốc** là cái van chính để lọc quỹ đạo rác. Xét cả lúc khởi tạo lẫn
  trong suốt quá trình bám: quỹ đạo có vận tốc ra ngoài dải hai vòng liên tiếp
  thì bị xoá.
- **Cửa sổ cự ly tối đa** chặn trần cửa sổ liên kết, dùng cho cả vòng quét đầu
  tiên khi chưa biết vận tốc. Với đài tầm gần, tích *vận tốc lớn nhất × chu kỳ
  vòng quét* có thể vượt cả cự ly tối đa; không có trần này thì cửa sổ ôm trọn
  màn hình và mọi điểm dấu đều rơi vào quỹ đạo đầu tiên gặp được.

Group **Hiệu chỉnh tâm chùm** bù sai lệch lắp đặt: **bù phương vị** (độ) và **bù
cự ly** (mét) cộng vào kết quả **sau khi** thuật toán đã tính xong tâm chùm, ngay
trước lúc gán vào `azm` và `range` của gói `PlotTC`. Phương vị bù xong quay vòng
về 0–360°; cự ly bù xong ra âm thì gán bằng 0. Cả hai mặc định 0.0, tức là không
bù gì. Lớp [điểm dấu đơn xung](#hiện-điểm-dấu-đơn-xung) **không** bù — nó là dữ
liệu thô của đài, để đối chiếu xem thuật toán đã làm gì với nó.

Ô **cửa sổ phương vị cơ sở** không tham gia việc ghép điểm dấu — cửa sổ liên kết
xét theo khoảng cách, vì xét riêng theo phương vị thì ở gần tâm đài cùng một
khoảng cách lại thành một góc rất lớn. Ô đó chỉ nới thêm hình cửa sổ dự đoán vẽ
trên màn hình và các trường window trong gói tin Track.

### Hai chốt an toàn chỉ có trong params.json

Không đưa lên giao diện vì đặt xong là quên, không phải thứ chỉnh trong lúc
chiến đấu. Sửa trong `params.json`, mục `track`:

| Khoá | Mặc định | Tác dụng |
|---|---|---|
| `maxTracks` | 200 | Trần số quỹ đạo. Gặp nhiễu dày thì danh sách phình vô hạn, mà mỗi điểm dấu phải quét qua toàn bộ danh sách — càng phình càng chậm, càng chậm càng phình |
| `minInitRangeM` | 50 | Cự ly nhỏ nhất cho phép **khởi tạo** quỹ đạo mới. Quanh tâm đài là chỗ địa vật mạnh nhất |

Cả hai chỉ chặn việc khởi tạo, không đụng tới quỹ đạo đã có: mục tiêu đang bám
mà bay qua vùng chết quanh tâm đài thì vẫn được cập nhật bình thường. Bảng
[Vùng cấm khởi tạo](#tab-tham-số) trong tab Tham số là chốt thứ ba cùng loại,
chỉ khác là khoanh theo vùng và chỉnh được ngay trên giao diện.

## Gửi dữ liệu đi hệ thống khác

Mỗi khi có điểm dấu mới hoặc một quỹ đạo được cập nhật, gói tin tương ứng được
gửi tới mọi dòng trong bảng **Cổng UDP gửi dữ liệu** (tab Kết nối) đang bật ô
*Gửi* và đúng loại dữ liệu. Lần chạy đầu (chưa có `params.json`) bảng có sẵn một
dòng `Command` ở cổng 6103, đã tích ô *Gửi*.

| Cột | Ý nghĩa |
|---|---|
| Gửi | Bật/tắt dòng đó. Dòng `Plot`/`Track` **luôn bắt đầu ở trạng thái tắt mỗi lần chạy** — mở phần mềm lên mà tự phát gói ra mạng là chuyện không ai muốn. Dòng `Command` thì giữ nguyên trạng thái đã lưu |
| Loại dữ liệu | `Plot` (điểm dấu tâm chùm), `Track` (quỹ đạo) hoặc `Command` ([lệnh điều khiển](#lệnh-điều-khiển)) |
| LocalIP | Card mạng **đi ra**. Để trống là theo bảng định tuyến của hệ điều hành. Chú ý: khác hẳn ý nghĩa của cột cùng tên bên bảng nhận |
| LocalPort | Để `0` là để hệ điều hành tự chọn cổng nguồn |
| RemoteIP / RemotePort | Đích đến. Dòng đã bật *Gửi* thì hai ô này bắt buộc có giá trị |
| Broadcast | Gửi tới địa chỉ quảng bá của dải chứa RemoteIP (ví dụ `192.168.0.110` → `192.168.0.255`) thay vì gửi thẳng cho một máy |

Địa chỉ quảng bá tra từ chính card mạng của máy chứ không thay số cuối bằng 255
— cách kia chỉ đúng với dải `/24`. Bật *Broadcast* mà không card nào của máy
cùng dải với RemoteIP thì dòng đó báo lỗi chứ không âm thầm gửi đơn hướng.

Bảng này sửa được cả lúc đang kết nối (khác bảng cổng nhận): mọi thay đổi mở
lại socket ngay. Địa chỉ trong bảng cũng vào danh sách kiểm tra thông mạng ở
góc trái thanh trạng thái.

Dòng `Command` **không nằm dưới** nút *Bắt đầu / Dừng gửi dữ liệu*: nút đó là
công tắc của dòng dữ liệu điểm dấu và quỹ đạo, còn lệnh điều khiển thì đi ra vì
trắc thủ vừa vặn một nút chứ không phải vì có dòng dữ liệu nào đang chảy.

Quỹ đạo bị xoá được gửi kèm `track_status = 6`, dù xoá bằng tay hay bằng thuật
toán — hệ thống nhận không biết thì nó giữ quỹ đạo đó trên màn hình vĩnh viễn.

### Kiểm tra phía gửi

```bash
python3 tools/recv_plot_track.py
```

Đóng vai hệ thống nhận: mở cổng 6101 và 6102, giải mã theo đúng bảng mô tả giao
thức rồi in ra, kèm kiểm tra Header và Length. Thêm `--raw` để in đủ từng
trường, `--port` để đổi cổng. Nghe trên `0.0.0.0` nên nhận được cả gói quảng bá.

## Lệnh điều khiển

Tab **Điều khiển** (giữa *Danh sách* và *Kết nối*) là nơi vặn các tham số của
đài. Bốn group, mỗi group là một gói tin:

| Group | Gói tin | Header |
|---|---|---|
| Điều khiển ăng ten | `CMD_ANTEN` | `0xA4A3A2A1` |
| Tham số chung | `CMD_COMMON` | `0x04030201` |
| Tham số DSP kênh cự ly | `CMD_DSP_R` | `0xD4D3D2D1` |
| Tham số DSP kênh tốc độ | `CMD_DSP_S` | `0xD9D8D7D6` |

`CMD_DSP_R` và `CMD_DSP_S` dùng **chung cả hai giá trị Category**, chỉ khác
Header và độ dài — nên việc phân loại gói bám vào Header, không bám vào Category.

**Không có nút gửi.** Mỗi lần đổi một lựa chọn hay một ô nhập là **cả gói** của
group đó đi ra ngay. Ô nhập tắt `keyboardTracking`, nên gõ dở một con số chưa
phát lệnh — chỉ khi rời ô, bấm `Enter`, hay bấm mũi tên.

Lệnh đi ra các dòng **`Command`** trong bảng cổng gửi; trạng thái phản hồi về
**đúng cổng nguồn của gói lệnh** — xem [Cổng nhận Status](#cổng-nhận-status).
Chưa cấu hình được cổng lệnh thì có một thông báo — hộp thoại đúng một lần, kèm
dòng chữ đỏ ở cuối tab.

### Đọc phần báo lệch

Nhận được trạng thái mà giá trị nào đó khác giá trị đang điều khiển (đài chưa
đáp ứng lệnh) thì chỗ đó được đánh dấu bằng **chữ đỏ**:

| Kiểu điều khiển | Cách báo |
|---|---|
| Nút chọn (radio) | lựa chọn ứng với **giá trị trạng thái** đổi sang chữ đỏ |
| Ô nhập | giá trị nhận về hiện bên phải ô, chữ đỏ |
| Hộp chọn | giá trị nhận về hiện bên phải hộp, chữ đỏ |

Nhãn mỗi group mang **hai số Serial**: `Tên group (serial lệnh - serial trạng
thái)`, cập nhật mỗi lần gửi lệnh thành công hoặc nhận được trạng thái. Hai số
lệch nhau nhiều là dấu hiệu đài không trả lời.

Ba trường **chỉ nhận trạng thái**, ô nhập bị khoá: `AT_Azm` (phương vị trả về),
`Beta_Back` (phương vị hiện tại) và `HW_Version` — phiên bản phần cứng về dưới
dạng `0xyyMMddhh`, hiện thành `yyyy/MM/dd-hh`.

Giá trị lệnh được lưu trong `params.json` (khoá `control`, theo **tên trường**),
nên mở phần mềm lên là thấy đúng thứ đã vặn hôm trước — và phép tính
`Video[1024]` có `ZFbeat` / `GainU` / `DataSend` dùng ngay từ gói đầu tiên chứ
không phải chờ đài gửi trạng thái về.

### Kiểm tra tab Điều khiển

```bash
python3 tools/fake_control.py --disobey
```

Đóng vai đài: nghe cổng lệnh 6103, giải mã gói vừa nhận rồi trả gói trạng thái
tương ứng về **đúng nơi gói lệnh đi ra**, như đài thật ([Cổng nhận
Status](#cổng-nhận-status)). Đưa `--status-port 6003` thì quay lại kiểu cũ — trả
về một cổng cố định, để thử nhánh ô tự động đang tắt. `--disobey` cố tình trả về
khác lệnh ở vài trường để thấy phần báo lệch bằng chữ đỏ; bỏ nó đi thì đài "nghe
lời" và mọi thứ sạch.
`AT_Azm` / `Beta_Back` trả về một phương vị quay đều 6 vòng/phút. Công cụ này trả
lời cả hai gói của kit ADF4159 dưới đây.

## Điều khiển kit tạo tín hiệu ADF4159

Nút **Điều khiển ADF4159** ở cuối tab *Điều khiển* mở một cửa sổ riêng: dựng lại
phần mềm gốc của Analog Devices (*ADF4158/9 PLL Software*) theo gam màu tối của
phần mềm này, và thay đường USB của nó bằng hai gói lệnh UDP.

| Gói tin | Header | Category lệnh / trạng thái | Mang gì |
|---|---|---|---|
| `CMD_ADF4159_REG8` | `0xADF4159A` | `0x5018` / `0x50180` | cả tám thanh ghi |
| `CMD_ADF4159_REG` | `0xADF4159B` | `0x6018` / `0x60180` | đúng một thanh ghi |

Lệnh đi ra chính các dòng **`Command`** của bảng cổng gửi, trạng thái về theo
đúng đường của bốn gói lệnh ở trên ([Cổng nhận Status](#cổng-nhận-status)) — kit
nằm trong cùng một đài nên dùng chung đường ấy, không phải cấu hình thêm gì.

Gói một thanh ghi **không có trường "số hiệu thanh ghi"**: ba bit thấp nhất của
chính từ 32 bit đó đã là số hiệu (000..111), đúng quy ước của con chip. Nhờ vậy
phần đọc trạng thái cũng biết ngay gói trả về nói về thanh ghi nào.

### Cách dùng

Khác hẳn tab *Điều khiển* — ở đó vặn một ô là lệnh đi ra ngay, còn ở đây **phải
bấm nút Ghi**. Lý do là của chính con chip: ghi `R0` mới là lúc kit chốt tần số,
nên "đổi tới đâu gửi tới đó" sẽ đẩy nó qua một loạt trạng thái nửa vời.

Ô hex của thanh ghi nào **chưa ghi** kể từ lần đổi gần nhất thì có nền xanh lá,
giống hệt phần mềm gốc — nhìn là biết còn thanh ghi nào đang nằm trên màn hình
mà chưa xuống tới kit. Dòng số dưới mỗi ô là giá trị kit trả về, **chữ đỏ** khi
nó khác giá trị vừa ghi xuống.

Ba nút gửi cả bộ:

| Nút | Làm gì |
|---|---|
| Ghi cả 8 thanh ghi (một gói) | một gói `CMD_ADF4159_REG8` |
| Ghi lần lượt 7, 6, 6, 5, 5, 4, 4, 3, 2, 1, 0 | mười một gói `CMD_ADF4159_REG` nối nhau |
| Về giá trị mặc định | chỉ đổi các ô trên màn hình, **không** gửi gì |

Dãy `7, 6, 6, 5, 5, 4, 4, 3, 2, 1, 0` là thứ tự của phần mềm gốc và là thứ tự
duy nhất đúng: `R0` vào cuối cùng. Hai số 6, hai số 5, hai số 4 là vì `R4`, `R5`,
`R6` mỗi cái có hai bộ giá trị — nhánh quét lên và nhánh thứ hai, phân biệt bằng
`CLK DIV SEL` (R4 DB6), `DEV SEL` (R5 DB23) và `STEP SEL` (R6 DB23). Ba thanh ghi
nhánh 2 nằm ở hàng dưới của thanh thanh ghi và chỉ gửi lẻ được, vì lệnh tám thanh
ghi không có chỗ cho chúng.

Nút **Bật/tắt quét tần** (tab *Quét tần và dịch khoá*) lật bit `RAMP ON` rồi ghi
ngay `R0`, không đụng bảy thanh ghi còn lại.

### Tham số và thanh ghi

Cửa sổ nhận **tham số** (tần số VCO, tần số chuẩn, số bước quét...) rồi tự tính
ra tám thanh ghi; `params.json` cũng lưu tham số chứ không lưu thanh ghi, vì
chiều ngược lại — tách một từ 32 bit ra thành "tần số VCO 6000 MHz" — không có
lời giải duy nhất.

Các công thức lấy thẳng từ tài liệu ADF4159 (`docs-local/ADF4159`):

```
fPFD  = REFIN × (1 + D) / (R × (1 + T))
RFout = (INT + FRAC/2^25) × fPFD
fDEV  = (fPFD / 2^25) × DEV × 2^DEV_OFFSET
Timer = CLK1 × CLK2 / fPFD
Delay = Delay_Word / fPFD  (× CLK1 nếu chọn nhịp PFD × CLK1)
```

Bộ giá trị mặc định đúng bằng bộ mà phần mềm gốc mở lên đã có sẵn, nên tám thanh
ghi tính ra phải khớp từng số với ảnh chụp phần mềm đó — `src/proc/adf4159.h`
kiểm điều này bằng `static_assert`, sai một bit là **hỏng biên dịch** chứ không
phải đợi tới lúc kit phát ra sai tần số.

Ba điều kiện tài liệu nói thẳng ra được kiểm lúc chạy và hiện chữ đỏ dưới nhóm
*Tham số RF*: `INT` nhỏ hơn mức bộ chia trước cho phép (23 với 4/5, 75 với 8/9),
trên 8 GHz mà còn để bộ chia trước 4/5, và `CLK1` với `CLK2` cùng bằng 1.

### Hai thứ có trong phần mềm gốc mà ở đây không có

**Pulse TXdata** và **Readback** — cả hai đều thao tác thẳng lên chân của bo
mạch qua bộ chuyển USB, mà đường lệnh ở đây là UDP: hai gói trên chỉ chở giá trị
thanh ghi, không có chỗ diễn đạt "nhấp một xung lên chân TXDATA" hay "đọc ngược
INT/FRAC ra". Cần tới chúng thì phải bổ sung giao thức trước.

### Kiểm tra cửa sổ ADF4159

```bash
cmake --build build --target ar0101-guidrv
```

```bash
cd build && ./ar0101-guidrv ../tests/scripts/adf4159.txt
```

Kịch bản mở cửa sổ, đặt một nhánh quét theo đúng ví dụ FMCW trong tài liệu
(`DEV = 20972`, `DEVoff = 4`, 200 bước, `CLK1 = 250`) rồi chụp lại ba ảnh vào
`/tmp`. Muốn thấy cả chiều trạng thái trả về thì chạy `tools/fake_control.py`
song song và bấm **Bắt đầu nhận dữ liệu** ở tab *Kết nối* trước.

## Ghi lưu và phát lại

Tab **Ghi lưu** làm hai việc: ghi dữ liệu xuống đĩa, và tái hiện lại một phiên đã
ghi. Cả hai đều chạy trên **luồng riêng** — đĩa là thứ hay khựng nhất trong cả
phần mềm, để chung luồng với việc vẽ hay việc vét socket là mất gói.

### Ghi lưu

Hai loại dữ liệu ghi vào **hai file khác nhau**, bật độc lập:

| Ô đánh dấu | Ghi cái gì | Mặc định | Tốc độ |
|---|---|---|---|
| Ghi dữ liệu gốc | nguyên datagram `RAW_V` và `RAW_P` | tắt | ~1.7 MB/s |
| Ghi dữ liệu đã xử lý | góc quét + nền tạp `Video[1024]` (0..255), `PlotTC`, `Track`, và các gói chưa giải mã (trạng thái hệ thống, trạng thái lệnh) | bật | ~0.4 MB/s |

Trong lúc ghi, tab hiện thời gian ghi (`hh:mm:ss`), tổng số bản ghi và số bản ghi
từng loại. Dòng "bỏ mất … bản ghi vì đĩa không theo kịp" chỉ xuất hiện khi hàng
đợi 64 MB bị tràn — thấy nó là đĩa quá chậm cho tốc độ đang ghi.

### Phát lại

Chọn loại dữ liệu, chọn phiên trong danh sách (hoặc *Mở tệp ghi lưu khác* để lấy
file chép từ máy khác sang), rồi bấm *Bắt đầu phát lại*.

| Loại phát lại | Chuyện gì xảy ra |
|---|---|
| Dữ liệu gốc | toàn bộ đường xử lý **chạy lại từ đầu** — đổi `DataSend`, `ZFbeat`, `Multi_V`, tham số chùm xung hay tham số quỹ đạo rồi xem lại chính phiên đó để so kết quả |
| Dữ liệu đã xử lý | bộ bám đứng yên, màn hình hiện **đúng cái đã ghi**; nhẹ hơn nhiều và file nhỏ hơn 4 lần. Gói trạng thái lệnh điều khiển đã ghi cũng được đưa lại vào tab *Điều khiển* |

Tốc độ tái hiện 1/4x … 8x chỉ áp cho dữ liệu đã xử lý; dữ liệu gốc luôn 1x nên
hàng chọn tốc độ tự ẩn. Nhịp phát lại bám theo **mốc thời gian ghi trong file**,
không theo tốc độ đọc đĩa. Máy không kịp xử lý ở tốc độ cao thì nhịp tự chậm lại
chứ không bỏ bản ghi — chậm nhưng đủ, hơn là nhanh nhưng thủng.

Bấm *Bắt đầu phát lại* sẽ **tắt** nhận dữ liệu, gửi dữ liệu và ghi lưu nếu đang
bật. Trong lúc phát lại:

- **Gửi dữ liệu** bật lại được. Với dữ liệu đã xử lý thì gói trong file được
  chuyển tiếp nguyên vẹn, không dựng lại.
- **Ghi lưu** bật lại được, nhưng chỉ khi đang phát lại *dữ liệu gốc* và chỉ tích
  ô *Ghi dữ liệu đã xử lý* — đây là cách chạy lại thuật toán với tham số mới rồi
  ghi kết quả ra một phiên mới.
- **Nhận dữ liệu** thì không: bấm vào sẽ ra thông báo.

### Tổ chức file ghi lưu

```
records/index.db                          ← danh mục phiên (SQLite)
records/2026/08/02/raw_20260802_143012.rec   ← dữ liệu gốc
records/2026/08/02/dat_20260802_143012.rec   ← dữ liệu đã qua xử lý
```

Thư mục `records` nằm **cạnh file chạy**, chia theo `yyyy/MM/dd`; tên file là
thời gian bắt đầu ghi. File tự ngắt sang file mới khi đạt **2 GB** — riêng dữ
liệu đã xử lý còn ngắt mỗi **2 tiếng**, tuỳ điều kiện nào đến trước.

Mỗi file gồm **64 byte header** (định danh `0x6969cafe`, phân loại, thời gian
bắt đầu/kết thúc, các bộ đếm) rồi tới phần Data: các bản ghi nối đuôi nhau, mỗi
bản ghi có 16 byte tiêu đề riêng mang loại, độ dài và mốc thời gian. Chi tiết
trong [src/record/recordfile.h](src/record/recordfile.h).

Phân loại nằm **trong** header chứ không suy từ tên file, nên đổi tên file cũng
không nhận nhầm loại. Header được ghi lại mỗi 2 giây, nên phiên bị mất điện giữa
chừng vẫn đọc được; trường hợp xấu nhất thì phần mềm tự đếm lại từ phần Data.

`index.db` chỉ là bản chép sẵn của các header để mở danh sách cho nhanh — đĩa
luôn là nguồn đúng. Xoá nó đi thì lần chạy sau dựng lại đầy đủ từ chính các file
`.rec`. Thiếu trình điều khiển `QSQLITE` thì việc ghi và phát lại vẫn chạy, chỉ
mất danh sách phiên (vẫn dùng được nút *Mở tệp ghi lưu*).

### Soi một file ghi lưu

```bash
python3 tools/dump_rec.py --scan build/records
```

Đọc header, duyệt phần Data rồi đối chiếu số bản ghi đếm được với số khai trong
header — dùng để kiểm tra phía ghi mà không phải mở giao diện. Thêm `--list N`
để in N bản ghi đầu tiên.

### Phát lại ra mạng cho máy khác

Chức năng *Phát lại* ở trên chạy ngay trong phần mềm. Muốn dựng lại một phiên
trên **máy khác** — máy không có file ghi lưu — thì đẩy file ra mạng dưới dạng
gói UDP, phía nhận không phân biệt được với dòng dữ liệu của đài thật:

```bash
python3 tools/playback_raw.py -source ./raw_20260803_103025.rec -addr 192.168.232.238 -udpports 8200 8300
```

`-udpports` nhận hai số: cổng `RAW_V` rồi cổng `RAW_P` của máy nhận. Địa chỉ
kết thúc bằng `.255` được nhận ra là địa chỉ quảng bá và tự bật `SO_BROADCAST`.
Nhịp gửi bám theo mốc thời gian ghi trong file; thêm `-speed X` để phát nhanh
hơn (`-speed 0` là nhanh nhất có thể) và `-loop` để phát lặp lại. Công cụ chỉ
nhận **file dữ liệu gốc**; đưa file dữ liệu đã xử lý vào thì nó báo lỗi và dừng.

## Nền bản đồ số

Có hai nguồn nền bản đồ, chọn bằng ComboBox trong tab **Cài đặt**:

| Mục trong ComboBox | Nguồn | Dữ liệu |
|---|---|---|
| `MT - …` | Tile ảnh tải sẵn từ MapTiler | `maps/mt/<kiểu-nền>/` |
| `TC` | Lớp vector tự dựng từ shapefile | `maps/tc/` |

Cả hai đều **không nằm trong repo** và đều không bắt buộc: thiếu dữ liệu thì
phần mềm vẫn chạy, chỉ để nền trống kèm dòng nhắc chỉ đúng thư mục còn thiếu.

### Lớp bản đồ TC

Đọc thẳng shapefile (`.shp` + `.dbf` + `.prj`) trong `maps/tc`, không cần thư
viện ngoài: bờ biển, biên giới, địa giới tỉnh, sông ngòi, đường bay dân dụng,
sân bay và tên địa danh. Toạ độ trong tệp có thể đang ở phép chiếu Lambert
Conformal Conic hoặc Transverse Mercator — phần mềm tự đọc `.prj` và quy về
kinh/vĩ độ.

Năm ô ngay dưới ComboBox cho ẩn/hiện từng lớp; lựa chọn được lưu vào `settings.json`.
Đường dẫn dữ liệu nằm ở trường `vectorDir`, quy tắc giống `tilesDir` bên dưới.

### Tile MapTiler

Máy mới phải tự tải về trước khi nền bản đồ hiện lên.

1. Lấy khoá miễn phí tại <https://cloud.maptiler.com/account/keys/>
2. Lưu vào `docs-local/maptiler.key` (thư mục này đã nằm trong `.gitignore`)
3. Tải tile — mỗi kiểu nền khoảng 70–180 MB:

```bash
python3 tools/download_tiles.py --style basic-v2-dark --min-zoom 11 --max-zoom 14 --radius 50
python3 tools/download_tiles.py --style basic-v2-dark --min-zoom 8  --max-zoom 10 --radius 160
```

Chạy lại không tải trùng, tile đã có sẵn sẽ bỏ qua. Xem `--help` để đổi tâm
hoặc bán kính.

### Kiểu nền bản đồ

Mỗi kiểu nền nằm trong một thư mục con riêng:

```
maps/mt/basic-v2-dark/tileset.json
maps/mt/dataviz-dark/tileset.json
...
```

Phần mềm **tự quét** các thư mục con này để đổ vào ComboBox chọn kiểu nền
(cạnh ô "Hiển thị nền bản đồ"), tên hiện ra có thêm tiền tố `MT - `. Tải thêm
một style là có thêm lựa chọn ngay, không phải sửa code:

```bash
python3 tools/download_tiles.py --style topo-v2-dark --min-zoom 11 --max-zoom 14 --radius 50
```

Tên hiển thị lấy từ trường `label` trong `tileset.json`; đổi bằng `--label`.

### Đổi đường dẫn thư mục bản đồ

Sửa trường `tilesDir` trong `settings.json` — đây là thư mục **gốc** chứa các kiểu nền:

| Giá trị | Ý nghĩa |
|---|---|
| `maps/mt` | Mặc định — tính từ thư mục chứa file chạy |
| `/media/usb/maps` hoặc `D:/ban-do` | Đường dẫn tuyệt đối, dùng nguyên |

Đường dẫn tương đối còn được dò ngược lên vài cấp thư mục cha, nhờ vậy lúc phát
triển (file chạy trong `build/`) vẫn thấy `maps/mt/tiles` ở gốc repo.

Biến môi trường (tên khai trong [src/app/appinfo.h](src/app/appinfo.h), hiện là
`MX01_TILES_DIR`) đè lên tất cả — tiện khi thử nhanh:

```bash
MX01_TILES_DIR=/duong/dan/khac ./ar0101
```

Bộ mang đi máy khác nên có bố cục:

```
ar0101                      ← file chạy
settings.json                   ← cấu hình hiển thị (tự sinh ở lần chạy đầu)
params.json                 ← tham số và danh sách cổng (tự sinh ở lần chạy đầu)
maps/mt/<kiểu-nền>/         ← tile bản đồ MapTiler, mỗi kiểu một thư mục
maps/tc/                    ← shapefile của lớp bản đồ TC
records/yyyy/MM/dd/         ← dữ liệu ghi lưu (tự sinh khi bấm Ghi lưu)
```

Dữ liệu bản đồ © MapTiler © OpenStreetMap contributors.

## Tách dự án mới từ bộ này

Bộ code này dùng làm gốc cho phần mềm khác có cùng bố cục được. Mọi cái tên đã
gom về **hai chỗ**, sửa xong là chạy — không phải đi tìm tên rải rác trong code:

| Sửa ở đâu | Sửa cái gì |
|---|---|
| [CMakeLists.txt](CMakeLists.txt), dòng `project(...)` | Tên file chạy. **Chữ không dấu**, vì là tên file thật trên đĩa. |
| [src/app/appinfo.h](src/app/appinfo.h) | Tên hiển thị, tên đơn vị, tên hai file cấu hình, biến môi trường. |

Tên hiển thị **viết tiếng Việt có dấu được** — nó chỉ ra tiêu đề cửa sổ và tiêu
đề hộp thoại. Hai thứ này độc lập nhau:

```
project(x123)                                     ← file chạy: x123 / x123.exe
appinfo::displayName() = "Ra đa tầm gần X123"     ← chữ trên thanh tiêu đề
appinfo::configFileName() = "x123.json"           ← cấu hình hiển thị, không dấu
appinfo::paramsFileName() = "x123-params.json"    ← tham số và cổng, không dấu
```

CI và `tools/download_tiles.py` tự đọc tên từ `project(...)` nên không phải sửa.

Việc còn lại sau khi đổi tên:

1. Sửa dòng tiêu đề và phần mô tả trong README này.
2. `git remote set-url origin <repo mới>` — nhớ làm ngay, kẻo push nhầm về repo gốc.
3. Chép `maps/` sang (không nằm trong repo): tile MapTiler tải lại bằng script,
   riêng `maps/tc` là dữ liệu riêng, phải chép tay.
4. Xoá `build/` cũ nếu có, rồi cấu hình lại từ đầu.

> **MSVC:** mã nguồn là UTF-8 không BOM, nên CMakeLists đã bật sẵn `/utf-8`.
> Bỏ cờ này thì chữ tiếng Việt trên giao diện sẽ thành ký tự lạ khi build bằng
> Visual Studio. GCC và Clang thì mặc định đã đúng.

## Tải bản dựng sẵn (CI)

GitHub Actions ([.github/workflows/ci.yml](.github/workflows/ci.yml)) build ở mỗi
push/PR vào `main` và đính kèm bản chạy cho cả ba nền tảng. Vào tab **Actions** →
chọn lần chạy mới nhất → mục **Artifacts** ở cuối trang.

| Artifact | Cần cài thêm gì |
|---|---|
| `ar0101-windows-latest` | **Không cần gì** — giải nén rồi chạy `ar0101.exe` |
| `ar0101-ubuntu-24.04` | Thư viện Qt 6 của hệ điều hành, xem bên dưới |
| `ar0101-macos-15` | Qt 6 qua Homebrew, xem bên dưới |

Cả ba đều chỉ có **file chạy**. Nền bản đồ (`maps/`) không nằm trong repo nên
phải chép sang riêng — thiếu thì phần mềm vẫn chạy, chỉ để nền trống.

### Windows

Bản Windows đã được `windeployqt` gói sẵn Qt DLL, plugin nền tảng và cả runtime
của MSVC. Giải nén cả thư mục rồi chạy `ar0101.exe` — **không** tách riêng file
`.exe` ra khỏi thư mục, nó cần các DLL nằm cạnh.

### Ubuntu

```bash
sudo apt install libqt6widgets6 libqt6network6 qt6-qpa-plugins libqt6sql6-sqlite
```

`qt6-qpa-plugins` là bắt buộc — thiếu nó phần mềm báo *"could not load the Qt
platform plugin xcb"* rồi thoát. `libqt6sql6-sqlite` thì không bắt buộc: thiếu
nó phần mềm vẫn chạy, chỉ mất danh sách phiên ghi lưu — xem
[Ghi lưu và phát lại](#ghi-lưu-và-phát-lại).

Bản trên CI được build bằng chính Qt trong kho apt của **Ubuntu 24.04**, nên chạy
được trên Ubuntu 24.04 trở đi. Máy dùng bản cũ hơn thì build lại từ mã
nguồn (xem mục [Build](#build)) — cần thêm `qt6-base-dev`.

```bash
chmod +x ar0101 && ./ar0101
```

### macOS

```bash
brew install qt
```

Rồi mở `ar0101.app`. Lần đầu macOS sẽ chặn vì bản dựng chưa ký; vào **System
Settings → Privacy & Security** bấm *Open Anyway*, hoặc gỡ cờ cách ly:

```bash
xattr -dr com.apple.quarantine ar0101.app
```
