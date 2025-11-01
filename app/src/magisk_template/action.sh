#!/system/bin/sh
MODDIR=${0%/*}
# 修改后结构有点混乱，但不影响正常使用
#
# 持久化设备类型
if [ -f "/proc/tristatekey/tri_state" ]; then
    DEVICE_TYPE="tri"    # 三段式按键
else
    DEVICE_TYPE="custom" # 自定义按键
fi

get_key_click() {
    sleep 0.5
    local key_info=$(getevent -qlc 1 | grep KEY_VOLUME)
    case "$key_info" in
        *KEY_VOLUMEUP*) echo 0; return ;;
        *KEY_VOLUMEDOWN*) echo 1; return ;;
    esac
    echo 2
}

toggle_tristate() {
    local tri_file="/proc/tristatekey/tri_state"
    if [ ! -e "$tri_file" ]; then
        echo "[提示] 三段式按键文件不存在"
        return 1
    fi
    local perm=$(stat -c %a "$tri_file" 2>/dev/null || echo "")
    if [ "$perm" = "200" ]; then
        chmod 0644 "$tri_file" 2>/dev/null
        rm -rf "$MODDIR/disable_tri" 2>/dev/null
        echo "[三段式] 当前状态: 已解除屏蔽"
    else
        chmod 0200 "$tri_file" 2>/dev/null
        touch "$MODDIR/disable_tri" 2>/dev/null
        echo "[三段式] 当前状态: 已设置屏蔽"
    fi
}

show_prompt() {
    echo "-------------------------------------------"
    echo " 欢迎使用 Oplus侧键拓展模块 "
    echo " 功能：让你的侧边键 / 自定义按键 拥有更多玩法！"
    echo " 我们在模块目录的txt文件为您提供了一些常用命令，您可以复制，然后加入自定义操作"
    echo "-------------------------------------------"

    if [ "$DEVICE_TYPE" = "tri" ]; then
        local perm=$(stat -c %a /proc/tristatekey/tri_state 2>/dev/null || echo "")
        local status="未知"
        [ "$perm" = "200" ] && status="已屏蔽" || status="未屏蔽"
        echo "✅ 检测到 [三段式侧键]"
        echo "👉 请在模块目录中的 t-stage.sh 中自定义操作"
        echo " "
        echo "当前屏蔽状态: $status"
        echo "按音量+切换屏蔽 / 按音量-退出"
        local tri_file="/proc/tristatekey/tri_state"
        local key=$(get_key_click)
        if [ "$key" -eq 0 ]; then
            toggle_tristate
        fi
    else
        local config_file="${MODDIR}/max_clicks"
        local current_mode=0

        # 读取当前模式
        if [ -f "$config_file" ]; then
             current_mode=$(cat "$config_file")
        fi
        echo "✅ 检测到 [自定义按键]"
        echo "👉 模块支持监听 [单击 / 双击 / 长按]"
        echo "👉 请在模块目录中的 cust.sh 中自定义操作"
        echo "⚠️ 请先到系统设置中将侧键设为 [无操作]"
        echo "✅ 您也可以到模块目录的max_clicks更改监听模式"
        case "$current_mode" in
            1)
                echo "当前模式：仅单击+长按 (单击立即响应)"
            ;;
            2)
                echo "当前模式：单击+双击+长按 (单击有400ms延迟)"
            ;;
            0)
                echo "当前模式：不限制连击次数 (所有短按均有400ms延迟)"
            ;;
        esac
        echo " "
        echo " [音量+] → 切换到下一个模式"
        echo " [音量-] → 确认当前选择"
        echo " "
        echo "0= 不限制连击次数 (所有短按均有400ms延迟)"
        echo "1= 仅单击+长按 (单击立即响应)"
        echo "2= 单击+双击+长按 (单击有400ms延迟)"
        echo " "
        echo "👇🏻这里是序号"
        while true; do
            echo "  $current_mode"
            case $(get_key_click) in
                0)
                    current_mode=$((current_mode + 1))
                ;;
                1)
                    break
                ;;
                2)
                    echo "未检测到音量键，请重试..."
                ;;
                esac
            if [ $current_mode -gt 2 ]; then
                current_mode=0
            fi
        done
        echo " "
        echo " 选择了: $current_mode"
        echo " "
        rm -f "${MODDIR}/max_clicks"
        echo "$current_mode" > "${MODDIR}/max_clicks"
        case "$current_mode" in
            1)
            echo "切换后模式：仅单击+长按 (单击立即响应)"
            ;;
            2)
            echo "切换后模式：单击+双击+长按 (单击有400ms延迟)"
            ;;
            0)
            echo "切换后模式：不限制连击次数 (所有短按均有400ms延迟)"
            ;;
        esac
    fi
}

main() {
    show_prompt
    echo "退出脚本"
}

main
