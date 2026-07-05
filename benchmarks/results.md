# Benchmark Results & Evaluation
Generated on Sun 05 Jul 2026 08:34:02 PM IST

## Configuration
- **Duration:** 5s
- **Threads:** 4
- **Payload Size:** 64 bytes

## Scaling Sweep Results

| Connections | Server | Throughput (req/sec) | Latency p50 (us) | Latency p99 (us) | Latency Avg (us) |
|---|---|---|---|---|---|
| 200 | Thread-per-Connection | 1.02649e+06 | 191 | 264 | 194.278 |
| 200 | Event-Driven Epoll | 1.08225e+06 | 181 | 265 | 184.241 |
| 200 | async_engine (Sender/Receiver) | 1.01974e+06 | 189 | 298 | 195.565 |
| 200 | async_engine (Fixed Buffers) | 937571 | 199 | 363 | 212.75 |
| 200 | Coroutine (exec::task) | 936880 | 200 | 344 | 212.908 |
| 500 | Thread-per-Connection | 890844 | 544 | 849 | 560.637 |
| 500 | Event-Driven Epoll | 1.02835e+06 | 471 | 726 | 485.61 |
| 500 | async_engine (Sender/Receiver) | 966675 | 486 | 778 | 516.621 |
| 500 | async_engine (Fixed Buffers) | 906905 | 508 | 904 | 550.712 |
| 500 | Coroutine (exec::task) | 917001 | 505 | 821 | 544.636 |
| 1000 | Thread-per-Connection | 783423 | 1217 | 1877 | 1275.62 |
| 1000 | Event-Driven Epoll | 980397 | 982 | 1516 | 1019.19 |
| 1000 | async_engine (Sender/Receiver) | 952077 | 1013 | 1549 | 1049.54 |
| 1000 | async_engine (Fixed Buffers) | 897579 | 1049 | 1698 | 1113.29 |
| 1000 | Coroutine (exec::task) | 934872 | 1037 | 1585 | 1068.92 |
| 2000 | Thread-per-Connection | 704785 | 2787 | 3878 | 2835.91 |
| 2000 | Event-Driven Epoll | 868111 | 2240 | 3304 | 2302.2 |
| 2000 | async_engine (Sender/Receiver) | 848695 | 2296 | 3341 | 2354.94 |
| 2000 | async_engine (Fixed Buffers) | 822083 | 2354 | 3416 | 2431.39 |
| 2000 | Coroutine (exec::task) | 837641 | 2324 | 3385 | 2386.24 |
| 4000 | Thread-per-Connection | 665684 | 5931 | 7839 | 6003.13 |
| 4000 | Event-Driven Epoll | 769831 | 5160 | 6447 | 5192.05 |
| 4000 | async_engine (Sender/Receiver) | 744157 | 5340 | 6311 | 5371.5 |
| 4000 | async_engine (Fixed Buffers) | 739855 | 5360 | 6742 | 5402.45 |
| 4000 | Coroutine (exec::task) | 726003 | 5431 | 7178 | 5505.71 |
