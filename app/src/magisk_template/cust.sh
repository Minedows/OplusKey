#!/system/bin/sh

# 从 C++ 程序接收的第一个参数 (single, double, 或 long)
ACTION=${1:-"none"}

# 单击时执行的命令
cust_single_click_script() {
    # --- 请在这里定义您的自定义功能 ---
    # 示例：截屏
    service call color_screenshot 1
    
    echo "Single-click action executed"
}

# 双击时执行的命令
cust_double_click_script() {
    # --- 请在这里定义您的自定义功能 ---
    # 示例：打开/关闭Coloros录屏
    dumpsys activity services | grep -q "com.oplus.screenrecorder/.RecorderService" && am start-service -n com.oplus.screenrecorder/com.oplus.screenrecorder.floatwindow.services.CommandRecorderService --ez recorder_game true --ei recorder_command 3 || am start -n com.oplus.screenrecorder/.MainActivity

    echo "Double-click action executed"
}

# 长按时执行的命令
cust_long_click_script() {
    # --- 请在这里定义您的自定义功能 ---
    # 示例：小布识屏 (不要在桌面或锁屏打开，会出Bug)
    am start-foreground-service -a com.coloros.directui.SidebarScenesFunction -n com.coloros.directui/.DirectUIServices --es extra_entrance_function "full_screen_ocr"

    echo "Long-press action executed"
}

# --- 主逻辑：根据参数调用不同函数 ---

case "$ACTION" in
    "single")
        cust_single_click_script
        ;;
    "double")
        cust_double_click_script
        ;;
    "long")
        cust_long_click_script
        ;;
    *)
        # 如果接收到未知参数，则不执行任何操作
        echo "Unknown action: $ACTION"
        ;;
esac