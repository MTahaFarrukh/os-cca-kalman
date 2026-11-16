# Linux vs Windows - system call comparison

| Category | Linux call | Linux avg (µs) | Windows call | Windows avg (µs) | Win/Linux |
|---|---|---:|---|---:|---:|
| File I/O | open | 7.237 | CreateFile | 69.144 | 9.6x |
| File I/O | read | 1.529 | ReadFile | 40.744 | 26.6x |
| File I/O | write | 0.200 | WriteFile | 15.436 | 77.2x |
| File I/O | close | 6.889 | CloseHandle | 114.261 | 16.6x |
| Process | exec | 1221.342 | CreateProcess | 38805.924 | 31.8x |
| Process | wait | 388.885 | WaitForSingleObject | 39894.589 | 102.6x |
| Memory | mmap | 0.982 | VirtualAlloc | 6.659 | 6.8x |
| Memory | munmap | 1.471 | VirtualFree | 6.487 | 4.4x |
| IPC | pipe | 3.410 | CreatePipe | 27.091 | 7.9x |
| IPC | shmget | 5.360 | CreateFileMapping | 21.756 | 4.1x |
| IPC | shmat | 3.004 | MapViewOfFile | 7.979 | 2.7x |
| Permissions | chmod | 1.436 | SetFileSecurity | 135.688 | 94.5x |
| Permissions | chown | 1.990 | SetNamedSecurityInfoA | 269.940 | 135.7x |

# Kalman filter scaling

| OS | N | Spawn (ms) | Compute (ms) | Total (ms) | Compute speedup | RMSE (m) |
|---|---:|---:|---:|---:|---:|---:|
| Linux | 1 | 1.15 | 781.23 | 783.03 | 1.00x | 0.7839 |
| Linux | 2 | 5.48 | 400.72 | 404.24 | 1.95x | 0.7839 |
| Linux | 4 | 5.51 | 276.39 | 280.15 | 2.83x | 0.7839 |
| Linux | 8 | 21.50 | 278.66 | 300.88 | 2.80x | 0.7839 |
| Windows | 1 | 24.26 | 651.19 | 675.46 | 1.00x | 0.7839 |
| Windows | 2 | 43.50 | 336.29 | 379.80 | 1.94x | 0.7839 |
| Windows | 4 | 76.88 | 238.93 | 315.83 | 2.73x | 0.7839 |
| Windows | 8 | 141.83 | 203.45 | 345.29 | 3.20x | 0.7839 |
