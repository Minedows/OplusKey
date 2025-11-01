#!/system/bin/sh
rm -rf "/data/misc/user/0"
touch /data/adb/modules/tri_exp_caelifall/remove > /dev/null 2>&1
ps -A -o pid,args | grep tri_exp_caelifall | grep -v grep | awk '{print $1}' | xargs -r kill -9 > /dev/null 2>&1

GET_KEY_CLICK() {
    sleep 0.5
    case "$(getevent -qlc 1 | grep KEY_VOLUME)" in
        *KEY_VOLUMEUP*) echo 0;;
        *KEY_VOLUMEDOWN*) echo 1;;
        *) echo 2;;
    esac
}

echo "-------------------------------------------"
echo " 欢迎使用 Oplus侧键拓展模块 "
echo " 功能：让你的侧边键 / 自定义按键 拥有更多玩法！"
echo "-------------------------------------------"

if [ -f "/proc/tristatekey/tri_state" ]; then
    echo "已为您屏蔽系统原有功能（卸载自动恢复，也可通过action功能调整）"
    echo "✅ 检测到 [三段式侧键]"
    echo "👉 请在模块目录中的 t-stage.sh 中自定义操作"
    chmod 0200 /proc/tristatekey/tri_state 2 >/dev/null
    touch "$MODPATH/disable_tri"
    echo "⚠️ 已屏蔽系统原有功能（卸载自动恢复，也可在 action 功能调整）"
else
    echo "✅ 检测到 [自定义按键]"
    echo "👉 模块支持监听 [单击 / 双击 / 长按 / 三击] 等复杂手势"
    echo "👉 请在模块目录中的 cust.sh 中自定义操作"
    echo "⚠️ 请先到系统设置中将侧键设为 [无操作]"
    echo " "
    echo "  [音量+] → 切换到下一个模式"
    echo "  [音量-] → 确认当前选择"
    echo " "
    echo " 0= 不限制连击次数 (所有短按均有400ms延迟)"
    echo " 1= 仅单击+长按 (单击立即响应)"
    echo " 2= 单击+双击+长按 (单击有400ms延迟)"
    echo " "
    echo " 👇🏻这里是序号"
    FC=0
    while true; do
        echo "  $FC"
        case $(GET_KEY_CLICK) in
            0)
            FC=$((FC + 1))
            ;;
            1)
            break
            ;;
            2)
            echo "未检测到音量键，请重试..."
        esac
        if [ $FC -gt 2 ]; then
            FC=0
        fi
    done
    echo " "
    echo " 选择了: $FC"
    echo " "
    rm -f "$MODPATH/max_clicks"
    echo "$FC" > "$MODPATH/max_clicks"
    case "$FC" in
        1)
            echo " 当前模式：仅单击+长按 (单击立即响应)"
        ;;
        2)
            echo " 当前模式：单击+双击+长按 (单击有400ms延迟)"
        ;;
        0)
            echo " 当前模式：不限制连击次数 (所有短按均有400ms延迟)"
        ;;
    esac
    echo "✅ 后续可以运行action，或者更改模块目录的max_clicks来改变监听模式"
fi
echo "-------------------------------------------"
echo " 我们为您准备了一些预设功能，可以复制到 cust.sh 或 t-stage.sh 使用："
echo " - 手电筒（只支持骁龙设备）"
echo " - 付款码"
echo " - 扫一扫"
echo " - NFC"
echo " - 蓝牙"
echo " - 录音"
echo " - 录屏"
echo " - Clash 服务"
echo " - 启动相机"
echo " - 录制视频"
echo "-------------------------------------------"
echo "✅ 安装完成！您可以在 cust.sh / t-stage.sh 中定制功能"
echo "👉 更多玩法欢迎自行扩展"
echo ""
echo "感谢使用本模块！"
echo "作者：ItosEO & YangFengTuoZi & Minedows"
echo "-------------------------------------------"
set_perm_recursive $MODPATH 0 0 0755 0755