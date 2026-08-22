msh >help
RT-Thread shell commands:
clear            - clear the terminal screen
version          - show RT-Thread version information
list_thread      - list thread
list_sem         - list semaphore in system
list_event       - list event in system
list_mutex       - list mutex in system
list_mailbox     - list mail box in system
list_msgqueue    - list message queue in system
list_mempool     - list memory pool in system
list_timer       - list timer in system
list_device      - list device in system
list             - list objects
help             - RT-Thread shell help.
ps               - List threads in the system.
free             - Show the memory usage in the system.
pin              - pin [option]
reboot           - Reboot System
package_demo     - package demo: message queue 2 senders 1 sorter
parking_demo     - parking demo: counting semaphore 3 lots 5 cars
startup_demo     - startup demo: event set wait 3 modules then start business
ticket_demo      - ticket demo: with mutex sell 20 tickets via 2 windows
ticket_race      - ticket demo without mutex: observe duplicate/wrong tickets

msh >parking_demo
parking: 3 lots, 5 cars, demo started
msh >car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3
car5 left, used spacecar2 entered, used spaces: 3
car4 left, ucar1 entered, used spaces: 3
car3 left, sed spaces: 2
used spaces: 2
s: 2
car4 entered, used spaces: 3
car2 left, used spaces: 2
car5 entered, used spaces: 3
car3 entered, used spaces: 3
:car4 left, used spaces: 2
car2 entered, used spaces: 3
car5 left, car1 entered, used spaces: 3
d, used spaces:car3 left, used scar4 entered, used spaces: 3
d spaces:car2 left, used spaces: 2
car5 entered, used spaces: 3
car1 left, used spaces: 2
car3 entered, used spaces: 3
car4 left, used spaces: 2
car2 entered, used spaces: 3
car5 left, used spaces: 2
car1 entered, used spaces: 3
car3 left, car4 enterd, used spaces:ed, used spaces: 3