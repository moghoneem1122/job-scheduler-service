CREATE TABLE jobs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT,
    type TEXT,
    status TEXT,
    next_run_time TEXT,
    retry_policy INTEGER,
    retry_count INTEGER
);