#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>
#define EVENT_NET_READY (1U << 0)
#define EVENT_SENSOR_READY (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)
#define EVENT_ALL (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)
static rt_event_t boot_event; static volatile int boot_done; static rt_uint8_t boot_running;
struct boot_arg { const char *name; rt_uint32_t bit; int delay; };
static struct boot_arg boot_args[3];
static void boot_module(void *p) { struct boot_arg *a=p; rt_thread_mdelay(a->delay); rt_event_send(boot_event,a->bit); rt_kprintf("[startup] %s ready\n",a->name); {rt_base_t l=rt_hw_interrupt_disable();++boot_done;rt_hw_interrupt_enable(l);} }
static void boot_business(void *p) { rt_uint32_t got=0; (void)p; if(rt_event_recv(boot_event,EVENT_ALL,RT_EVENT_FLAG_AND,rt_tick_from_millisecond(3000),&got)!=RT_EOK){rt_kprintf("[startup] wait modules timeout, storage is not ready\n");rt_event_recv(boot_event,EVENT_ALL,RT_EVENT_FLAG_AND,RT_WAITING_FOREVER,&got);}rt_kprintf("[startup] all modules are ready\n[startup] business task started\n");{rt_base_t l=rt_hw_interrupt_disable();++boot_done;rt_hw_interrupt_enable(l);} }
static void boot_case(int storage_delay) { rt_thread_t t[4]; int i; boot_done=0; boot_event=rt_event_create("bootevt",RT_IPC_FLAG_FIFO);if(!boot_event){rt_kprintf("[startup] event create failed\n");return;} boot_args[0]=(struct boot_arg){"network",EVENT_NET_READY,500};boot_args[1]=(struct boot_arg){"sensor",EVENT_SENSOR_READY,1000};boot_args[2]=(struct boot_arg){"storage",EVENT_STORAGE_READY,storage_delay};for(i=0;i<3;i++){t[i]=rt_thread_create("bootmod",boot_module,&boot_args[i],768,18,10);if(!t[i]||rt_thread_startup(t[i])!=RT_EOK){rt_kprintf("[startup] module create/start failed\n");return;}}t[3]=rt_thread_create("bootbiz",boot_business,0,768,19,10);if(!t[3]||rt_thread_startup(t[3])!=RT_EOK){rt_kprintf("[startup] business create/start failed\n");return;}while(boot_done<4)rt_thread_mdelay(20);rt_event_delete(boot_event);boot_event=RT_NULL; }
static void startup_manager(void *p){(void)p;rt_kprintf("[startup] normal case\n");boot_case(1500);rt_kprintf("[startup] timeout case\n");boot_case(5000);{rt_base_t l=rt_hw_interrupt_disable();boot_running=0;rt_hw_interrupt_enable(l);}}
static void startup_demo(void){rt_thread_t t;rt_base_t l=rt_hw_interrupt_disable();if(boot_running){rt_hw_interrupt_enable(l);rt_kprintf("[startup] demo already running\n");return;}boot_running=1;rt_hw_interrupt_enable(l);t=rt_thread_create("bootmgr",startup_manager,0,1024,17,10);if(!t||rt_thread_startup(t)!=RT_EOK){rt_kprintf("[startup] manager create/start failed\n");l=rt_hw_interrupt_disable();boot_running=0;rt_hw_interrupt_enable(l);}}
MSH_CMD_EXPORT(startup_demo, startup event AND and timeout demo);
