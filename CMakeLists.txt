cmake_minimum_required(VERSION 3.16)

# Tên dự án cũng là tên file chạy. Tách dự án mới thì đổi đúng dòng này và
# src/appinfo.h. Chỉ dùng chữ không dấu — đây là tên file thật trên đĩa; tên
# hiển thị (có dấu được) nằm trong src/appinfo.h.
project(cross_p1 VERSION 0.1.0 LANGUAGES CXX)

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

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(${PROJECT_NAME}
    src/main.cpp
    src/appinfo.h
    src/appparams.cpp
    src/appparams.h
    src/appsettings.cpp
    src/appsettings.h
    src/ascope.cpp
    src/ascope.h
    src/connectiontab.cpp
    src/connectiontab.h
    src/geo.h
    src/lanstatus.cpp
    src/lanstatus.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/paramstab.cpp
    src/paramstab.h
    src/prjprojection.cpp
    src/prjprojection.h
    src/radarvideo.cpp
    src/radarvideo.h
    src/radarview.cpp
    src/radarview.h
    src/rawpacket.h
    src/settingstab.cpp
    src/settingstab.h
    src/shapefile.cpp
    src/shapefile.h
    src/theme.h
    src/tilecache.cpp
    src/tilecache.h
    src/udplink.cpp
    src/udplink.h
    src/vectormap.cpp
    src/vectormap.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Network)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
