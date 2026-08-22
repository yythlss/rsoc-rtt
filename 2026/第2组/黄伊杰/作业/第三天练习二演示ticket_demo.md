Microsoft Windows [版版本本 10.0.22631.3880]
(c) Microsoft Corporation。。保保留留所所有有权权利利。。

C:\Users\hyj18>D:/RT-ThreadStudio/plugins/org.rt-thread.studio.serial.putty_1.0.5/putty/plink.exe -serial COM5 -sercfg 115
200,8,n,1,N

 \ | /
- RT -     Thread Op
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 19 2026 21:22:14
  2006 - 2022 Copyright by RT-Thread team
 msh >ticket_demo
 === with mutex ===
 sale 20 tickets by 2 windows
 window B sold ticket 20
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
 window B exits
 all tickets sold
 remaining tickets: 0
 window A exits
 msh >ticket_race
 === WITHOUT mutex (race condition) ===
 window B sold ticket 20
  (race) remaining now 19
 window A sold ticket 20
  (race) remaining now 18
 window B sold ticket 18
  (race) remaining now 17
 window A sold ticket 18
  (race) remaining now 16
 window B sold ticket 16
  (race) remaining now 15
 window A sold ticket 16
  (race) remaining now 14
 window B sold ticket 14
  (race) remaining now 13
 window A sold ticket 14
  (race) remaining now 12
 window B sold ticket 12
  (race) remaining now 11
 window A sold ticket 12
  (race) remaining now 10
 window B sold ticket 10
  (race) remaining now 9
 window A sold ticket 10
  (race) remaining now 8
 window B sold ticket 8
  (race) remaining now 7
 window A sold ticket 8
  (race) remaining now 6
 window B sold ticket 6
  (race) remaining now 5
 window A sold ticket 6
  (race) remaining now 4
 window B sold ticket 4
  (race) remaining now 3
 window A sold ticket 4
 window B sold ticket 2
  (race) remaining now 1
 window A sold ticket 2
  (race) remaining now 0
 all tickets sold
 remaining tickets: 0
 window B exits
 all tickets sold
 remaining tickets: 0
 window A exits
 msh >