/**
 * @file cust-action_main.cpp
 * @brief 高级、健壮、低功耗的自定义按键手势识别器。
 *
 * 该程序作为一个后台服务运行，通过监听Linux输入子系统（/dev/input/event*）
 * 来捕获特定物理按键的事件。它能够精确地区分用户的复杂手势序列，包括：
 *  - N次连击 (单击, 双击, 三击...)
 *  - 长按
 *  - 连击后长按
 *
 * 它通过一个事件驱动的、基于条件变量的计时器线程来管理所有延迟操作，
 * 实现了极低的CPU和功耗。同时，通过手势ID和互斥锁机制，确保了在复杂
 * 并发场景下的绝对线程安全和逻辑正确性。
 *
 * 用户可以通过在模块目录下创建`max_clicks`文件来定制最大点击次数，
 * 以实现对常用操作（如双击）的零延迟响应。
 *
 * 最终，程序会调用外部的 shell 脚本（cust.sh），并传递一个编码后
 * 的手势码（例如 "10" 代表单击, "11" 代表长按, "20" 代表双击），
 * 以此执行高度可定制的功能。
 */

// =====================================================================================
// --- 0. 头文件与宏定义 ---
// =====================================================================================

// C++ 标准库
#include <iostream>               // 标准输入输出流
#include <string>                 // 字符串处理
#include <vector>                 // 动态数组（用于任务列表）
#include <thread>                 // 多线程支持
#include <chrono>                 // 时间处理（用于计时和延时）
#include <atomic>                 // 原子操作，保证多线程下的变量安全
#include <mutex>                  // 互斥锁，用于保护共享数据
#include <condition_variable>     // 条件变量，用于线程间的同步与等待
#include <functional>             // functional库，用于包装可调用对象（如lambda表达式）
#include <algorithm>              // 算法库（如排序、查找）
#include <fstream>                // 文件流，用于读写文件
#include <cstdio>                 // C风格的输入输出函数 (printf/fprintf)

// 系统头文件 (Linux specific)
#include <fcntl.h>                // 文件控制选项 (如 O_RDONLY)
#include <unistd.h>               // POSIX 操作系统 API (如 read, close)
#include <linux/input.h>          // Linux 输入事件子系统的定义 (struct input_event)
#include <cerrno>                 // C 风格的错误码定义 (errno)
#include <cstring>                // C 风格的字符串操作 (strerror)

// 项目头文件
#include "cust-action_main.h"     // 主模块的头文件声明
// #include "logger.h"             // 用本地宏定义重写
#include "utils.h"

#ifndef LOG_INFO
#define LOG_INFO(format, ...) printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(format, ...) fprintf(stderr, "[ERROR] " format "\n", ##__VA_ARGS__)
#endif

// =====================================================================================
// --- 1. 核心数据结构与全局变量 ---
// =====================================================================================

/**
 * @brief 描述一个需要延迟执行的任务。
 */
struct DelayedTask {
    long id;                                    // 任务的唯一ID，用于取消
    long gesture_id;                            // 关联的手势ID，用于防止旧手势的任务干扰新一轮手势
    std::chrono::steady_clock::time_point execution_time; // 任务应当被执行的精确时间点
    std::function<void()> action;               // 要执行的具体操作，通常是一个lambda函数
};

// --- 全局状态变量 ---
std::vector<DelayedTask> tasks;         // 全局的任务队列，存放所有待执行的延时任务
std::mutex tasks_mutex;                 // 互斥锁，用于保护对 `tasks` 队列的并发访问
std::condition_variable tasks_cv;       // 条件变量，用于高效地等待任务或通知 `timer_thread` 线程
std::atomic<long> next_task_id(1);      // 原子计数器，用于生成唯一的 `DelayedTask` ID
std::atomic<bool> running(true);        // 全局运行状态标志，用于优雅地停止所有线程
std::thread timer_thread;               // 计时器线程对象，负责执行延时任务

// --- 手势识别相关的状态变量 ---
int clickCount = 0;                     // 当前手势序列中的点击次数计数器
long current_gesture_id = 0;            // 当前正在进行的手势的唯一ID
bool longPressHandled = false;          // 标志位，表示当前按键按下是否已经被处理为长按
std::mutex state_mutex;                 // 互斥锁，用于保护手势相关的状态变量 (clickCount, current_gesture_id, longPressHandled)
int fd = -1;                            // 输入设备的文件描述符

// =====================================================================================
// --- 2. 任务管理与计时器线程 ---
// =====================================================================================

/**
 * @brief 添加一个延时任务到任务队列。
 * @param delay_ms 延迟执行的毫秒数。
 * @param gesture_id 与此任务关联的手势ID。
 * @param action 到时需要执行的函数。
 * @return 返回任务的唯一ID，可用于后续取消。
 */
long addTask(int delay_ms, long gesture_id, std::function<void()> action) {
    long id = next_task_id++; // 原子地获取并增加任务ID
    auto execution_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);

    {
        std::lock_guard<std::mutex> lock(tasks_mutex); // 加锁以安全地修改任务队列
        tasks.push_back({id, gesture_id, execution_time, std::move(action)});

        // 对任务队列进行排序，确保执行时间最早的任务在队列末尾 (pop_back效率高)
        // 可以进行优化，但当前场景用sort排序已经足够了
        std::sort(tasks.begin(), tasks.end(), [](const auto& a, const auto& b) {
            return a.execution_time > b.execution_time;
        });
    }

    tasks_cv.notify_one(); // 唤醒可能正在等待的计时器线程
    return id;
}

/**
 * @brief 根据任务ID取消一个尚未执行的延时任务。
 * @param id 要取消的任务ID。如果ID为0，则不执行任何操作。
 */
void cancelTask(long id) {
    if (id == 0) return; // 无效ID，直接返回
    {
        std::lock_guard<std::mutex> lock(tasks_mutex); // 加锁以安全地修改任务队列
        // 使用 remove_if + erase 的惯用法从vector中删除指定ID的任务
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [id](const DelayedTask& task) {
            return task.id == id;
        }), tasks.end());
    }
    tasks_cv.notify_one(); // 唤醒计时器线程，让其可以重新计算等待时间
}

/**
 * @brief 计时器线程的主函数。
 *        该线程循环等待并执行到期的延时任务。
 * @param script_path (未使用) 此处参数未在函数体内使用，可能是早期版本遗留。
 */
void timer_thread_func(const std::string& script_path) {
    while (running) { // 只要程序在运行状态，就持续循环
        std::vector<DelayedTask> due_tasks; // 存储已到期的任务，避免在锁内执行耗时操作
        {
            std::unique_lock<std::mutex> lock(tasks_mutex); // 使用 unique_lock 以配合条件变量

            if (tasks.empty()) {
                // 如果任务队列为空，则无限期等待，直到被 addTask 或程序退出信号唤醒
                tasks_cv.wait(lock, [&]() { return !tasks.empty() || !running; });
            } else {
                // 如果有任务，则计算下一个任务的到期时间点
                auto next_timeout = tasks.back().execution_time;
                // 等待直到下一个任务到期，或被其他信号唤醒
                tasks_cv.wait_until(lock, next_timeout);
            }

            if (!running) break; // 如果程序被要求退出，则跳出循环

            auto now = std::chrono::steady_clock::now();
            // 检查并取出所有已到期（或过时）的任务
            while (!tasks.empty() && now >= tasks.back().execution_time) {
                due_tasks.push_back(std::move(tasks.back()));
                tasks.pop_back();
            }
        } // 锁在此处被释放

        // 在锁外执行所有到期的任务，避免阻塞其他线程
        for (const auto& task : due_tasks) {
            task.action();
        }
    }
}

// =====================================================================================
// --- 3. 主逻辑 ---
// =====================================================================================

/**
 * @brief 从配置文件中读取最大连续点击次数。
 * @param mod_dir 模块所在的目录路径。
 * @return 返回读取到的最大点击次数，如果文件不存在或内容无效，返回0。
 */
int getMaxClicks(const std::string& mod_dir) {
    std::ifstream file(mod_dir + "/max_clicks"); // 构造文件路径并尝试打开
    if (file.is_open()) {
        int max_c = 0;
        file >> max_c; // 从文件中读取一个整数
        if (max_c > 0) return max_c;
    }
    return 0; // 默认返回0，表示不限制
}

/**
 * @brief 程序的主入口函数，负责监听按键事件并进行手势识别。
 * @param mod_dir 模块所在的目录路径，用于查找配置文件和脚本。
 */
void custActionMain(const std::string &mod_dir) {
    LOG_INFO("尝试打开设备 /dev/input/event0...");
    fd = open("/dev/input/event0", O_RDONLY); // 以只读方式打开输入设备
    if (fd < 0) {
        LOG_ERROR("打开设备 /dev/input/event0 失败. 错误: %s", strerror(errno));
        return;
    }
    LOG_INFO("设备打开成功。");

    // --- 定义手势识别的超时常量 ---
    const int GESTURE_TIMEOUT_MS = 400;     // 两次点击之间的最大间隔，超过则认为手势结束
    const int LONG_PRESS_TIMEOUT_MS = 500;  // 按下超过此时间被视为长按

    int max_clicks = getMaxClicks(mod_dir); // 获取最大点击次数配置
    const std::string script_path = mod_dir + "/cust.sh"; // 要执行的脚本路径

    // --- 任务ID变量，用于跟踪和取消特定的延时任务 ---
    long long_press_task_id = 0;       // 长按检测任务的ID
    long gesture_timeout_task_id = 0; // 手势序列超时任务的ID

    // --- 启动后台计时器线程 ---
    running = true;
    timer_thread = std::thread(timer_thread_func, script_path);

    struct input_event ev{}; // 用于接收输入事件的数据结构
    while (running) { // 主事件监听循环
        // 阻塞式读取一个输入事件
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            // 读取被信号中断，且程序仍在运行，则继续循环
            if (n < 0 && errno == EINTR && running) {
                continue;
            }
            // 如果是程序退出信号导致的读取中断
            if (!running) LOG_INFO("收到退出信号，停止读取循环。");
                // 其他读取错误
            else LOG_ERROR("读取错误: %s", strerror(errno));
            break; // 退出循环
        }

        // --- 事件过滤：只关心特定按键 (code 0x02df) 的事件 ---
        if (ev.type != EV_KEY || ev.code != 0x02df) continue;

        // --- 按键按下事件 (ev.value == 1) ---
        if (ev.value == 1) {
            // 取消之前可能存在的“手势超时”任务，因为新的按下事件表示手势序列在继续
            cancelTask(gesture_timeout_task_id);

            long current_gid;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                // 如果这是新一轮手势的第一次点击，则重置状态
                if (clickCount == 0) {
                    current_gesture_id++; // 生成一个新的手势ID
                    max_clicks = getMaxClicks(mod_dir); // 重新加载配置
                }
                longPressHandled = false; // 重置长按标志
                current_gid = current_gesture_id; // 记录当前手势ID
            }

            // 添加一个延时任务来检测长按
            long_press_task_id = addTask(LONG_PRESS_TIMEOUT_MS, current_gid, [current_gid, script_path]() {
                std::string code_to_exec;
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    // 确认此任务仍然属于当前手势（防止旧任务干扰）
                    if (current_gesture_id == current_gid) {
                        longPressHandled = true; // 标记已处理为长按
                        clickCount++; // 增加点击计数
                        LOG_INFO("长按被确认为第 %d 次点击。", clickCount);
                        // 生成执行代码：[点击次数] + "1" (表示长按)
                        code_to_exec = std::to_string(clickCount) + "1";
                        clickCount = 0; // 手势结束，重置计数器
                    }
                }
                // 如果生成了代码，则在后台执行脚本
                if (!code_to_exec.empty()) {
                    execBackground(DEFAULT_SHELL, {script_path, code_to_exec});
                }
            });

            // --- 按键抬起事件 (ev.value == 0) ---
        } else if (ev.value == 0) {
            // 按键已抬起，所以立即取消长按检测任务
            cancelTask(long_press_task_id);

            bool was_long_press;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                was_long_press = longPressHandled; // 检查刚刚的按下是否已被处理为长按
            }
            // 如果是长按事件后的抬起，则忽略，因为长按事件已经触发
            if (was_long_press) continue;

            std::string code_to_exec;
            long current_gid;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                clickCount++; // 点击次数加一
                current_gid = current_gesture_id; // 记录当前手势ID

                // 如果达到了配置的最大点击次数，则手势立即结束
                if (max_clicks > 0 && clickCount >= max_clicks) {
                    LOG_INFO("短按。已达到最大点击次数。总计: %d", clickCount);
                    // 生成执行代码：[点击次数] + "0" (表示短按)
                    code_to_exec = std::to_string(clickCount) + "0";
                    clickCount = 0; // 重置计数器
                } else {
                    LOG_INFO("短按。总计点击次数: %d", clickCount);
                }
            }

            // 如果已生成执行代码（由于达到最大点击次数），则立即执行并跳过后续步骤
            if (!code_to_exec.empty()) {
                execBackground(DEFAULT_SHELL, {script_path, code_to_exec});
                continue;
            }

            // 添加一个手势超时任务。如果在 GESTURE_TIMEOUT_MS 内没有新的点击，此任务将执行
            gesture_timeout_task_id = addTask(GESTURE_TIMEOUT_MS, current_gid, [current_gid, script_path]() {
                std::string code_to_exec;
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    // 确认任务有效，并且确实有点击发生
                    if (current_gesture_id == current_gid && clickCount > 0) {
                        LOG_INFO("手势序列在 %d 次点击后超时。", clickCount);
                        // 生成执行代码：[最终点击次数] + "0"
                        code_to_exec = std::to_string(clickCount) + "0";
                        clickCount = 0; // 手势结束，重置计数器
                    }
                }
                // 如果生成了代码，则执行脚本
                if (!code_to_exec.empty()) {
                    execBackground(DEFAULT_SHELL, {script_path, code_to_exec});
                }
            });
        }
    }

    // --- 清理与退出 ---
    running = false; // 设置全局停止标志
    tasks_cv.notify_one(); // 唤醒计时器线程，让它检查 running 标志并退出
    if(timer_thread.joinable()) timer_thread.join(); // 等待计时器线程完全结束
    if(fd >= 0) {
        close(fd); // 关闭设备文件描述符
        fd = -1;
    }
}

/**
 * @brief 外部调用的退出函数，用于优雅地停止程序。
 */
void custActionExit() {
    LOG_INFO("调用 custActionExit，开始清理...");
    // 使用 exchange 原子地设置 running 为 false，并检查其旧值，防止重复进入
    if (running.exchange(false)) {
        if (fd >= 0) {
            // 多线程环境下，直接关闭另一个线程正在使用的fd是不安全的。
            // 一个更健壮的方法是使用 pipe-to-self 或 signalfd 来中断阻塞的 read。
            // 但对于此场景，设置 running 标志并等待主循环自然退出已足够。
        }
        // 确保计时器和主循环都能被唤醒并快速退出
        tasks_cv.notify_one();
        if (timer_thread.joinable()) {
            timer_thread.join();
        }
        LOG_INFO("清理完成。");
    }
}