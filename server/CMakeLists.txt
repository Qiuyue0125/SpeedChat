cmake_minimum_required(VERSION 3.16)

project(server LANGUAGES CXX)

# ===== 指定 Qt 路径（命令行传入 -DCMAKE_PREFIX_PATH=... 或修改此处） =====
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(CMAKE_PREFIX_PATH "D:/QT/6.7.3/mingw_64")
endif()

# ===== C++ 标准 =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Qt 自动 MOC/RCC =====
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ===== 输出目录（根据构建模式自动选择子目录） =====
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

# ===== 编译器选项 =====
if(MSVC)
    add_compile_options(/utf-8)
    add_custom_command(TARGET suliao POST_BUILD
        COMMAND  -E copy_if_different
    )
endif()

# ===== 查找 Qt（优先 Qt6，回退 Qt5） =====
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Sql)

message(STATUS "Using Qt${QT_VERSION_MAJOR}: ${Qt${QT_VERSION_MAJOR}_VERSION}")

# ===== 收集源代码（源码在 server/ 子目录下） =====
file(GLOB_RECURSE SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB_RECURSE HEADERS
    CONFIGURE_DEPENDS
    src/*.h
)

file(GLOB_RECURSE RESOURCES
    CONFIGURE_DEPENDS
    src/*.qrc
)

# ===== 可执行目标 =====
add_executable(server
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ===== 包含路径 =====
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/depend
)

# ===== 链接 Qt 库 =====
target_link_libraries(server PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
)

# ===== 运行时 Qt 库定位 =====
if(WIN32)
    # Windows: 构建后用 windeployqt 将 DLL 复制到输出目录
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin"
        REQUIRED
    )
    message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

    add_custom_command(TARGET server POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-virtualkeyboard
            "$<TARGET_FILE:server>"
        COMMENT "Deploying Qt dependencies via windeployqt..."
    )
endif()

# ===== 安装规则 =====
install(TARGETS server
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)