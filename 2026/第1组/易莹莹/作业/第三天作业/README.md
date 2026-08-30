# exercise1
## 思考题
**为什么使用计数信号量，而不是互斥量？**
>答：
	1. 停车场有 3 个相同的车位，计数信号量的初始值设置为 3，表示当前有 3 个可用车位。每辆车进入时申请一个信号量，信号量减 1；车辆离开时释放信号量，信号量加 1，因此可以准确限制同时进入的车辆数量。
	2. 互斥量通常用于保护共享数据或临界区，同一时刻只能被一个线程持有，不能表示同时存在的多个车位。因此，本题适合使用计数信号量。
# exercise2
## 对比实验
### 不使用互斥量
```c
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 19 2026 21:01:53
 2006 - 2022 Copyright by RT-Thread team
msh >ticket_demo
msh >window B sold ticket 20
window A sold ticket 20
window B sold ticket 19
window A sold ticket 19
window B sold ticket 18
window A sold ticket 18
window B sold ticket 17
window A sold ticket 17
window B sold ticket 16
window A sold ticket 16
window B sold ticket 15
window A sold ticket 15
window B sold ticket 14
window A sold ticket 14
window B sold ticket 13
window A sold ticket 13
window B sold ticket 12
window A sold ticket 12
window B sold ticket 11
window A sold ticket 11
window B sold ticket 10
window A sold ticket 10
window B sold ticket 9
window A sold ticket 9
window B sold ticket 8
window A sold ticket 8
window B sold ticket 7
window A sold ticket 7
window B sold ticket 6
window A sold ticket 6
window B sold ticket 5
window A sold ticket 5
window B sold ticket 4
window A sold ticket 4
window B sold ticket 3
window A sold ticket 3
window B sold ticket 2
window A sold ticket 2
window B sold ticket 1
all tickets sold
remaining tickets: 0
window A sold ticket 1
all tickets sold
remaining tickets: 0
```
### 使用互斥量
```c
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 19 2026 21:01:53
 2006 - 2022 Copyright by RT-Thread team
msh >list_thread
thread   pri  status      sp     stack size max used left tick  error
-------- ---  ------- ---------- ----------  ------  ---------- ---
tshell    20  running 0x000000d0 0x00001000    15%   0x00000002 OK
tidle0    31  ready   0x0000005c 0x00000100    64%   0x00000013 OK
timer      4  suspend 0x0000007c 0x00000200    24%   0x00000009 OK
msh >ticket_demo
msh >window B sold ticket 20
window A sold ticket 19
window B sold ticket 18
window A sold ticket 17
list_thread
thread   pri  status      sp     stack size max used left tick  error
-------- ---  ------- ---------- ----------  ------  ---------- ---
B         20  suspend 0x000000ac 0x00000200    45%   0x00000001 OK
A         20  suspend 0x000000ac 0x00000200    45%   0x00000001 OK
tshell    20  running 0x000000d0 0x00001000    15%   0x0000000a OK
tidle0    31  ready   0x0000005c 0x00000100    64%   0x00000007 OK
timer      4  suspend 0x0000007c 0x00000200    24%   0x00000009 OK
msh >window B sold ticket 16
window A sold ticket 15
window B sold ticket 14
window A sold ticket 13
window B sold ticket 12
window A sold ticket 11
window B sold ticket 10
window A sold ticket 9
window B sold ticket 8
window A sold ticket 7
window B sold ticket 6
window A sold ticket 5
window B sold ticket 4
window A sold ticket 3
window B sold ticket 2
window A sold ticket 1
all tickets sold
remaining tickets: 0
list_thread
thread   pri  status      sp     stack size max used left tick  error
-------- ---  ------- ---------- ----------  ------  ---------- ---
tshell    20  running 0x000000d0 0x00001000    15%   0x00000005 OK
tidle0    31  ready   0x0000005c 0x00000100    64%   0x0000001b OK
timer      4  suspend 0x0000007c 0x00000200    24%   0x00000009 OK
```
### 思考题
**为什么只给 `ticket_count--` 加锁仍可能不安全？**
>答：
	1. 售票过程不只有 `ticket_count--`，还包括检查是否有余票、读取票号、修改余票和打印结果。如果只给减 1 操作加锁，两个线程仍可能同时读取到相同的余票值，导致得到相同的票号，或者一个线程读取到余票后，另一个线程先完成售票，造成数据判断错误。
	2. 因此，检查余票、读取票号、减少余票和记录售票结果必须放在同一个互斥量保护的临界区内，保证整个售票操作具有原子性。
# exercise3
### 思考题

**使用三个独立信号量和使用事件集有什么区别？**
>答：
	1. 三个信号量可以分别表示网络、传感器和存储模块是否完成初始化，但主业务线程需要依次等待三个信号量，代码较复杂，而且信号量获取后计数会被消耗。
	2. 事件集可以使用不同的事件位表示不同模块的状态，并通过一次 `AND` 条件等待三个事件全部到达。事件位可以同时保存多个模块的就绪状态，适合表示“多个条件是否已经满足”，代码更直观。
	3. 因此，信号量更适合表示资源数量或同步次数，事件集更适合表示多个独立条件的组合状态。

# 运行结果
```c

 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 19 2026 21:01:53
 2006 - 2022 Copyright by RT-Thread team
msh >parking_demo
msh >car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3
car2 waiting for a space
car1 waiting for a space
car2 waiting for a space
car1 waiting for a space
car3 left
car2 entered, used spaces: 3
car4 left
car1 entered, used spaces: 3
car5 left
car1 left
car2 left
car3 entered, used spaces: 1
car4 entered, used spaces: 2
car5 entered, used spaces: 3

 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 19 2026 21:01:53
 2006 - 2022 Copyright by RT-Thread team
msh >ticket_demo
msh >window B sold ticket 20
window A sold ticket 19
window B sold ticket 18
window A sold ticket 17
window B sold ticket 16
window A sold ticket 15
window B sold ticket 14
window A sold ticket 13
window B sold ticket 12
window A sold ticket 11
window B sold ticket 10
window A sold ticket 9
window B sold ticket 8
window A sold ticket 7
window B sold ticket 6
window A sold ticket 5
window B sold ticket 4
window A sold ticket 3
window B sold ticket 2
window A sold ticket 1
all tickets sold
remaining tickets: 0
startup_demo
msh >network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
package_demo
msh >package 2001 -> north, weight: 900 g
package 1001 -> south, weight: 800 g
package 2002 -> east, weight: 950 g
package 1002 -> north, weight: 850 g
package 2003 -> south, weight: 1000 g
package 1003 -> east, weight: 900 g
package 2004 -> north, weight: 1050 g
package 1004 -> south, weight: 950 g
package 2005 -> east, weight: 1100 g
package 1005 -> north, weight: 1000 g
total packages: 10
east: 3, south: 3, north: 4
total weight: 9500 g
send failed: 0
parking_demo
msh >car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3
car2 waiting for a space
car1 waiting for a space
car2 waiting for a space
car1 waiting for a space
car3 left
car2 entered, used spaces: 3
car4 left
car1 entered, used spaces: 3
car5 left
car1 left
car2 left
car3 entered, used spaces: 1
car4 entered, used spaces: 2
car5 entered, used spaces: 3
```