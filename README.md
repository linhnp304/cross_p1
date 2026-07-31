# ar0101 — AR01.01

Màn hình trắc thủ ra đa, Qt Widgets đa nền tảng (Ubuntu, Windows, macOS) — một codebase, build & chạy trên cả ba hệ điều hành.

## Yêu cầu

- CMake >= 3.16
- Qt 6 (Widgets, Network)
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
| `mx01.json` | Cấu hình hiển thị: nền bản đồ, tâm đài, cự ly tối đa, vòng cự ly, tốc độ mờ video | Trắc thủ |
| `params.json` | Tham số kỹ thuật (Fs, B, Tc, ZFbeat) và danh sách cổng UDP | Người lắp đặt |

Cả bộ (file chạy + hai file cấu hình + bản đồ) mang sang máy khác là chạy được ngay.

> Lúc phát triển, file chạy nằm trong `build/` nên cấu hình cũng ở đó và sẽ mất
> khi xoá thư mục `build`. Không sao — thiếu file thì phần mềm dùng giá trị mặc định.

## Nhận dữ liệu ra đa

Tab **Kết nối** liệt kê các cổng UDP nhận dữ liệu. Mặc định có sẵn ba dòng
`UDP-RAW_V` (6001), `UDP-RAW_P` (6002), `UDP-STATUS` (6003) trên `127.0.0.1`.

- `RemoteIP` để trống hoặc `0.0.0.0`, `RemotePort` để `0` → nhận từ **mọi máy /
  mọi cổng**. Đây là mặc định, vì bên gửi thường dùng cổng nguồn ngẫu nhiên.
- Cột **Tên** chỉ là nhãn cho người đọc. Gói tin được phân loại theo header của
  chính nó, nên đổi tên hay gộp cổng cũng không làm hỏng việc giải mã.
- Bảng chỉ sửa được lúc đã dừng kết nối.

Dữ liệu `RAW_V` (1024 điểm biên độ mỗi gói, ~400 gói/giây) hiện ở hai nơi: nền
tạp trên bản đồ (panel 1) và đường biên độ trên cửa sổ biên độ (panel 2.2).

Việc đọc socket và giải mã chạy trên một luồng riêng, đẩy vào hai bộ đệm tách
biệt — một cho hiển thị, một dành sẵn cho chức năng ghi lưu ở giai đoạn sau.

### Tab Tham số

Cự ly tối đa suy ra từ tham số theo công thức rút gọn **Rmax = 75·Fs·Tc/B** (mét).
Với giá trị mặc định Fs=1, B=154, Tc=2500 thì Rmax = 1217.53 m → **1.218 km**.

- Bật **Tự động cập nhật thang cự ly** rồi bấm *Áp dụng* → ô "Cự ly tối đa" bên
  tab Cài đặt và các vòng cự ly tự đổi theo.
- Bật **Tự động nhận từ trạng thái lệnh điều khiển** → nút *Áp dụng* bị khoá
  (mọi thay đổi vào thẳng) và ô tự cập nhật thang cự ly bị bật cố định.

### Công cụ tạo giả dữ liệu

Không có đài thật vẫn thử được toàn bộ đường nhận và hiển thị:

```bash
python3 tools/fake_raw_v.py
```

Gửi `RAW_V` tới `127.0.0.1:6001`, nhịp 2.5 ms, phương vị chạy đúng 6 vòng/phút,
nền tạp ngẫu nhiên 10000–15000. Thêm `--targets 6` để có vài mục tiêu giả cho
dễ nhìn ra thang cự ly. Xem `--help` để đổi cổng hoặc tốc độ vòng quét.

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
| `ar0101-ubuntu-latest` | Thư viện Qt 6 của hệ điều hành, xem bên dưới |
| `ar0101-macos-15` | Qt 6 qua Homebrew, xem bên dưới |

Cả ba đều chỉ có **file chạy**. Nền bản đồ (`maps/`) không nằm trong repo nên
phải chép sang riêng — thiếu thì phần mềm vẫn chạy, chỉ để nền trống.

### Windows

Bản Windows đã được `windeployqt` gói sẵn Qt DLL, plugin nền tảng và cả runtime
của MSVC. Giải nén cả thư mục rồi chạy `ar0101.exe` — **không** tách riêng file
`.exe` ra khỏi thư mục, nó cần các DLL nằm cạnh.

### Ubuntu

```bash
sudo apt install libqt6widgets6 libqt6network6 qt6-qpa-plugins
```

`qt6-qpa-plugins` là bắt buộc — thiếu nó phần mềm báo *"could not load the Qt
platform plugin xcb"* rồi thoát.

Bản trên CI được build bằng chính Qt trong kho apt của `ubuntu-latest`, nên chạy
được trên bản Ubuntu đó trở đi. Máy dùng bản Ubuntu cũ hơn thì build lại từ mã
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
