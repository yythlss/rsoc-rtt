/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     RT-Thread    first version
 */

#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>


#define DEMO_STACK_SIZE 512
#define DEMO_PRIORITY 20
#define DEMO_TIMESLICE 5

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define ALL_MODULES_READY    (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* Exercise 1: parking spaces */
#define CAR_COUNT 5
#define PARKING_SPACE_COUNT 3
static struct rt_semaphore parking_sem;
static struct rt_thread car_threads[CAR_COUNT];
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t car_stacks[CAR_COUNT][DEMO_STACK_SIZE];
static volatile rt_uint8_t parking_used;
static volatile rt_bool_t parking_running;

static void car_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)(rt_ubase_t)parameter;

    while (1)
    {
        if (rt_sem_take(&parking_sem, rt_tick_from_millisecond(2000)) != RT_EOK)
        {
            rt_kprintf("car%u waiting for a space\n", (unsigned int)car_id);
            rt_thread_mdelay(1000);
            continue;
        }

        parking_used++;
        rt_kprintf("car%u entered, used spaces: %u\n",
                   (unsigned int)car_id, (unsigned int)parking_used);
        rt_thread_mdelay(3000 + car_id * 1000);
        parking_used--;
        rt_kprintf("car%u left\n", (unsigned int)car_id);
        rt_sem_release(&parking_sem);
        rt_thread_mdelay(10000);
    }
}

static int parking_demo(int argc, char *argv[])
{
    rt_err_t result;
    rt_uint32_t i;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    if (parking_running)
        return -RT_EBUSY;

    parking_used = 0;
    result = rt_sem_init(&parking_sem, "parking", PARKING_SPACE_COUNT,
                         RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;
    parking_running = RT_TRUE;

    for (i = 0; i < CAR_COUNT; i++)
    {
        result = rt_thread_init(&car_threads[i], "car", car_entry,
                                (void *)(rt_ubase_t)(i + 1), car_stacks[i],
                                sizeof(car_stacks[i]), DEMO_PRIORITY,
                                DEMO_TIMESLICE);
        if (result != RT_EOK)
        {
            parking_running = RT_FALSE;
            rt_sem_detach(&parking_sem);
            return result;
        }
        result = rt_thread_startup(&car_threads[i]);
        if (result != RT_EOK)
        {
            parking_running = RT_FALSE;
            rt_sem_detach(&parking_sem);
            return result;
        }
    }
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, start parking demo);

/* Exercise 2: ticket windows */
#define WINDOW_COUNT 2
#define TOTAL_TICKETS 20
static struct rt_mutex ticket_mutex;
static struct rt_thread window_threads[WINDOW_COUNT];
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t window_stacks[WINDOW_COUNT][DEMO_STACK_SIZE];
static rt_int32_t ticket_count;
static rt_int32_t sold_tickets[TOTAL_TICKETS];
static volatile rt_uint8_t ticket_done_count;
static volatile rt_bool_t ticket_running;

static void window_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;
    rt_int32_t ticket_number;

    while (1)
    {
        if (rt_mutex_take(&ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
            break;
        if (ticket_count <= 0)
        {
            rt_mutex_release(&ticket_mutex);
            break;
        }
        ticket_number = ticket_count--;
        sold_tickets[TOTAL_TICKETS - ticket_number] = ticket_number;
        rt_kprintf("window %s sold ticket %d\n", window_name, ticket_number);
        rt_mutex_release(&ticket_mutex);
        rt_thread_mdelay(100);
    }

    rt_enter_critical();
    ticket_done_count++;
    if (ticket_done_count == WINDOW_COUNT)
    {
        ticket_running = RT_FALSE;
        rt_exit_critical();
        rt_mutex_detach(&ticket_mutex);
        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %d\n", ticket_count);
    }
    else
    {
        rt_exit_critical();
    }
}

static int ticket_demo(int argc, char *argv[])
{
    static const char *names[WINDOW_COUNT] = {"A", "B"};
    rt_err_t result;
    rt_uint32_t i;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    if (ticket_running)
        return -RT_EBUSY;
    ticket_count = TOTAL_TICKETS;
    ticket_done_count = 0;
    result = rt_mutex_init(&ticket_mutex, "ticket", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;
    ticket_running = RT_TRUE;

    for (i = 0; i < WINDOW_COUNT; i++)
    {
        result = rt_thread_init(&window_threads[i], names[i], window_entry,
                                (void *)names[i], window_stacks[i],
                                sizeof(window_stacks[i]), DEMO_PRIORITY,
                                DEMO_TIMESLICE);
        if (result != RT_EOK)
            return result;
        result = rt_thread_startup(&window_threads[i]);
        if (result != RT_EOK)
            return result;
    }
    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, start ticket demo);

/* Exercise 3: startup conditions */
#define MODULE_COUNT 3
static struct rt_event module_event;
static struct rt_thread module_threads[MODULE_COUNT + 1];
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t module_stacks[MODULE_COUNT + 1][DEMO_STACK_SIZE];
static volatile rt_uint32_t ready_events;
static volatile rt_bool_t startup_running;

static void module_entry(void *parameter)
{
    rt_uint32_t event = (rt_uint32_t)(rt_ubase_t)parameter;
    const char *name;
    rt_int32_t delay;

    if (event == EVENT_NET_READY)
    {
        name = "network";
        delay = 1000;
    }
    else if (event == EVENT_SENSOR_READY)
    {
        name = "sensor";
        delay = 2000;
    }
    else
    {
        name = "storage";
        delay = 5000;
    }
    rt_thread_mdelay(delay);
    rt_enter_critical();
    ready_events |= event;
    rt_exit_critical();
    if (rt_event_send(&module_event, event) != RT_EOK)
        rt_kprintf("%s event send failed\n", name);
    else
        rt_kprintf("%s ready\n", name);
}

static void business_entry(void *parameter)
{
    rt_uint32_t received;
    rt_err_t result;
    RT_UNUSED(parameter);

    result = rt_event_recv(&module_event, ALL_MODULES_READY, RT_EVENT_FLAG_AND,
                           rt_tick_from_millisecond(3000), &received);
    if (result == -RT_ETIMEOUT)
    {
        rt_kprintf("wait modules timeout,");
        if (!(ready_events & EVENT_NET_READY)) rt_kprintf(" network");
        if (!(ready_events & EVENT_SENSOR_READY)) rt_kprintf(" sensor");
        if (!(ready_events & EVENT_STORAGE_READY)) rt_kprintf(" storage");
        rt_kprintf(" is not ready\n");
    }
    result = rt_event_recv(&module_event, ALL_MODULES_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           RT_WAITING_FOREVER, &received);
    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }
    rt_event_detach(&module_event);
    startup_running = RT_FALSE;
}

static int startup_demo(int argc, char *argv[])
{
    static const char *names[MODULE_COUNT] = {"network", "sensor", "storage"};
    static const rt_uint32_t events[MODULE_COUNT] = {
        EVENT_NET_READY, EVENT_SENSOR_READY, EVENT_STORAGE_READY
    };
    rt_err_t result;
    rt_uint32_t i;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    if (startup_running)
        return -RT_EBUSY;
    ready_events = 0;
    result = rt_event_init(&module_event, "modules", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;
    startup_running = RT_TRUE;
    for (i = 0; i < MODULE_COUNT; i++)
    {
        result = rt_thread_init(&module_threads[i], names[i], module_entry,
                                (void *)(rt_ubase_t)events[i], module_stacks[i],
                                sizeof(module_stacks[i]), DEMO_PRIORITY,
                                DEMO_TIMESLICE);
        if (result != RT_EOK) return result;
        result = rt_thread_startup(&module_threads[i]);
        if (result != RT_EOK) return result;
    }
    result = rt_thread_init(&module_threads[MODULE_COUNT], "business",
                            business_entry, RT_NULL, module_stacks[MODULE_COUNT],
                            sizeof(module_stacks[MODULE_COUNT]), DEMO_PRIORITY,
                            DEMO_TIMESLICE);
    if (result != RT_EOK) return result;
    return rt_thread_startup(&module_threads[MODULE_COUNT]);
}
MSH_CMD_EXPORT(startup_demo, start startup demo);

/* Exercise 4: package sorting */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

#define PRODUCER_COUNT 2
#define PACKAGES_PER_PRODUCER 5
#define PACKAGE_QUEUE_CAPACITY 5
static struct rt_messagequeue package_queue;
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t package_pool[PACKAGE_QUEUE_CAPACITY * sizeof(struct package_msg)];
static struct rt_thread package_threads[PRODUCER_COUNT + 1];
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t package_stacks[PRODUCER_COUNT + 1][DEMO_STACK_SIZE];
static volatile rt_uint8_t package_done_count;
static volatile rt_uint32_t package_send_failed;
static volatile rt_bool_t package_running;

static const char *package_region_name(rt_uint8_t region)
{
    if (region == 1) return "east";
    if (region == 2) return "south";
    return "north";
}

static void package_sender_entry(void *parameter)
{
    rt_uint32_t sender = (rt_uint32_t)(rt_ubase_t)parameter;
    struct package_msg message;
    rt_uint32_t i;
    rt_bool_t last_sender = RT_FALSE;

    for (i = 0; i < PACKAGES_PER_PRODUCER; i++)
    {
        message.id = sender * 1000 + i + 1;
        message.region = (rt_uint8_t)((sender + i) % 3 + 1);
        message.weight = (rt_uint16_t)(700 + sender * 100 + i * 50);
        while (rt_mq_send(&package_queue, &message, sizeof(message)) != RT_EOK)
        {
            package_send_failed++;
            rt_thread_mdelay(20);
        }
        rt_thread_mdelay(50);
    }

    rt_enter_critical();
    package_done_count++;
    if (package_done_count == PRODUCER_COUNT)
        last_sender = RT_TRUE;
    rt_exit_critical();

    if (last_sender)
    {
        message.id = 0;
        message.region = 0;
        message.weight = 0;
        while (rt_mq_send(&package_queue, &message, sizeof(message)) != RT_EOK)
        {
            package_send_failed++;
            rt_thread_mdelay(20);
        }
    }
}

static void package_sorter_entry(void *parameter)
{
    struct package_msg message;
    rt_uint32_t total = 0;
    rt_uint32_t region_count[3] = {0, 0, 0};
    rt_uint32_t total_weight = 0;
    rt_err_t result;
    RT_UNUSED(parameter);

    while (1)
    {
        result = rt_mq_recv(&package_queue, &message, sizeof(message),
                            RT_WAITING_FOREVER);
        if (result != RT_EOK)
            continue;
        if (message.id == 0)
            break;
        total++;
        if (message.region >= 1 && message.region <= 3)
            region_count[message.region - 1]++;
        total_weight += message.weight;
        rt_kprintf("package %u -> %s, weight: %u g\n", (unsigned int)message.id,
                   package_region_name(message.region),
                   (unsigned int)message.weight);
    }
    rt_kprintf("total packages: %u\n", (unsigned int)total);
    rt_kprintf("east: %u, south: %u, north: %u\n",
               (unsigned int)region_count[0], (unsigned int)region_count[1],
               (unsigned int)region_count[2]);
    rt_kprintf("total weight: %u g\n", (unsigned int)total_weight);
    rt_kprintf("send failed: %u\n", (unsigned int)package_send_failed);
    rt_mq_detach(&package_queue);
    package_running = RT_FALSE;
}

static int package_demo(int argc, char *argv[])
{
    rt_err_t result;
    rt_uint32_t i;
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    if (package_running)
        return -RT_EBUSY;
    package_done_count = 0;
    package_send_failed = 0;
    result = rt_mq_init(&package_queue, "packages", package_pool,
                        sizeof(struct package_msg), sizeof(package_pool),
                        RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;
    package_running = RT_TRUE;
    for (i = 0; i < PRODUCER_COUNT; i++)
    {
        result = rt_thread_init(&package_threads[i], i == 0 ? "sender1" : "sender2",
                                package_sender_entry, (void *)(rt_ubase_t)(i + 1),
                                package_stacks[i], sizeof(package_stacks[i]),
                                DEMO_PRIORITY, DEMO_TIMESLICE);
        if (result != RT_EOK) return result;
        result = rt_thread_startup(&package_threads[i]);
        if (result != RT_EOK) return result;
    }
    result = rt_thread_init(&package_threads[PRODUCER_COUNT], "sorter",
                            package_sorter_entry, RT_NULL,
                            package_stacks[PRODUCER_COUNT],
                            sizeof(package_stacks[PRODUCER_COUNT]),
                            DEMO_PRIORITY, DEMO_TIMESLICE);
    if (result != RT_EOK) return result;
    return rt_thread_startup(&package_threads[PRODUCER_COUNT]);
}
MSH_CMD_EXPORT(package_demo, start package sorting demo);

int main(void)
{
    return 0;
}

/*
int main(void)
{
    int count = 1;

    while (count++)
    {
        LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
*/
