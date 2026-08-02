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

## File cấu hình

Hai file, đều nằm **ngay cạnh file chạy** và đều sinh ra ở lần chạy đầu:

| File | Chứa gì | Của ai |
|---|---|---|
| `mx01.json` | Cấu hình hiển thị: nền bản đồ, tâm đài, cự ly tối đa, vòng cự ly, tốc độ mờ video, màu các đối tượng đồ hoạ, danh sách phân loại mục tiêu | Trắc thủ |
| `params.json` | Tham số kỹ thuật (Fs, B, Tc, ZFbeat), rẻ quạt xử lý, tham số hai thuật toán xử lý, và danh sách cổng UDP | Người lắp đặt |

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
`sudo chown -R $USER params.json mx01.json records`.

**4. Đối chiếu tham số** Fs, B, Tc, ZFbeat bên tab Tham số cho khớp đài — ZFbeat
sai thang thì dữ liệu về đủ nhưng nền tạp vẫn đen kịt.

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
mặc định có sẵn ba dòng `UDP-RAW_V` (6001), `UDP-RAW_P` (6002), `UDP-STATUS`
(6003) trên `127.0.0.1`.

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

Dữ liệu `RAW_V` (1024 điểm biên độ mỗi gói, ~400 gói/giây) hiện ở hai nơi: nền
tạp trên bản đồ (panel 1) và đường biên độ trên cửa sổ biên độ (panel 2.2).

Đọc số tại một điểm cụ thể:

- **Trên bản đồ** — thanh trạng thái hiện kinh/vĩ độ con trỏ, kèm **phương vị và
  cự ly** tính từ tâm đài, làm tròn tới 0.001° và 1 m.
- **Trên cửa sổ biên độ** — rê chuột vào là hiện biên độ và cự ly của ô đang
  trỏ, kèm vạch chỉ vị trí. Cự ly theo đúng công thức của cự ly tối đa, thay số
  ô vào chỗ 1024, rút gọn còn **R = Rmax·n/1024**; ô cuối cùng vì thế đúng bằng
  cự ly tối đa. Panel hẹp thì hai nhãn `0` và cự ly tối đa tự ẩn để nhường chỗ.

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

Hai nút ở cuối tab mở hai cửa sổ tham số của phần xử lý — xem
[Điểm dấu và quỹ đạo](#điểm-dấu-và-quỹ-đạo).

**Xử lý theo rẻ quạt** giới hạn phần dữ liệu `RAW_P` được đưa vào xử lý. Góc
tính theo chiều kim đồng hồ từ góc bắt đầu tới góc kết thúc, nên *bắt đầu > kết
thúc* là rẻ quạt vắt qua hướng bắc chứ không phải nhập ngược. Có hiệu lực ngay,
không cần bấm *Áp dụng* (nút đó thuộc về nhóm thang cự ly).

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
   quanh tâm đài. Mỗi vòng quay ăng-ten là một nhịp: điểm dấu tới thì ghép vào
   quỹ đạo đang có, hết vòng thì quỹ đạo nào không được ghép sẽ ngoại suy, ngoại
   suy quá số vòng cho phép thì xoá.

Hình vẽ trên panel 1:

| Đối tượng | Hình | Ghi chú |
| --- | --- | --- |
| Điểm dấu | hình vuông nhỏ | luôn nằm **trên** lớp quỹ đạo; tự xoá sau 8 giây |
| Quỹ đạo | tam giác cân, quay theo hướng chuyển động | để rỗng ruột khi đang ngoại suy |
| Vết lịch sử | hình tròn nhỏ, mỗi vòng quét một vết | xanh biển = đang bám, đỏ = ngoại suy, cam = còn lại |

Hai ô **Hiện thông tin** trong tab Cài đặt bật thêm chữ cạnh hình. Ba nhãn nằm
ba phía khác nhau vì điểm dấu và quỹ đạo của cùng một mục tiêu gần như trùng vị
trí — cùng một phía là chữ đè lên chữ:

- **Quỹ đạo** — số đầu tốp phía trên, phương vị-cự ly bên phải (chỉ số, làm tròn
  0.01° và 0.1 m).
- **Điểm dấu** — phương vị-cự ly bên trái.

Màu của mọi đối tượng đổi được trong tab **Màu sắc**; ẩn/hiện và độ dài vết
chỉnh trong tab **Cài đặt**, nhóm *Điểm dấu và quỹ đạo*.

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
| Xoá toàn bộ điểm dấu | xoá lớp điểm dấu đang vẽ trên bản đồ. Không đụng tới cửa sổ *Thông tin chi tiết điểm dấu* — cửa sổ đó là dòng chảy riêng và đã có nút xoá của nó |
| Xoá toàn bộ quỹ đạo | xoá mọi quỹ đạo, kèm cả đầu tốp / độ cao / phân loại đã nhập tay. Có hỏi lại trước, vì thuật toán không dựng lại được phần nhập tay đó. Mỗi quỹ đạo vẫn đi qua đúng đường xoá bằng tay nên hệ thống nhận đều nhận được `track_status = 6` |

Danh sách tên phân loại mục tiêu nằm trong `mx01.json`, khoá `classifyNames` —
sửa thẳng trong file, chưa có giao diện quản lý.

### Tham số hai thuật toán

Hai nút ở cuối tab **Tham số** mở hai cửa sổ chỉnh được lúc đang chạy; giá trị
lưu vào `params.json`.

- **Tham số chùm xung** — tiêu chuẩn độ dài chùm, dopler, xét duyệt xung vào
  chùm, số chu kỳ mở/đóng chùm, phương án tính tâm (đơn giản hay có trọng số
  biên độ), và số giây giữ điểm dấu trên màn hình.
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
mà bay qua vùng chết quanh tâm đài thì vẫn được cập nhật bình thường.

## Gửi dữ liệu đi hệ thống khác

Mỗi khi có điểm dấu mới hoặc một quỹ đạo được cập nhật, gói tin tương ứng được
gửi tới mọi dòng trong bảng **Cổng UDP gửi dữ liệu** (tab Kết nối) đang bật ô
*Gửi* và đúng loại dữ liệu. Bảng mặc định rỗng.

| Cột | Ý nghĩa |
|---|---|
| Gửi | Bật/tắt dòng đó. **Luôn bắt đầu ở trạng thái tắt mỗi lần chạy** — mở phần mềm lên mà tự phát gói ra mạng là chuyện không ai muốn |
| Loại dữ liệu | `Plot` (điểm dấu tâm chùm) hoặc `Track` (quỹ đạo) |
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

Quỹ đạo bị xoá được gửi kèm `track_status = 6`, dù xoá bằng tay hay bằng thuật
toán — hệ thống nhận không biết thì nó giữ quỹ đạo đó trên màn hình vĩnh viễn.

### Kiểm tra phía gửi

```bash
python3 tools/recv_plot_track.py
```

Đóng vai hệ thống nhận: mở cổng 6101 và 6102, giải mã theo đúng bảng mô tả giao
thức rồi in ra, kèm kiểm tra Header và Length. Thêm `--raw` để in đủ từng
trường, `--port` để đổi cổng. Nghe trên `0.0.0.0` nên nhận được cả gói quảng bá.

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
| Dữ liệu gốc | toàn bộ đường xử lý **chạy lại từ đầu** — đổi `ZFbeat`, tham số chùm xung hay tham số quỹ đạo rồi xem lại chính phiên đó để so kết quả |
| Dữ liệu đã xử lý | bộ bám đứng yên, màn hình hiện **đúng cái đã ghi**; nhẹ hơn nhiều và file nhỏ hơn 4 lần |

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
trong [src/recordfile.h](src/recordfile.h).

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

Năm ô ngay dưới ComboBox cho ẩn/hiện từng lớp; lựa chọn được lưu vào `mx01.json`.
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

Sửa trường `tilesDir` trong `mx01.json` — đây là thư mục **gốc** chứa các kiểu nền:

| Giá trị | Ý nghĩa |
|---|---|
| `maps/mt` | Mặc định — tính từ thư mục chứa file chạy |
| `/media/usb/maps` hoặc `D:/ban-do` | Đường dẫn tuyệt đối, dùng nguyên |

Đường dẫn tương đối còn được dò ngược lên vài cấp thư mục cha, nhờ vậy lúc phát
triển (file chạy trong `build/`) vẫn thấy `maps/mt/tiles` ở gốc repo.

Biến môi trường (tên khai trong [src/appinfo.h](src/appinfo.h), hiện là
`MX01_TILES_DIR`) đè lên tất cả — tiện khi thử nhanh:

```bash
MX01_TILES_DIR=/duong/dan/khac ./ar0101
```

Bộ mang đi máy khác nên có bố cục:

```
ar0101                      ← file chạy
mx01.json                   ← cấu hình hiển thị (tự sinh ở lần chạy đầu)
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
| [src/appinfo.h](src/appinfo.h) | Tên hiển thị, tên đơn vị, tên hai file cấu hình, biến môi trường. |

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
