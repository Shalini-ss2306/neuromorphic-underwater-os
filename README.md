# Neuromorphic OS for Underwater Robotic Perception

**OS mini project:** A C simulation of an operating-system resource manager for an underwater robot inspecting subsea oil/gas pipelines.

## Authors
- SHALINI S — RA2511026020179
- OVIYA VS — RA2511026020148

## Demonstrated OS concepts
- Concurrent sensor producer and scheduler using POSIX threads
- Priority scheduling (priority 1 is highest; FIFO breaks ties)
- Shared queue protected by a mutex and condition variable
- Simulated battery-aware processing and power-conserve mode
- Sensor fault handling / degraded mode
- Communication-loss handling and alert status
- Timestamped CSV event log

## Run
On Linux or Windows WSL with GCC:

```bash
gcc -std=c11 -Wall -Wextra -pthread underwater_os.c -o underwater_os
./underwater_os
```

Or run `make` then `./underwater_os`.

The program writes `underwater_os_log.csv` in the current directory.

## Scope note
This is a software simulation, not a real underwater robot controller or certified pipeline detector. Sensor events are simulated. “Suspected pipeline damage” means a warning for operator review, not a confirmed leak. Battery values are simulated, so do not claim measured power savings.

## Suggested GitHub description
C/POSIX-thread simulation of an underwater inspection OS featuring priority scheduling, synchronization, battery-aware task handling, sensor-fault management, and event logging.
