/*
 * 第三天作业：RT-Thread 线程间同步与通信
 * 开发板：STM32F407 星火号
 * 作者：杨智文（第3组）
 *
 * MSH 命令：
 *   parking_demo  - 计数信号量实现停车场车位管理
 *   ticket_demo   - 互斥量实现多窗口售票，并进行无锁对比
 *   startup_demo  - 事件集等待多个模块全部初始化完成
 *   package_demo  - 消息队列实现快递生产与分拣
 */

#include <rtthread.h>
#include <finsh.h>

#define DEMO_STACK_SIZE          1024
#define RUNNER_STACK_SIZE        1536
#define DEMO_PRIORITY            16
#define RUNNER_PRIORITY          15
#define DEMO_TIMESLICE           5

/* 用临界区保护演示运行标志，防止同一命令被重复启动。 */
static rt_bool_t demo_try_start(rt_bool_t *running)
{
    rt_bool_t accepted = RT_FALSE;

    rt_enter_critical();
    if (*running == RT_FALSE)
    {
        *running = RT_TRUE;
        accepted = RT_TRUE;
    }
    rt_exit_critical();

    return accepted;
}

static void demo_finish(rt_bool_t *running)
{
    rt_enter_critical();
    *running = RT_FALSE;
    rt_exit_critical();
}

static rt_thread_t create_thread(const char *name,
                                 void (*entry)(void *parameter),
                                 void *parameter,
                                 rt_uint32_t stack_size,
                                 rt_uint8_t priority)
{
    rt_thread_t thread;

    thread = rt_thread_create(name, entry, parameter, stack_size,
                              priority, DEMO_TIMESLICE);
    if (thread == RT_NULL)
    {
        rt_kprintf("[error] create thread %s failed\n", name);
    }

    return thread;
}

/* ==================== 练习一：停车场 ==================== */

#define PARKING_CAR_COUNT        5
#define PARKING_SPACE_COUNT      3

struct parking_car_arg
{
    rt_uint8_t id;
};

struct parking_context
{
    rt_sem_t spaces;
    rt_sem_t done;
    rt_uint8_t used_spaces;
    struct parking_car_arg cars[PARKING_CAR_COUNT];
};

static struct parking_context parking_ctx;
static rt_bool_t parking_running = RT_FALSE;

static void parking_car_entry(void *parameter)
{
    struct parking_car_arg *car = (struct parking_car_arg *)parameter;
    rt_uint8_t used;

    /* 让车辆错开到达，同时保证前 3 辆车先占满车位。 */
    rt_thread_mdelay((rt_int32_t)car->id * 80);

    while (rt_sem_take(parking_ctx.spaces, rt_tick_from_millisecond(2000)) != RT_EOK)
    {
        rt_kprintf("car%u waiting for a space (timeout)\n", car->id);
        rt_thread_mdelay(300);
    }

    rt_enter_critical();
    parking_ctx.used_spaces++;
    used = parking_ctx.used_spaces;
    rt_exit_critical();

    rt_kprintf("car%u entered, used spaces: %u\n", car->id, used);

    /* 停留时间超过 2 秒，使后到车辆能够触发一次等待超时。 */
    rt_thread_mdelay(2400 + (rt_int32_t)car->id * 120);

    rt_enter_critical();
    parking_ctx.used_spaces--;
    used = parking_ctx.used_spaces;
    rt_exit_critical();

    rt_kprintf("car%u left, used spaces: %u\n", car->id, used);
    rt_sem_release(parking_ctx.spaces);
    rt_sem_release(parking_ctx.done);
}

static void parking_runner_entry(void *parameter)
{
    rt_thread_t cars[PARKING_CAR_COUNT] = {RT_NULL};
    rt_uint8_t created = 0;
    rt_uint8_t index;
    char name[RT_NAME_MAX];

    RT_UNUSED(parameter);
    rt_memset(&parking_ctx, 0, sizeof(parking_ctx));

    parking_ctx.spaces = rt_sem_create("parksem", PARKING_SPACE_COUNT,
                                       RT_IPC_FLAG_FIFO);
    parking_ctx.done = rt_sem_create("parkdone", 0, RT_IPC_FLAG_FIFO);
    if ((parking_ctx.spaces == RT_NULL) || (parking_ctx.done == RT_NULL))
    {
        rt_kprintf("[parking] create semaphore failed\n");
        goto cleanup;
    }

    rt_kprintf("\n[parking] 3 spaces, 5 cars, wait timeout = 2 seconds\n");
    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        parking_ctx.cars[index].id = index + 1;
        rt_snprintf(name, sizeof(name), "car%u", index + 1);
        cars[index] = create_thread(name, parking_car_entry,
                                    &parking_ctx.cars[index],
                                    DEMO_STACK_SIZE, DEMO_PRIORITY);
        if (cars[index] == RT_NULL)
        {
            break;
        }
        created++;
    }

    if (created != PARKING_CAR_COUNT)
    {
        for (index = 0; index < created; index++)
        {
            rt_thread_delete(cars[index]);
        }
        goto cleanup;
    }

    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        rt_thread_startup(cars[index]);
    }

    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        rt_sem_take(parking_ctx.done, RT_WAITING_FOREVER);
    }
    rt_kprintf("[parking] all cars completed, maximum capacity remained 3\n");

cleanup:
    if (parking_ctx.done != RT_NULL)
    {
        rt_sem_delete(parking_ctx.done);
        parking_ctx.done = RT_NULL;
    }
    if (parking_ctx.spaces != RT_NULL)
    {
        rt_sem_delete(parking_ctx.spaces);
        parking_ctx.spaces = RT_NULL;
    }
    demo_finish(&parking_running);
}

static int parking_demo(int argc, char **argv)
{
    rt_thread_t runner;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_try_start(&parking_running) == RT_FALSE)
    {
        rt_kprintf("parking_demo is already running\n");
        return -RT_EBUSY;
    }

    runner = create_thread("pk_run", parking_runner_entry, RT_NULL,
                           RUNNER_STACK_SIZE, RUNNER_PRIORITY);
    if (runner == RT_NULL)
    {
        demo_finish(&parking_running);
        return -RT_ENOMEM;
    }
    rt_thread_startup(runner);
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking demo);

/* ==================== 练习二：多窗口售票 ==================== */

#define INITIAL_TICKET_COUNT     20

struct ticket_worker_arg
{
    char window;
    rt_bool_t use_mutex;
};

struct ticket_context
{
    rt_mutex_t lock;
    rt_sem_t done;
    rt_int32_t remaining;
    rt_uint32_t sold_count;
    rt_uint32_t duplicate_count;
    rt_uint8_t sold_map[INITIAL_TICKET_COUNT + 1];
    struct ticket_worker_arg workers[2];
};

static struct ticket_context ticket_ctx;
static rt_bool_t ticket_running = RT_FALSE;

static void record_ticket_sale(char window, rt_int32_t ticket)
{
    rt_bool_t duplicate;
    rt_uint32_t sold;

    /* 统计信息单独用临界区保护，避免影响无锁实验的余票竞争窗口。 */
    rt_enter_critical();
    duplicate = ticket_ctx.sold_map[ticket] ? RT_TRUE : RT_FALSE;
    if (duplicate)
    {
        ticket_ctx.duplicate_count++;
    }
    else
    {
        ticket_ctx.sold_map[ticket] = 1;
    }
    ticket_ctx.sold_count++;
    sold = ticket_ctx.sold_count;
    rt_exit_critical();

    rt_kprintf("window %c sold ticket %d%s (sale #%u)\n",
               window, ticket, duplicate ? " DUPLICATE" : "", sold);
}

static void ticket_worker_entry(void *parameter)
{
    struct ticket_worker_arg *worker = (struct ticket_worker_arg *)parameter;
    rt_int32_t observed;

    while (1)
    {
        if (worker->use_mutex)
        {
            if (rt_mutex_take(ticket_ctx.lock, RT_WAITING_FOREVER) != RT_EOK)
            {
                rt_kprintf("window %c failed to take mutex\n", worker->window);
                break;
            }

            if (ticket_ctx.remaining <= 0)
            {
                rt_mutex_release(ticket_ctx.lock);
                break;
            }

            observed = ticket_ctx.remaining;
            ticket_ctx.remaining--;
            record_ticket_sale(worker->window, observed);
            rt_mutex_release(ticket_ctx.lock);
        }
        else
        {
            /* 故意在读取与写回之间延时，放大竞态条件。 */
            observed = ticket_ctx.remaining;
            if (observed <= 0)
            {
                break;
            }
            rt_thread_mdelay(10);
            ticket_ctx.remaining = observed - 1;
            record_ticket_sale(worker->window, observed);
        }

        rt_thread_mdelay((worker->window == 'A') ? 12 : 17);
    }

    rt_sem_release(ticket_ctx.done);
}

static rt_err_t run_ticket_phase(rt_bool_t use_mutex)
{
    rt_thread_t workers[2] = {RT_NULL};
    const char *names[2];
    rt_uint8_t index;

    ticket_ctx.remaining = INITIAL_TICKET_COUNT;
    ticket_ctx.sold_count = 0;
    ticket_ctx.duplicate_count = 0;
    rt_memset(ticket_ctx.sold_map, 0, sizeof(ticket_ctx.sold_map));

    names[0] = use_mutex ? "swinA" : "uwinA";
    names[1] = use_mutex ? "swinB" : "uwinB";
    ticket_ctx.workers[0].window = 'A';
    ticket_ctx.workers[1].window = 'B';
    ticket_ctx.workers[0].use_mutex = use_mutex;
    ticket_ctx.workers[1].use_mutex = use_mutex;

    for (index = 0; index < 2; index++)
    {
        workers[index] = create_thread(names[index], ticket_worker_entry,
                                       &ticket_ctx.workers[index],
                                       DEMO_STACK_SIZE, DEMO_PRIORITY);
        if (workers[index] == RT_NULL)
        {
            if (index > 0)
            {
                rt_thread_delete(workers[0]);
            }
            return -RT_ENOMEM;
        }
    }

    rt_thread_startup(workers[0]);
    rt_thread_startup(workers[1]);
    rt_sem_take(ticket_ctx.done, RT_WAITING_FOREVER);
    rt_sem_take(ticket_ctx.done, RT_WAITING_FOREVER);

    rt_kprintf("[%s] sales=%u, duplicates=%u, remaining=%d\n",
               use_mutex ? "with mutex" : "without mutex",
               ticket_ctx.sold_count, ticket_ctx.duplicate_count,
               ticket_ctx.remaining);
    return RT_EOK;
}

static void ticket_runner_entry(void *parameter)
{
    RT_UNUSED(parameter);
    rt_memset(&ticket_ctx, 0, sizeof(ticket_ctx));

    ticket_ctx.lock = rt_mutex_create("ticketmt", RT_IPC_FLAG_PRIO);
    ticket_ctx.done = rt_sem_create("tickdone", 0, RT_IPC_FLAG_FIFO);
    if ((ticket_ctx.lock == RT_NULL) || (ticket_ctx.done == RT_NULL))
    {
        rt_kprintf("[ticket] create IPC object failed\n");
        goto cleanup;
    }

    rt_kprintf("\n[ticket] phase 1: intentionally unsafe, no mutex\n");
    if (run_ticket_phase(RT_FALSE) != RT_EOK)
    {
        goto cleanup;
    }

    rt_kprintf("\n[ticket] phase 2: mutex protects check, decrement and record\n");
    if (run_ticket_phase(RT_TRUE) == RT_EOK)
    {
        rt_kprintf("[ticket] all tickets sold safely, remaining tickets: %d\n",
                   ticket_ctx.remaining);
    }

cleanup:
    if (ticket_ctx.done != RT_NULL)
    {
        rt_sem_delete(ticket_ctx.done);
        ticket_ctx.done = RT_NULL;
    }
    if (ticket_ctx.lock != RT_NULL)
    {
        rt_mutex_delete(ticket_ctx.lock);
        ticket_ctx.lock = RT_NULL;
    }
    demo_finish(&ticket_running);
}

static int ticket_demo(int argc, char **argv)
{
    rt_thread_t runner;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_try_start(&ticket_running) == RT_FALSE)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -RT_EBUSY;
    }

    runner = create_thread("tk_run", ticket_runner_entry, RT_NULL,
                           RUNNER_STACK_SIZE, RUNNER_PRIORITY);
    if (runner == RT_NULL)
    {
        demo_finish(&ticket_running);
        return -RT_ENOMEM;
    }
    rt_thread_startup(runner);
    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, mutex protected ticket selling demo);

/* ==================== 练习三：启动条件检查 ==================== */

#define EVENT_NET_READY          (1U << 0)
#define EVENT_SENSOR_READY       (1U << 1)
#define EVENT_STORAGE_READY      (1U << 2)
#define EVENT_ALL_READY          (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

struct startup_module_arg
{
    const char *name;
    rt_uint32_t event;
    rt_int32_t delay_ms;
};

struct startup_context
{
    rt_event_t event;
    rt_sem_t done;
    struct startup_module_arg modules[3];
};

static struct startup_context startup_ctx;
static rt_bool_t startup_running = RT_FALSE;

static void startup_module_entry(void *parameter)
{
    struct startup_module_arg *module = (struct startup_module_arg *)parameter;

    rt_thread_mdelay(module->delay_ms);
    rt_kprintf("%s ready\n", module->name);
    rt_event_send(startup_ctx.event, module->event);
}

static void startup_business_entry(void *parameter)
{
    rt_uint32_t received = 0;
    rt_uint32_t ready;
    rt_err_t result;

    RT_UNUSED(parameter);

    result = rt_event_recv(startup_ctx.event, EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           rt_tick_from_millisecond(3000), &received);
    if (result != RT_EOK)
    {
        /* 超时后读取当前事件位，仅用于报告尚未完成的模块。 */
        ready = startup_ctx.event->set;
        rt_kprintf("wait modules timeout:");
        if ((ready & EVENT_NET_READY) == 0)
        {
            rt_kprintf(" network");
        }
        if ((ready & EVENT_SENSOR_READY) == 0)
        {
            rt_kprintf(" sensor");
        }
        if ((ready & EVENT_STORAGE_READY) == 0)
        {
            rt_kprintf(" storage");
        }
        rt_kprintf(" is not ready\n");

        result = rt_event_recv(startup_ctx.event, EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_FOREVER, &received);
    }

    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }
    rt_sem_release(startup_ctx.done);
}

static void startup_runner_entry(void *parameter)
{
    rt_thread_t threads[4] = {RT_NULL};
    const char *names[4] = {"netinit", "seninit", "stoinit", "biztask"};
    rt_uint8_t created = 0;
    rt_uint8_t index;

    RT_UNUSED(parameter);
    rt_memset(&startup_ctx, 0, sizeof(startup_ctx));

    startup_ctx.event = rt_event_create("st_event", RT_IPC_FLAG_FIFO);
    startup_ctx.done = rt_sem_create("stdone", 0, RT_IPC_FLAG_FIFO);
    if ((startup_ctx.event == RT_NULL) || (startup_ctx.done == RT_NULL))
    {
        rt_kprintf("[startup] create IPC object failed\n");
        goto cleanup;
    }

    startup_ctx.modules[0].name = "network";
    startup_ctx.modules[0].event = EVENT_NET_READY;
    startup_ctx.modules[0].delay_ms = 500;
    startup_ctx.modules[1].name = "sensor";
    startup_ctx.modules[1].event = EVENT_SENSOR_READY;
    startup_ctx.modules[1].delay_ms = 1000;
    startup_ctx.modules[2].name = "storage";
    startup_ctx.modules[2].event = EVENT_STORAGE_READY;
    startup_ctx.modules[2].delay_ms = 5000;

    rt_kprintf("\n[startup] wait all modules, first timeout = 3 seconds\n");
    for (index = 0; index < 3; index++)
    {
        threads[index] = create_thread(names[index], startup_module_entry,
                                       &startup_ctx.modules[index],
                                       DEMO_STACK_SIZE, DEMO_PRIORITY);
        if (threads[index] == RT_NULL)
        {
            break;
        }
        created++;
    }
    if (created == 3)
    {
        threads[3] = create_thread(names[3], startup_business_entry, RT_NULL,
                                   DEMO_STACK_SIZE, DEMO_PRIORITY - 1);
        if (threads[3] != RT_NULL)
        {
            created++;
        }
    }

    if (created != 4)
    {
        for (index = 0; index < created; index++)
        {
            rt_thread_delete(threads[index]);
        }
        goto cleanup;
    }

    for (index = 0; index < 4; index++)
    {
        rt_thread_startup(threads[index]);
    }
    rt_sem_take(startup_ctx.done, RT_WAITING_FOREVER);

cleanup:
    if (startup_ctx.done != RT_NULL)
    {
        rt_sem_delete(startup_ctx.done);
        startup_ctx.done = RT_NULL;
    }
    if (startup_ctx.event != RT_NULL)
    {
        rt_event_delete(startup_ctx.event);
        startup_ctx.event = RT_NULL;
    }
    demo_finish(&startup_running);
}

static int startup_demo(int argc, char **argv)
{
    rt_thread_t runner;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_try_start(&startup_running) == RT_FALSE)
    {
        rt_kprintf("startup_demo is already running\n");
        return -RT_EBUSY;
    }

    runner = create_thread("st_run", startup_runner_entry, RT_NULL,
                           RUNNER_STACK_SIZE, RUNNER_PRIORITY);
    if (runner == RT_NULL)
    {
        demo_finish(&startup_running);
        return -RT_ENOMEM;
    }
    rt_thread_startup(runner);
    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, event set startup dependency demo);

/* ==================== 练习四：快递分拣中心 ==================== */

#define PACKAGE_PER_SENDER       5
#define PACKAGE_END_ID           0

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct package_sender_arg
{
    rt_uint8_t sender_id;
};

struct package_context
{
    rt_mq_t queue;
    rt_sem_t sender_done;
    rt_sem_t sorter_done;
    rt_uint32_t send_failed;
    struct package_sender_arg senders[2];
};

static struct package_context package_ctx;
static rt_bool_t package_running = RT_FALSE;

static const char *package_region_name(rt_uint8_t region)
{
    switch (region)
    {
    case 1:
        return "east";
    case 2:
        return "south";
    case 3:
        return "north";
    default:
        return "unknown";
    }
}

static void package_sender_entry(void *parameter)
{
    struct package_sender_arg *sender = (struct package_sender_arg *)parameter;
    struct package_msg message;
    rt_uint8_t index;

    for (index = 1; index <= PACKAGE_PER_SENDER; index++)
    {
        message.id = (rt_uint32_t)sender->sender_id * 1000U + index;
        message.region = (rt_uint8_t)(((sender->sender_id + index) % 3U) + 1U);
        message.weight = (rt_uint16_t)(650U + sender->sender_id * 100U + index * 90U);

        while (rt_mq_send(package_ctx.queue, &message, sizeof(message)) != RT_EOK)
        {
            rt_enter_critical();
            package_ctx.send_failed++;
            rt_exit_critical();
            rt_kprintf("sender%u queue full, retry package %lu\n",
                       sender->sender_id, (unsigned long)message.id);
            rt_thread_mdelay(80);
        }

        rt_kprintf("sender%u accepted package %lu\n",
                   sender->sender_id, (unsigned long)message.id);
        rt_thread_mdelay((sender->sender_id == 1) ? 35 : 50);
    }

    rt_sem_release(package_ctx.sender_done);
}

static void package_sorter_entry(void *parameter)
{
    struct package_msg message;
    rt_uint32_t total = 0;
    rt_uint32_t region_count[3] = {0, 0, 0};
    rt_uint32_t total_weight = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        if (rt_mq_recv(package_ctx.queue, &message, sizeof(message),
                       RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        if (message.id == PACKAGE_END_ID)
        {
            break;
        }

        total++;
        if ((message.region >= 1) && (message.region <= 3))
        {
            region_count[message.region - 1]++;
        }
        total_weight += message.weight;
        rt_kprintf("package %lu -> %s, weight: %u g\n",
                   (unsigned long)message.id,
                   package_region_name(message.region), message.weight);

        /* 分拣速度略慢，用于制造队列满的可恢复场景。 */
        rt_thread_mdelay(120);
    }

    rt_kprintf("[package] total packages: %u\n", total);
    rt_kprintf("[package] east: %u, south: %u, north: %u\n",
               region_count[0], region_count[1], region_count[2]);
    rt_kprintf("[package] total weight: %u g\n", total_weight);
    rt_kprintf("[package] send failed and retried: %u\n",
               package_ctx.send_failed);
    rt_sem_release(package_ctx.sorter_done);
}

static void package_runner_entry(void *parameter)
{
    rt_thread_t senders[2] = {RT_NULL};
    rt_thread_t sorter = RT_NULL;
    struct package_msg end_message = {PACKAGE_END_ID, 0, 0};

    RT_UNUSED(parameter);
    rt_memset(&package_ctx, 0, sizeof(package_ctx));

    package_ctx.queue = rt_mq_create("packmq", sizeof(struct package_msg), 5,
                                     RT_IPC_FLAG_FIFO);
    package_ctx.sender_done = rt_sem_create("pkgsend", 0, RT_IPC_FLAG_FIFO);
    package_ctx.sorter_done = rt_sem_create("pkgdone", 0, RT_IPC_FLAG_FIFO);
    if ((package_ctx.queue == RT_NULL) ||
        (package_ctx.sender_done == RT_NULL) ||
        (package_ctx.sorter_done == RT_NULL))
    {
        rt_kprintf("[package] create IPC object failed\n");
        goto cleanup;
    }

    package_ctx.senders[0].sender_id = 1;
    package_ctx.senders[1].sender_id = 2;
    senders[0] = create_thread("sender1", package_sender_entry,
                               &package_ctx.senders[0],
                               DEMO_STACK_SIZE, DEMO_PRIORITY);
    senders[1] = create_thread("sender2", package_sender_entry,
                               &package_ctx.senders[1],
                               DEMO_STACK_SIZE, DEMO_PRIORITY);
    sorter = create_thread("sorter", package_sorter_entry, RT_NULL,
                           DEMO_STACK_SIZE, DEMO_PRIORITY - 1);

    if ((senders[0] == RT_NULL) || (senders[1] == RT_NULL) ||
        (sorter == RT_NULL))
    {
        if (senders[0] != RT_NULL)
        {
            rt_thread_delete(senders[0]);
        }
        if (senders[1] != RT_NULL)
        {
            rt_thread_delete(senders[1]);
        }
        if (sorter != RT_NULL)
        {
            rt_thread_delete(sorter);
        }
        goto cleanup;
    }

    rt_kprintf("\n[package] 2 senders, 1 sorter, queue capacity = 5\n");
    rt_thread_startup(sorter);
    rt_thread_startup(senders[0]);
    rt_thread_startup(senders[1]);

    rt_sem_take(package_ctx.sender_done, RT_WAITING_FOREVER);
    rt_sem_take(package_ctx.sender_done, RT_WAITING_FOREVER);

    /* 两个生产者完成后发送结束消息；队列满时等待并重试。 */
    while (rt_mq_send(package_ctx.queue, &end_message,
                      sizeof(end_message)) != RT_EOK)
    {
        rt_thread_mdelay(50);
    }
    rt_sem_take(package_ctx.sorter_done, RT_WAITING_FOREVER);

cleanup:
    if (package_ctx.sorter_done != RT_NULL)
    {
        rt_sem_delete(package_ctx.sorter_done);
        package_ctx.sorter_done = RT_NULL;
    }
    if (package_ctx.sender_done != RT_NULL)
    {
        rt_sem_delete(package_ctx.sender_done);
        package_ctx.sender_done = RT_NULL;
    }
    if (package_ctx.queue != RT_NULL)
    {
        rt_mq_delete(package_ctx.queue);
        package_ctx.queue = RT_NULL;
    }
    demo_finish(&package_running);
}

static int package_demo(int argc, char **argv)
{
    rt_thread_t runner;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_try_start(&package_running) == RT_FALSE)
    {
        rt_kprintf("package_demo is already running\n");
        return -RT_EBUSY;
    }

    runner = create_thread("mq_run", package_runner_entry, RT_NULL,
                           RUNNER_STACK_SIZE, RUNNER_PRIORITY);
    if (runner == RT_NULL)
    {
        demo_finish(&package_running);
        return -RT_ENOMEM;
    }
    rt_thread_startup(runner);
    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, message queue package sorting demo);
