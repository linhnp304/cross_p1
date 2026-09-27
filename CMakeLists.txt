cmake_minimum_required(VERSION 3.16)

# Tên dự án cũng là tên file chạy. Tách dự án mới thì đổi đúng dòng này và
# src/app/appinfo.h. Chỉ dùng chữ không dấu — đây là tên file thật trên đĩa; tên
# hiển thị (có dấu được) nằm trong src/app/appinfo.h.
project(ar0101 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Mã nguồn viết bằng UTF-8 và có chuỗi tiếng Việt. MSVC mặc định đọc theo bảng
# mã của Windows nên chữ có dấu sẽ thành ký tự lạ; /utf-8 bắt nó đọc cho đúng.
# GCC và Clang mặc định đã là UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Sql chỉ dùng cho danh mục phiên ghi lưu (SQLite). Thiếu trình điều khiển
# QSQLITE lúc chạy thì phần mềm vẫn ghi và phát lại được bằng file .rec, chỉ mất
# danh sách phiên — xem RecordIndex::open().
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql)

qt_standard_project_setup()

# Mã nguồn chia theo chủ đề, mỗi thư mục một tầng của phần mềm:
#
#   app     khởi tạo, cửa sổ chính, hai file cấu hình
#   net     giao thức gói tin, luồng nhận UDP, phía gửi
#   proc    tách tâm chùm xung, bám quỹ đạo, thanh ghi ADF4159, đọc hệ số bộ
#           lọc — thuần thuật toán, không đụng Qt Widgets
#   record  ghi lưu và phát lại
#   maps    nền bản đồ số, chiếu toạ độ
#   ui      các tab, cửa sổ con và phần vẽ
#
# Include viết đủ đường dẫn (`#include "net/udplink.h"`) chứ không chỉ tên file,
# nhờ target_include_directories dưới đây — nhìn một dòng include là biết thứ
# đang dùng thuộc tầng nào.
#
# Danh sách này để riêng ra khỏi qt_add_executable vì công cụ điều khiển giao
# diện (tests/guidrv.cpp) dùng lại đúng chừng này file, chỉ thay src/main.cpp.
set(AR_SOURCES
    # --- app: khởi tạo, cửa sổ chính, hai file cấu hình ---
    src/app/appcolors.cpp
    src/app/appcolors.h
    src/app/appinfo.h
    src/app/appparams.cpp
    src/app/appparams.h
    src/app/appsettings.cpp
    src/app/appsettings.h
    src/app/mainwindow.cpp
    src/app/mainwindow.h

    # --- net: giao thức gói tin, nhận và gửi UDP ---
    src/net/adfproto.h
    src/net/cmdproto.h
    src/net/filterproto.h
    src/net/netaddr.h
    src/net/packetio.h
    src/net/rawpacket.h
    src/net/syncproto.h
    src/net/udplink.cpp
    src/net/udplink.h
    src/net/udpsender.cpp
    src/net/udpsender.h

    # --- proc: tách tâm chùm xung, bám quỹ đạo, thanh ghi ADF4159, hệ số bộ lọc ---
    src/proc/adf4159.cpp
    src/proc/adf4159.h
    src/proc/beamcenter.cpp
    src/proc/beamcenter.h
    src/proc/filterdata.cpp
    src/proc/filterdata.h
    src/proc/plotstore.h
    src/proc/plottrack.h
    src/proc/procparams.h
    src/proc/tracker.cpp
    src/proc/tracker.h

    # --- record: ghi lưu và phát lại ---
    src/record/player.cpp
    src/record/player.h
    src/record/recorder.cpp
    src/record/recorder.h
    src/record/recordfile.cpp
    src/record/recordfile.h
    src/record/recordindex.cpp
    src/record/recordindex.h

    # --- maps: nền bản đồ số, chiếu toạ độ ---
    src/maps/geo.h
    src/maps/prjprojection.cpp
    src/maps/prjprojection.h
    src/maps/shapefile.cpp
    src/maps/shapefile.h
    src/maps/tilecache.cpp
    src/maps/tilecache.h
    src/maps/vectormap.cpp
    src/maps/vectormap.h

    # --- ui: các tab, cửa sổ và phần vẽ ---
    src/ui/adf4159window.cpp
    src/ui/adf4159window.h
    src/ui/ascope.cpp
    src/ui/ascope.h
    src/ui/beamparamsdialog.cpp
    src/ui/beamparamsdialog.h
    src/ui/colorstab.cpp
    src/ui/colorstab.h
    src/ui/compacttabs.cpp
    src/ui/compacttabs.h
    src/ui/connectiontab.cpp
    src/ui/connectiontab.h
    src/ui/controltab.cpp
    src/ui/controltab.h
    src/ui/filterwindow.cpp
    src/ui/filterwindow.h
    src/ui/lanstatus.cpp
    src/ui/lanstatus.h
    src/ui/paramstab.cpp
    src/ui/paramstab.h
    src/ui/plotlistwindow.cpp
    src/ui/plotlistwindow.h
    src/ui/radarvideo.cpp
    src/ui/radarvideo.h
    src/ui/radarview.cpp
    src/ui/radarview.h
    src/ui/recordtab.cpp
    src/ui/recordtab.h
    src/ui/settingstab.cpp
    src/ui/settingstab.h
    src/ui/theme.h
    src/ui/trackinfopopup.cpp
    src/ui/trackinfopopup.h
    src/ui/tracklisttab.cpp
    src/ui/tracklisttab.h
    src/ui/trackparamsdialog.cpp
    src/ui/trackparamsdialog.h
)

qt_add_executable(${PROJECT_NAME} src/main.cpp ${AR_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- công cụ điều khiển giao diện bằng kịch bản ------------------------------
#
# EXCLUDE_FROM_ALL: dựng bình thường không đụng tới nó, phải gọi đích tên mới
# dựng — `cmake --build build --target ${PROJECT_NAME}-guidrv`. Nhờ vậy máy dựng
# tự động và người chỉ muốn chạy phần mềm không phải cài thêm Qt6::Test.
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Test)
if(Qt${QT_VERSION_MAJOR}Test_FOUND)
    qt_add_executable(${PROJECT_NAME}-guidrv EXCLUDE_FROM_ALL
        tests/guidrv.cpp ${AR_SOURCES})
    target_include_directories(${PROJECT_NAME}-guidrv PRIVATE src)
    target_link_libraries(${PROJECT_NAME}-guidrv
        PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Test)
    # Cùng thư mục với file chạy chính: phần mềm tìm maps/ và các file cấu hình
    # theo vị trí file chạy, để chỗ khác là nó không thấy bản đồ.
    set_target_properties(${PROJECT_NAME}-guidrv PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
