#!/system/bin/sh
#
# 自定义侧键功能脚本 (cust.sh)
#
# 由 cust-action 程序调用。
# 接收一个数字参数，代表用户的手势。
#
# 手势编码规则:
#   - 参数由两部分组成：{点击次数}{是否长按}
#   - 是否长按：0 表示以短按结束，1 表示以长按结束。
#
# 常见示例:
#   '10' -> 单击 (1次点击, 非长按)
#   '20' -> 双击 (2次点击, 非长按)
#   '30' -> 三连击 (3次点击, 非长按)
#   '11' -> 长按 (1次点击, 是长按)
#   '21' -> 单击后长按 (2次点击, 最后是长按)
#
ACTION_CODE=${1:-"unknown"}

case "$ACTION_CODE" in
    "10")
        # ==================================
        #  单击
        # ==================================
        # --- 请在这里定义您的单击功能 ---

        # 示例：截屏
        service call color_screenshot 1

        ;;

    "20")
        # ==================================
        #  双击
        # ==================================
        # --- 请在这里定义您的双击功能 ---

        # 示例：打开/关闭Coloros录屏
        dumpsys activity services | grep -q "com.oplus.screenrecorder/.RecorderService" && am start-service -n com.oplus.screenrecorder/com.oplus.screenrecorder.floatwindow.services.CommandRecorderService --ez recorder_game true --ei recorder_command 3 || am start -n com.oplus.screenrecorder/.MainActivity

        ;;

    "11")
        # ==================================
        #  长按
        # ==================================
        # --- 请在这里定义您的长按功能 ---

        # 示例：小布识屏 (不要在桌面或锁屏打开，会出Bug)
        am start-foreground-service -a com.coloros.directui.SidebarScenesFunction -n com.coloros.directui/.DirectUIServices --es extra_entrance_function "full_screen_ocr"

        ;;

    "30")
        # ==================================
        #  三击
        # ==================================
        # --- 请在这里定义您的三击功能 ---



        ;;

    "21")
        # ==================================
        #  单击后长按
        # ==================================
        # --- 请在这里定义您的单击后长按功能 ---


        ;;

    *)
        # ==================================
        #  未知手势
        # ==================================
        # 接收到未配置的手势码
        # 这里调用了cmd2gui输出toast，请前往酷安下载，否则无法输出
        am startservice -n com.cmd2gui/.svc -a Toast -e content "未定义手势：$ACTION_CODE"
        ;;
esac