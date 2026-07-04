#!/bin/bash
# ============================================
# 速聊服务端 AppImage 打包
# 用法: bash package.sh <Qt目录> [构建类型]
# ============================================
set -e

QT_PREFIX="${1:-/opt/Qt/6.7.3/gcc_64}"
BUILD_TYPE="${2:-Release}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BINARY="$BUILD_DIR/$BUILD_TYPE/server"
APPDIR="$SCRIPT_DIR/server.AppDir"
APP_NAME="server.AppImage"

# 共享工具缓存
TOOLS_DIR="$PROJECT_ROOT/tool/AppimageTools"
mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
PLUGIN_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"

echo "=========================================="
echo " Qt: $QT_PREFIX"
echo "=========================================="

# ===== 1. 编译 =====
echo "[1/5] 编译..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DQT_SKIP_AUTO_PLUGIN_INCLUSION=ON
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(nproc)"
[ -f "$BINARY" ] || { echo "错误: 编译失败"; exit 1; }

# ===== 2. 创建 AppDir =====
echo "[2/5] AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BINARY"                                "$APPDIR/usr/bin/"
cp "$SCRIPT_DIR/src/appimage/server.desktop"     "$APPDIR/"
cp "$SCRIPT_DIR/src/appimage/server.png"         "$APPDIR/"
cp "$SCRIPT_DIR/src/appimage/server.png"         "$APPDIR/.DirIcon"
cp "$SCRIPT_DIR/src/appimage/server.png"         "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
cp "$SCRIPT_DIR/src/appimage/AppRun"            "$APPDIR/"
chmod +x "$APPDIR/AppRun"
sed -i 's/\r$//' "$APPDIR/server.desktop" "$APPDIR/AppRun" 2>/dev/null || true

# ===== 3. 确保工具已下载 =====
echo "[3/5] 检查工具链..."
for tool_url in \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage|$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage|$PLUGIN_QT" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage|$APPIMAGETOOL"
do
    URL="${tool_url%%|*}"
    FILE="${tool_url##*|}"
    if [ ! -f "$FILE" ]; then
        echo "  -> 下载 $(basename "$FILE")..."
        wget -q --show-progress "$URL" -O "$FILE" || { echo "  -> 下载失败"; exit 1; }
        chmod +x "$FILE"
    fi
done

# ===== 4. linuxdeploy 收集依赖 =====
echo "[4/5] linuxdeploy 收集依赖..."

export QMAKE="$QT_PREFIX/bin/qmake"
export LDAI_OUTPUT="$APPDIR"

echo "  -> 运行 linuxdeploy (含 qt 插件)..."
set +e
LINUXDEPLOY_OUTPUT=$("$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/server" \
    --desktop-file "$APPDIR/server.desktop" \
    --icon-file "$APPDIR/server.png" \
    --plugin qt 2>&1)
LINUXDEPLOY_RC=$?
echo "$LINUXDEPLOY_OUTPUT"
set -e
if [ $LINUXDEPLOY_RC -ne 0 ]; then
    echo "  -> 警告: linuxdeploy 返回非零 ($LINUXDEPLOY_RC)，尝试手动补全 Qt 插件"
fi

# ===== 4.5 手动补全 Qt 插件 =====
echo "[4.5/5] 手动部署 Qt 插件..."
QT_PLUGINS_SRC="${QT_PREFIX}/plugins"
QT_PLUGINS_DST="$APPDIR/usr/plugins"

# 全量拷贝 Qt 插件目录（总大小不大，避免遗漏）
if [ -d "${QT_PLUGINS_SRC}" ]; then
    echo "  -> 全量拷贝 Qt 插件..."
    find "${QT_PLUGINS_SRC}" -name "*.so" | while read -r so; do
        rel="${so#${QT_PLUGINS_SRC}/}"
        dst_dir="${QT_PLUGINS_DST}/$(dirname "$rel")"
        mkdir -p "$dst_dir"
        cp -f "$so" "$dst_dir/" || true
    done
    echo "  -> 拷贝完成:"
    find "${QT_PLUGINS_DST}" -name "*.so" -exec ls -la {} \; 2>/dev/null || echo "    (空)"
fi

# ===== 5. 生成 AppImage =====
echo "[5/5] 生成 AppImage..."
"$APPIMAGETOOL" "$APPDIR" "$APP_NAME"

echo ""
echo "=========================================="
echo " 输出: $SCRIPT_DIR/$APP_NAME"
echo " 大小: $(du -h "$SCRIPT_DIR/$APP_NAME" | cut -f1)"
echo "=========================================="

# ===== 6. 清理构建目录 =====
echo "[6/6] 清理..."
rm -rf "$BUILD_DIR" 
#rm -rf "$APPDIR"
