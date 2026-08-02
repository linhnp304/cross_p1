cmake_minimum_required(VERSION 3.16)

# Tên dự án cũng là tên file chạy. Tách dự án mới thì đổi đúng dòng này và
# src/appinfo.h. Chỉ dùng chữ không dấu — đây là tên file thật trên đĩa; tên
# hiển thị (có dấu được) nằm trong src/appinfo.h.
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

qt_add_executable(${PROJECT_NAME}
    src/main.cpp
    src/appcolors.cpp
    src/appcolors.h
    src/appinfo.h
    src/appparams.cpp
    src/appparams.h
    src/appsettings.cpp
    src/appsettings.h
    src/ascope.cpp
    src/ascope.h
    src/beamcenter.cpp
    src/beamcenter.h
    src/beamparamsdialog.cpp
    src/beamparamsdialog.h
    src/colorstab.cpp
    src/colorstab.h
    src/connectiontab.cpp
    src/connectiontab.h
    src/geo.h
    src/lanstatus.cpp
    src/lanstatus.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/packetio.h
    src/paramstab.cpp
    src/paramstab.h
    src/player.cpp
    src/player.h
    src/plotlistwindow.cpp
    src/plotlistwindow.h
    src/plotstore.h
    src/plottrack.h
    src/prjprojection.cpp
    src/prjprojection.h
    src/procparams.h
    src/radarvideo.cpp
    src/radarvideo.h
    src/radarview.cpp
    src/radarview.h
    src/rawpacket.h
    src/recorder.cpp
    src/recorder.h
    src/recordfile.cpp
    src/recordfile.h
    src/recordindex.cpp
    src/recordindex.h
    src/recordtab.cpp
    src/recordtab.h
    src/settingstab.cpp
    src/settingstab.h
    src/shapefile.cpp
    src/shapefile.h
    src/theme.h
    src/tilecache.cpp
    src/tilecache.h
    src/trackinfopopup.cpp
    src/trackinfopopup.h
    src/tracklisttab.cpp
    src/tracklisttab.h
    src/trackparamsdialog.cpp
    src/trackparamsdialog.h
    src/tracker.cpp
    src/tracker.h
    src/udplink.cpp
    src/udplink.h
    src/udpsender.cpp
    src/udpsender.h
    src/vectormap.cpp
    src/vectormap.h
)

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
