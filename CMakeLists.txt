cmake_minimum_required(VERSION 3.21)
project(zcode-monitor LANGUAGES CXX)

# ── 构建环境完全来自环境变量（PATH / CMAKE_PREFIX_PATH），不硬编码任何路径 ──
# Qt 通过 PATH 中的 qmake / CMAKE_PREFIX_PATH 定位；编译器由 CMake 自动探测。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── full source list (core + selftest + UI) ──
# 应用图标（资源管理器 exe 图标）— 由 resources/app.rc.in 生成，指向程序化绘制的 ico
configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
# ico 内容变化时重新编译 rc（windres 不会自动感知依赖）
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/app.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/zcode-monitor.ico")

add_executable(zcode-monitor WIN32
    resources/icon.qrc
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    src/main.cpp
    src/selftest/SelfTest.h
    src/selftest/SelfTest.cpp
    src/util/Format.h
    src/util/Async.h
    src/util/CrashHandler.h
    src/util/CrashHandler.cpp
    src/util/Frost.h
    src/util/Frost.cpp
    src/util/Format.cpp
    src/core/Paths.h
    src/core/Paths.cpp
    src/core/Types.h
    src/core/DbService.h
    src/core/DbService.cpp
    src/core/RuntimeWatchdog.h
    src/core/RuntimeWatchdog.cpp
    src/core/LivePoller.h
    src/core/LivePoller.cpp
    src/core/TranscriptService.h
    src/core/TranscriptService.cpp
    src/core/LogTailService.h
    src/core/LogTailService.cpp
    src/core/RawService.h
    src/core/RawService.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/UiUtil.h
    src/ui/UiUtil.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/widgets/KpiCard.h
    src/ui/widgets/KpiCard.cpp
    src/ui/widgets/MiniChart.h
    src/ui/widgets/MiniChart.cpp
    src/ui/widgets/LiveFeed.h
    src/ui/widgets/LiveFeed.cpp
    src/ui/pages/OverviewPage.h
    src/ui/pages/OverviewPage.cpp
    src/ui/pages/SessionsPage.h
    src/ui/pages/SessionsPage.cpp
    src/ui/pages/ContextTab.h
    src/ui/pages/ContextTab.cpp
    src/ui/pages/TimelineTab.h
    src/ui/pages/TimelineTab.cpp
    src/ui/pages/AgentsPage.h
    src/ui/pages/AgentsPage.cpp
    src/ui/pages/ErrorsPage.h
    src/ui/pages/ErrorsPage.cpp
    src/ui/pages/RawPage.h
    src/ui/pages/RawPage.cpp
    src/ui/pages/HowPage.h
    src/ui/pages/HowPage.cpp
)

target_link_libraries(zcode-monitor PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    dwmapi
)

target_include_directories(zcode-monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
