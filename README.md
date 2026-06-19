<div align="center">

# ⚙️ C++ Job Scheduler Service
**A robust, multi-threaded, self-healing background task orchestrator.**

[![C++20](https://img.shields.io/badge/C++-20-blue.svg?style=for-the-badge&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Crow API](https://img.shields.io/badge/API-Crow-black.svg?style=for-the-badge&logo=c%2B%2B)](https://crowcpp.org/)
[![SQLite3](https://img.shields.io/badge/DB-SQLite3-003B57.svg?style=for-the-badge&logo=sqlite)](https://www.sqlite.org/)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg?style=for-the-badge&logo=cmake)](https://cmake.org/)

</div>

<br/>

## 🚀 Overview
The **Job Scheduler Service** is a lightweight, production-grade background execution engine. It acts as a modern, API-driven replacement for Linux `cron`. Simply hit the REST API to schedule terminal commands to run once, on an interval, or via complex cron expressions. 

It executes your commands safely in a background thread pool, logs standard output, and automatically retries failures using exponential backoff.

---

## ✨ Enterprise-Grade Features

- ⏱️ **Triple Scheduling Modes:** Support for `once` (exact time), `interval` (every X seconds), and `cron` (e.g., `0 12 * * 1-5`).
- 🧵 **Non-Blocking Thread Pool:** Execute up to *N* jobs simultaneously with zero CPU waste using `std::condition_variable` dispatching.
- 🛡️ **Self-Healing & Crash Recovery:** Automatically detects and rescues jobs that were orphaned during sudden server power loss.
- 🔁 **Exponential Backoff:** Intelligent retry policies that multiply wait times between failures to prevent system spamming.
- 📜 **Granular Execution Auditing:** Every job logs its exact millisecond duration, terminal exit code, `stdout`, and `stderr`.
- 🧠 **Smart Pausing:** Pausing and resuming jobs automatically recalculates the future schedule to prevent instant execution backlogs.
- 🔒 **Thread & SQL Safe:** Fully parameterized SQLite queries prevent SQL injection, wrapped in strict Mutex locks for parallel API access.

---

## 🛠️ Technology Stack
* **Language:** `C++20`
* **Web Framework:** `Crow` (Fast microframework for C++)
* **Persistence:** `SQLite3` (Embedded, zero-config database)
* **Concurrency:** `std::thread`, `std::mutex`, `std::condition_variable`
* **Build System:** `CMake`

---

## ⚙️ Quick Start

### 1. Install Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libsqlite3-dev
```

### 2. Build the Service
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 3. Run the Server
```bash
./JobScheduler
```
*The server will start listening on `http://0.0.0.0:18080` and the ThreadPool will spool up in the background.*

---

## 📖 API Documentation (cURL Examples)

### Create an Interval Job
Runs an echo command every 30 seconds with a 3-retry policy:
```bash
curl -s -X POST http://localhost:18080/jobs \
  -H "Content-Type: application/json" \
  -d '{
    "name": "backup-script",
    "command": "echo Starting backup...",
    "type": "interval",
    "intervalSeconds": 30,
    "retryPolicy": 3,
    "retryDelaySeconds": 5
  }'
```

### Create a Cron Job
Runs a script at 8:00 AM every Monday to Friday:
```bash
curl -s -X POST http://localhost:18080/jobs \
  -H "Content-Type: application/json" \
  -d '{
    "name": "daily-report",
    "command": "python3 /scripts/report.py",
    "type": "cron",
    "cronExpression": "0 8 * * 1-5"
  }'
```

### Get Job History / Logs
View the exact terminal output and duration of every execution attempt:
```bash
curl -s http://localhost:18080/jobs/1/executions
```

### Pause / Resume a Job
```bash
curl -s -X POST http://localhost:18080/jobs/1/pause
curl -s -X POST http://localhost:18080/jobs/1/resume
```

### Reset an Exhausted Job
Reactivate a job that failed too many times and was permanently disabled:
```bash
curl -s -X POST http://localhost:18080/jobs/1/reset
```

### Check Scheduler Health
Monitor thread pool saturation and active worker count:
```bash
curl -s http://localhost:18080/scheduler/status
```

---

## 📂 Project Architecture

```text
src/
├── main.cpp                 # Crow API Routes & Server Config
├── db/
│   └── Database.h           # SQLite3 Connection Manager
├── models/
│   └── Job.h                # Job Data Structures
├── repository/
│   └── JobRepository.h      # SQL Queries & Thread-Safe DB Access
├── services/
│   ├── JobService.h         # Core Business Logic & Validation
│   ├── JobExecutor.h        # Shell Execution via popen()
│   ├── SchedulerEngine.h    # Polling Loop & State Transitions
│   └── ThreadPool.h         # Parallel Worker Thread Management
└── utils/
    ├── TimeManager.h        # ISO 8601 & Local Time Handling
    └── CronParser.h         # Standard Cron String Parsing
```

---
<div align="center">
<i>Engineered with precision for modern backend infrastructure.</i>
</div>
