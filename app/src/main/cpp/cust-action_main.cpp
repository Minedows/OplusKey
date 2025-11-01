#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <cstring>
#include <filesystem>
#include "cust-action_main.h"
#include "logger.h"
#include "utils.h"
#include <thread>
#include <chrono>
#include <atomic>

/**
 * @file cust-action_main.cpp
 * @brief 负责监听和处理自定义侧键的输入事件。
 *
 * 该文件实现了对特定输入设备（/dev/input/event0）的底层监听，
 * 并根据按键的按下、保持、抬起事件，精确地区分出用户的单击、
 * 双击和长按操作。最终，它会调用外部的 shell 脚本（cust.sh）
 * 并传递相应的参数（"single", "double", "long"）来执行用户自定义的功能。
 */

// 全局变量，用于记录上一次短按抬起的时间戳（毫秒）。
// 这是实现双击判断的关键，通过计算两次点击的时间差来识别双击。
std::atomic<long> lastClickTime(0);

/**
 * @brief 处理短按事件，区分单击和双击。
 * @param usedTime 按键从按下到抬起的持续时间（毫秒）。
 * @param currentTime 按键抬起时的当前时间戳（毫秒）。
 * @param mod_dir 模块的根目录路径，用于检查双击模式开关文件。
 *
 * 当主循环判断出一次短按后，会调用此函数。
 * 此函数的核心是处理带有延迟确认的单击，以支持双击。
 */
void onClick(long usedTime, long currentTime, const std::string &mod_dir)
{
    // 检查模块目录下是否存在名为 "double_click" 的文件
    // 让用户自行选择是否开启双击模式
    if (std::filesystem::exists(mod_dir + "/double_click"))
    {
        // --- 双击模式已开启 ---

        // 判断当前点击与上一次点击的时间间隔是否小于400毫秒。
        if (currentTime - lastClickTime < 400)
        {
            // --- 判定为双击 ---
            LOG_INFO("Double Click detected");

            // 关键步骤：将 lastClickTime 重置为0。
            // 1. 这“消耗”了第一次点击，使其不再能触发单击事件。
            // 2. 这也防止了连续第三次点击被误判为第二次双击。
            lastClickTime = 0;

            // 调用外部脚本，并传递 "double" 参数。
            execBackground(DEFAULT_SHELL, {mod_dir + "/cust.sh", "double"});
        }
        else
        {
            // --- 判定为第一次单击（可能是独立的单击，也可能是双击的第一下） ---

            // 更新 lastClickTime 为当前时间，为可能的下一次点击做准备。
            lastClickTime = currentTime;

            // 启动一个分离的后台线程，作为“延迟确认”单击的计时器。
            std::thread([=]()
                        {
                            // 在线程内捕获当前点击的时间戳，用于后续的验证。
                            long clickTimeToVerify = currentTime;

                            // 让线程休眠400毫秒，即等待双击的最大时间窗口。
                            std::this_thread::sleep_for(std::chrono::milliseconds(400));

                            // 400毫秒后，线程醒来，检查全局的 lastClickTime 是否仍等于当初记录的时间。
                            // 如果相等，说明没有发生第二次点击来重置它，因此这是一次真正的单击。
                            if (lastClickTime == clickTimeToVerify)
                            {
                                LOG_INFO("Single Click detected (confirmed after timeout)");

                                // 调用外部脚本，并传递 "single" 参数。
                                execBackground(DEFAULT_SHELL, {mod_dir + "/cust.sh", "single"});

                                // （可选但推荐）确认单击后也重置，保持状态干净。
                                lastClickTime = 0;
                            }
                            // 如果不相等（通常是被双击逻辑重置为0了），则线程什么也不做，安静退出。
                        })
                .detach(); // detach() 使线程与主线程分离，自行在后台运行和销毁。
        }
    }
    else
    {
        // --- 双击模式未开启，任何短按都视为单击 ---
        LOG_INFO("Single Click detected");
        // 直接调用外部脚本，并传递 "single" 参数。
        execBackground(DEFAULT_SHELL, {mod_dir + "/cust.sh", "single"});
    }
}

// 全局变量，保存输入设备的文件描述符，以便在退出时能够关闭它。
int fd = -1;

/**
 * @brief 主函数，负责监听设备事件并分发处理。
 * @param mod_dir 模块的根目录路径。
 *
 * 此函数包含一个无限循环，用于持续从内核读取输入事件，
 * 并根据事件类型（按下、保持、抬起）来调用相应的处理逻辑。
 */

void custActionMain(const std::string &mod_dir)
{
    const char *device = "/dev/input/event0";
    fd = open(device, O_RDONLY);
    if (fd < 0)
    {
        perror("Unable to open device");
        exit(1);
    }

    struct input_event ev{};
    long pressTime = 0;
    std::atomic<bool> longPressTriggered(false);
    std::atomic<bool> isButtonPressed(false);

    while (true)
    {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev))
        {
            if (ev.code == 0x02df && ev.type == 1)
            { // 过滤事件

                if (ev.value == 1)
                { // 按下
                    pressTime = ev.time.tv_sec * 1000 + ev.time.tv_usec / 1000;
                    longPressTriggered = false;
                    isButtonPressed = true; // 标记按键已按下

                    // 启动一个后台线程来检测长按
                    std::thread([&]() { // 注意：这里使用引用捕获[&]以修改外部变量
                        // 睡眠500毫秒，作为长按的判定时间
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));

                        // 500毫秒后，检查按键是否仍然处于按下状态
                        if (isButtonPressed)
                        {
                            LOG_INFO("Long Press triggered (while holding)");
                            // 标记长按已触发，以防止后续的抬起事件触发单击
                            longPressTriggered = true;
                            // 执行长按脚本
                            execBackground(DEFAULT_SHELL, {mod_dir + "/cust.sh", "long"});
                        }
                    })
                        .detach();
                }
                else if (ev.value == 0)
                { // 抬起
                    // 标记按键已抬起
                    isButtonPressed = false;

                    // 检查长按是否“没有”被后台线程触发
                    if (!longPressTriggered)
                    {
                        long currentTime = ev.time.tv_sec * 1000 + ev.time.tv_usec / 1000;
                        // 如果没有，那么这就是一次短按，交由 onClick 处理
                        onClick(currentTime - pressTime, currentTime, mod_dir);
                    }
                }
            }
        }
        else
        {
            perror("Read error");
            break;
        }
    }
    close(fd);
}

/**
 * @brief 模块退出时调用的清理函数。
 */
void custActionExit()
{
    // 确保文件描述符有效时才关闭，防止重复关闭或关闭无效句柄。
    if (fd >= 0)
    {
        close(fd);
        fd = -1; // 重置为无效值
    }
}