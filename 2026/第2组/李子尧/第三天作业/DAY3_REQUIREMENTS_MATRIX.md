# Day3 Requirements Matrix

| Teacher requirement | Evidence / implementation | Status |
|---|---|---|
| Four independent MSH commands | `parking_demo`, `ticket_demo`, `startup_demo`, `package_demo` | IMPLEMENTED |
| At least two threads per exercise | Each command creates a manager plus worker threads | IMPLEMENTED |
| IPC/thread creation result checks | Every create/start path checks its return value | IMPLEMENTED |
| Parking: 3 spaces, 5 cars, 2 s timeout/retry | `applications/parking_demo.c` | IMPLEMENTED |
| Ticket unsafe/safe comparison, 20 tickets | `applications/ticket_demo.c` | IMPLEMENTED |
| Startup event AND plus timeout case | `applications/startup_demo.c` | IMPLEMENTED |
| Package MQ capacity 5, two producers and sorter | `applications/package_demo.c` | IMPLEMENTED |
| Studio clean build | Pending final build | MISSING |
| SCons build | Pending final build | MISSING |
| ST-Link flash and serial regression | Pending hardware run | MISSING |

