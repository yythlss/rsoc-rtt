#include <rtthread.h>
#include <stdlib.h> /* 用于 rand() */

/* ==================== 宏定义与数据结构 ==================== */

#define MSG_QUEUE_CAPACITY      (5)     // 消息队列容量
#define PACKAGES_PER_SENDER     (5)     // 每个发件线程发送的包裹数
#define TOTAL_SENDERS           (2)     // 发件线程总数
#define SENDER_STACK_SIZE       (1024)  // 发件线程栈大小
#define SORTER_STACK_SIZE       (2048)  // 分拣线程栈大小
#define SENDER_PRIORITY         (25)    // 发件优先级
#define SORTER_PRIORITY         (24)    // 分拣优先级（略高，优先处理）

// 快递消息结构
struct package_msg {
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

// 结束信号结构
struct end_msg {
    rt_uint32_t type; // 固定为 0xFFFF
};

/* ==================== 全局变量 ==================== */

static rt_mq_t g_msg_queue;
static rt_mutex_t g_id_mutex;
static rt_uint32_t g_next_id = 1;
static rt_uint32_t g_send_failed_count = 0;

/* ==================== 辅助函数 ==================== */

// 获取区域名称字符串
static const char* get_region_name(rt_uint8_t region) {
    switch (region) {
        case 1: return "east";
        case 2: return "south";
        case 3: return "north";
        default: return "unknown";
    }
}

/* ==================== 线程入口函数 ==================== */

/**
 * @brief 发件线程入口
 */
static void sender_thread_entry(void *parameter) {
    struct package_msg msg;
    rt_err_t result;

    for (int i = 0; i < PACKAGES_PER_SENDER; i++) {
        // 1. 获取唯一 ID (互斥锁保护)
        if (rt_mutex_take(g_id_mutex, RT_WAITING_FOREVER) == RT_EOK) {
            msg.id = g_next_id++;
            rt_mutex_release(g_id_mutex);
        } else {
            continue; // 获取失败则跳过
        }

        // 2. 生成随机数据
        // 使用 rand() % 3 + 1 生成 1, 2, 3
        msg.region = (rt_uint8_t)(rand() % 3 + 1);
        msg.weight = (rt_uint16_t)(rand() % 900 + 100); // 100g - 1000g

        // 3. 发送到消息队列
        // 注意：这里使用 RT_WAITING_NO 模拟“不堵塞”，如果队列满了直接算发送失败
        result = rt_mq_send(g_msg_queue, &msg, sizeof(struct package_msg));

        if (result != RT_EOK) {
            g_send_failed_count++;
            rt_kprintf("[SENDER] Send failed (Queue Full), ID: %d\n", msg.id);
        } else {
            rt_kprintf("[SENDER] Sent package ID: %d, Region: %s\n",
                       msg.id, get_region_name(msg.region));
        }

        // 模拟发件耗时
        rt_thread_mdelay(50);
    }

    // 4. 发送完成，投递结束信号
    struct end_msg end_signal;
    end_signal.type = 0xFFFF;
    rt_mq_send(g_msg_queue, &end_signal, sizeof(struct end_msg));

    rt_kprintf("[SENDER] Thread finished.\n");
}

/**
 * @brief 分拣线程入口
 */
static void sorter_thread_entry(void *parameter) {
    rt_err_t result;
    char recv_buf[64]; // 接收缓冲区，必须足够大以容纳最大的结构体
    int total_packages = 0;
    int count_east = 0, count_south = 0, count_north = 0;
    int total_weight = 0;

    rt_kprintf("\n[SYSTEM] Sorting center started...\n");

    while (1) {
        // 【修复点1】rt_mq_recv 需要4个参数：队列，缓冲，大小，等待时间
        result = rt_mq_recv(g_msg_queue, recv_buf, sizeof(recv_buf), RT_WAITING_FOREVER);

        if (result == RT_EOK) {
            // 检查是否是结束信号
            // 简单判断：如果第一个字节是 0xFF (即 0xFFFF 的低位)，视为结束信号
            // 严谨做法是判断 size 或者在结构体里加 type 字段，这里简化处理
            struct end_msg *p_end = (struct end_msg *)recv_buf;
            if (p_end->type == 0xFFFF) {
                break; // 收到结束信号，退出循环
            }

            // 正常处理快递
            struct package_msg *p_pkg = (struct package_msg *)recv_buf;

            total_packages++;
            total_weight += p_pkg->weight;

            const char *region_str = get_region_name(p_pkg->region);

            // 【修复点2】if-else 语法修正：LOG/printf 必须在 {} 内部或作为独立语句
            if (p_pkg->region == 1) {
                count_east++;
            } else if (p_pkg->region == 2) {
                count_south++;
            } else if (p_pkg->region == 3) {
                count_north++;
            }

            // 打印分拣信息
            rt_kprintf("[SORTER] Package %-5u -> %-6s weight: %u g\n",
                       p_pkg->id, region_str, p_pkg->weight);
        }
    }

    // 5. 打印统计结果
    rt_kprintf("\n---------------- STATISTICS ----------------\n");
    rt_kprintf("Total packages : %d\n", total_packages);
    rt_kprintf("East: %d, South: %d, North: %d\n", count_east, count_south, count_north);
    rt_kprintf("Total weight   : %d g\n", total_weight);
    rt_kprintf("Send failed    : %d\n", g_send_failed_count);
    rt_kprintf("--------------------------------------------\n");

    // 6. 清理资源 (可选，如果是单次运行Demo可以不删，但好习惯是删除)
    // 注意：在实际工程中，通常由创建者删除，这里为了演示完整性放在最后
}

/* ==================== MSH 命令导出 ==================== */

/**
 * @brief 启动快递分拣演示的命令函数
 */
void package_sorting_demo(int argc, char **argv) {
    rt_thread_t tid_sender1, tid_sender2, tid_sorter;

    rt_kprintf(">>> Starting Package Sorting Demo...\n");

    // 重置全局状态
    g_next_id = 1;
    g_send_failed_count = 0;

    // 1. 创建互斥量
    g_id_mutex = rt_mutex_create("id_mtx", RT_IPC_FLAG_FIFO);
    if (g_id_mutex == RT_NULL) {
        rt_kprintf("Create mutex failed!\n");
        return;
    }

    // 2. 创建消息队列
    // 队列大小要能容纳 package_msg 和 end_msg，取较大者
    g_msg_queue = rt_mq_create("pkg_mq",
                               sizeof(struct package_msg), // 消息最大长度
                               MSG_QUEUE_CAPACITY,         // 队列容量
                               RT_IPC_FLAG_FIFO);
    if (g_msg_queue == RT_NULL) {
        rt_kprintf("Create message queue failed!\n");
        rt_mutex_delete(g_id_mutex);
        return;
    }

    // 3. 创建分拣线程
    tid_sorter = rt_thread_create("sorter",
                                  sorter_thread_entry,
                                  RT_NULL,
                                  SORTER_STACK_SIZE,
                                  SORTER_PRIORITY,
                                  10);
    if (tid_sorter != RT_NULL) {
        rt_thread_startup(tid_sorter);
    }

    // 4. 创建两个发件线程
    tid_sender1 = rt_thread_create("sender1",
                                   sender_thread_entry,
                                   RT_NULL,
                                   SENDER_STACK_SIZE,
                                   SENDER_PRIORITY,
                                   10);
    if (tid_sender1 != RT_NULL) {
        rt_thread_startup(tid_sender1);
    }

    tid_sender2 = rt_thread_create("sender2",
                                   sender_thread_entry,
                                   RT_NULL,
                                   SENDER_STACK_SIZE,
                                   SENDER_PRIORITY,
                                   10);
    if (tid_sender2 != RT_NULL) {
        rt_thread_startup(tid_sender2);
    }

    rt_kprintf(">>> Threads created successfully.\n");
}

// 导出命令到 MSH
MSH_CMD_EXPORT(package_sorting_demo, Start the package sorting center demo);

int main(void)
{
    return 0;
}

