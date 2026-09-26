#!/usr/bin/env bash
# Tạo biểu tượng cho phần mềm, đặt shortcut trên Desktop và đưa vào "Show Apps"
# (GNOME Shell). Chạy sau khi đã build (cần có file chạy trong build/).
#
# Tên file chạy / tên hiển thị lấy từ project(...) trong CMakeLists.txt và
# appinfo::displayName() trong src/app/appinfo.h — cùng quy ước với
# tools/download_tiles.py — để tách dự án mới (đổi tên) không phải sửa gì ở
# đây. .ico dùng được cho bản dựng Windows sau này; icon theme của Linux không
# nhận .ico nên phải tách thêm bản .png để cài vào theme.
#
# Chạy lại bao nhiêu lần cũng an toàn, mọi bước đều ghi đè.
#
# Cách dùng: tools/make_app_icon.sh [đường-dẫn-ảnh-khác]
#
# --- Copy phần mềm sang máy khác: chỗ nào tự chạy được, chỗ nào phải sửa -----
#
# Tự thích nghi, KHÔNG cần sửa gì (script tự suy ra từ vị trí file này và từ
# CMakeLists.txt / appinfo.h):
#   - Đường dẫn gốc dự án (ROOT_DIR) — suy từ vị trí script, chép cả thư mục
#     dự án đi đâu cũng chạy được, miễn cấu trúc tools/ + src/ + CMakeLists.txt
#     còn nguyên.
#   - Tên file chạy, tên hiển thị (APP_ID, DISPLAY_NAME).
#   - Nơi cài icon/.desktop trên máy (~/.local/share/..., ~/Desktop) — tự dò
#     theo $HOME của người đang chạy script.
#
# CẦN kiểm tra lại, có thể phải sửa tay:
#   1. BUILD_DIR_CANDIDATES ở dưới — danh sách thư mục build hay dùng, script
#      tự dò file chạy trong từng thư mục theo thứ tự này. Build ở chỗ khác
#      (vd Qt Creator đặt kit tên riêng) thì thêm đường dẫn đó vào danh sách,
#      hoặc gọn hơn là export BIN_DIR=/đường/dẫn/build trước khi chạy script.
#   2. SRC_IMAGE mặc định (docs-local/images/rd1.jpeg) — docs-local/ nằm trong
#      .gitignore nên máy mới chép qua git sẽ KHÔNG có ảnh này. Hai cách xử lý:
#      chép ảnh mẫu qua thủ công (giữ đúng đường dẫn cũ), hoặc gọi script kèm
#      ảnh khác làm tham số: `tools/make_app_icon.sh /đường/dẫn/logo.png`.
#      Icon sinh ra (tools/icons/) cũng không lên repo — chạy lại script ở máy
#      mới là có lại y hệt, không cần chép icon đi theo.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC_IMAGE="${1:-$ROOT_DIR/docs-local/images/rd1.jpeg}"

APP_ID="$(grep -oP '^project\(\K[A-Za-z0-9_-]+' "$ROOT_DIR/CMakeLists.txt" || true)"
APP_ID="${APP_ID:-qt-app}"

DISPLAY_NAME="$(grep -oP 'displayName\(\)\s*\{\s*return\s*QStringLiteral\("\K[^"]+' \
    "$ROOT_DIR/src/app/appinfo.h" || true)"
DISPLAY_NAME="${DISPLAY_NAME:-$APP_ID}"

# Thư mục build hay gặp — CMake dòng lệnh mặc định ra build/, Qt Creator theo
# kit thì thường thêm một cấp tên kit bên dưới. Đặt BIN_DIR trước khi chạy
# script (export BIN_DIR=...) để bỏ qua bước dò này, dùng đúng đường dẫn chỉ định.
BUILD_DIR_CANDIDATES=(
    "$ROOT_DIR/build"
    "$ROOT_DIR/build/Desktop-Debug"
    "$ROOT_DIR/build/Desktop-Release"
)
if [ -z "${BIN_DIR:-}" ]; then
    for d in "${BUILD_DIR_CANDIDATES[@]}"; do
        if [ -x "$d/$APP_ID" ]; then
            BIN_DIR="$d"
            break
        fi
    done
fi
BIN_DIR="${BIN_DIR:-$ROOT_DIR/build}"
BIN_PATH="$BIN_DIR/$APP_ID"

ICON_DIR="$ROOT_DIR/tools/icons"
ICO_FILE="$ICON_DIR/$APP_ID.ico"
PNG_FILE="$ICON_DIR/$APP_ID.png"

if [ ! -f "$SRC_IMAGE" ]; then
    echo "Không thấy ảnh: $SRC_IMAGE" >&2
    exit 1
fi

if ! command -v python3 >/dev/null; then
    echo "Cần python3 để dựng icon (dùng thư viện Pillow)." >&2
    exit 1
fi
if ! python3 -c "import PIL" >/dev/null 2>&1; then
    echo "Thiếu thư viện Pillow — cài bằng: pip install Pillow" >&2
    exit 1
fi

mkdir -p "$ICON_DIR"

# --- Dựng .ico (nhiều cỡ) và .png (256x256, để cài vào icon theme) -----------
# Ảnh mẫu là JPEG (không có kênh alpha thật) với phần nền vẽ sẵn ô caro để giả
# lập trong suốt — phải tự tách nền đó ra. Cách làm: loang màu (flood fill) từ
# góc ảnh với ngưỡng đủ rộng để nối liền hai màu ô caro sáng, phần còn lại
# (vòng tròn ra đa) trở thành kênh alpha. Ảnh khác không có kiểu nền caro này
# thì bước loang màu coi như không đổi gì (nền đặc một màu vẫn loang hết).
python3 - "$SRC_IMAGE" "$ICO_FILE" "$PNG_FILE" <<'PYEOF'
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

src, ico_path, png_path = sys.argv[1:4]
im = Image.open(src).convert("RGB")

SENTINEL = (1, 2, 3)
flood = im.copy()
ImageDraw.floodfill(flood, (0, 0), SENTINEL, thresh=70)
fg_mask = ~np.all(np.array(flood) == np.array(SENTINEL), axis=-1)

alpha = Image.fromarray((fg_mask * 255).astype(np.uint8), mode="L")
alpha = alpha.filter(ImageFilter.GaussianBlur(1))

rgba = im.convert("RGBA")
rgba.putalpha(alpha)

# Cắt sát viền vật thể (không phải cả khung ảnh) theo hộp bao của kênh alpha,
# chừa thêm mép nhỏ rồi mới vuông hoá — để icon không có viền trong suốt thừa.
ys, xs = np.where(fg_mask)
x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
w, h = im.size
side = max(x1 - x0, y1 - y0) // 2 + 6
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
left, top = max(cx - side, 0), max(cy - side, 0)
right, bottom = min(cx + side, w), min(cy + side, h)
rgba = rgba.crop((left, top, right, bottom))

sizes = [16, 32, 48, 64, 128, 256]
rgba.save(ico_path, sizes=[(s, s) for s in sizes])
rgba.resize((256, 256), Image.LANCZOS).save(png_path)
PYEOF

echo "Đã dựng: $ICO_FILE"
echo "Đã dựng: $PNG_FILE"

# --- Cài .png vào icon theme của người dùng, để "Show Apps" tra ra bằng tên --
if command -v xdg-icon-resource >/dev/null; then
    xdg-icon-resource install --novendor --noupdate --size 256 "$PNG_FILE" "$APP_ID"
    xdg-icon-resource forceupdate
else
    THEME_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
    mkdir -p "$THEME_DIR"
    cp -f "$PNG_FILE" "$THEME_DIR/$APP_ID.png"
fi

if [ ! -x "$BIN_PATH" ]; then
    echo "Cảnh báo: chưa thấy file chạy $BIN_PATH — build dự án rồi chạy lại script để shortcut bấm chạy được." >&2
fi

# --- File .desktop dùng chung cho menu ứng dụng và shortcut trên Desktop -----
# desktop-file-install lấy tên file cài vào theo đúng tên file nguồn, nên phải
# đặt tên $APP_ID.desktop cho file tạm — không phải tên ngẫu nhiên của mktemp.
TMP_DIR="$(mktemp -d)"
TMP_DESKTOP="$TMP_DIR/$APP_ID.desktop"
cat > "$TMP_DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=$DISPLAY_NAME
GenericName=Phần mềm xử lý dữ liệu ra đa
Comment=$DISPLAY_NAME — phần mềm xử lý dữ liệu ra đa
Exec=$BIN_PATH
Path=$BIN_DIR
Icon=$APP_ID
Terminal=false
Categories=Science;Engineering;
StartupWMClass=$DISPLAY_NAME
StartupNotify=true
EOF

# Menu ứng dụng — đây là nơi GNOME Shell "Show Apps" quét ra icon.
if command -v desktop-file-install >/dev/null; then
    desktop-file-install --dir="$HOME/.local/share/applications" --rebuild-mime-info-cache "$TMP_DESKTOP"
else
    mkdir -p "$HOME/.local/share/applications"
    cp -f "$TMP_DESKTOP" "$HOME/.local/share/applications/$APP_ID.desktop"
fi
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
command -v xdg-desktop-menu >/dev/null && xdg-desktop-menu forceupdate || true

# Bản sao trên Desktop — cần bit thực thi + đánh dấu "tin cậy" cho Nautilus,
# nếu không nó chỉ hiện icon file text và bắt người dùng bấm "Allow Launching".
DESKTOP_DIR="$(command -v xdg-user-dir >/dev/null && xdg-user-dir DESKTOP || echo "$HOME/Desktop")"
mkdir -p "$DESKTOP_DIR"
cp -f "$TMP_DESKTOP" "$DESKTOP_DIR/$APP_ID.desktop"
chmod +x "$DESKTOP_DIR/$APP_ID.desktop"
command -v gio >/dev/null && gio set "$DESKTOP_DIR/$APP_ID.desktop" metadata::trusted true 2>/dev/null || true

rm -rf "$TMP_DIR"

echo "Đã cài vào menu ứng dụng: $HOME/.local/share/applications/$APP_ID.desktop"
echo "Đã tạo shortcut trên Desktop: $DESKTOP_DIR/$APP_ID.desktop"
echo "Mở 'Show Apps', tìm '$DISPLAY_NAME'. Chưa thấy ngay thì đăng xuất/đăng nhập lại — GNOME Shell cache icon theo phiên."
