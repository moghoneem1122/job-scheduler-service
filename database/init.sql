CREATE TABLE jobs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    command TEXT DEFAULT '',
    type TEXT,
    status TEXT,
    next_run_time TEXT,
    cron_expression TEXT DEFAULT '',
    interval_seconds INTEGER DEFAULT 0,
    retry_policy INTEGER DEFAULT 0,
    retry_count INTEGER DEFAULT 0,
    retry_delay_seconds INTEGER DEFAULT 0,
    last_run_time TEXT DEFAULT '',
    last_result TEXT DEFAULT ''
);

CREATE TABLE execution_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    job_id INTEGER NOT NULL,
    status TEXT NOT NULL,
    attempt INTEGER DEFAULT 0,
    start_time TEXT NOT NULL,
    end_time TEXT,
    duration_ms REAL DEFAULT 0,
    exit_code INTEGER DEFAULT -1,
    output TEXT DEFAULT '',
    error_output TEXT DEFAULT '',
    FOREIGN KEY (job_id) REFERENCES jobs(id) ON DELETE CASCADE
);