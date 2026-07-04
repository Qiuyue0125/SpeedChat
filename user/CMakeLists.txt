cmake_minimum_required(VERSION 3.16)

project(suliao LANGUAGES CXX)

# ===== 指定 Qt 路径（命令行传入 -DCMAKE_PREFIX_PATH=... 或修改此处） =====
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(CMAKE_PREFIX_PATH "D:/QT/6.7.3/mingw_64")
endif()

# ===== C++ 标准 =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Qt 自动 MOC/UIC/RCC =====
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ===== 输出目录（根据构建模式自动选择子目录） =====
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

# ===== 编译器选项 =====
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ===== 查找 Qt6 =====
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    Multimedia
    MultimediaWidgets
)

# WebEngine 可选
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    message(STATUS "Qt6 WebEngineWidgets found → AI 内嵌浏览器可用")
else()
    message(STATUS "Qt6 WebEngineWidgets NOT found → AI 页面将不可用")
endif()

message(STATUS "Using Qt6: ${Qt6_VERSION}")

# ===== 收集源代码（源码在 user/ 子目录下） =====
file(GLOB_RECURSE SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB_RECURSE HEADERS
    CONFIGURE_DEPENDS
    src/*.h
)

file(GLOB_RECURSE FORMS
    CONFIGURE_DEPENDS
    src/*.ui
)

set(RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resource/res.qrc
)

# ===== 可执行目标 =====
add_executable(suliao WIN32
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    ${RESOURCES}
)

# ===== 包含路径 =====
target_include_directories(suliao PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/depend
)

# ===== 链接 Qt 库 =====
target_link_libraries(suliao PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

# WebEngine 可选链接
if(Qt6WebEngineWidgets_FOUND)
    target_link_libraries(suliao PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(suliao PRIVATE HAS_WEBENGINE)
    add_custom_command(TARGET suliao POST_BUILD
        COMMAND  -E copy_if_different
    )
endif()

# ===== Windows 专用 =====
if(WIN32)
    target_sources(suliao PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/suliao_resource.rc")
endif()


# ===== 运行时 Qt 库定位 =====
if(WIN32)
    # Windows: 构建后用 windeployqt 将 DLL 复制到输出目录
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin"
        REQUIRED
    )
    message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

    add_custom_command(TARGET suliao POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-virtualkeyboard
            "$<TARGET_FILE:suliao>"
        COMMENT "Deploying Qt dependencies via windeployqt..."
    )
endif()

# ===== 安装规则 =====
install(TARGETS suliao
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
