CREATE TABLE jobs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    type TEXT,
    status TEXT,
    next_run_time TEXT,
    cron_expression TEXT DEFAULT '',
    interval_seconds INTEGER DEFAULT 0,
    retry_policy INTEGER,
    retry_count INTEGER
);