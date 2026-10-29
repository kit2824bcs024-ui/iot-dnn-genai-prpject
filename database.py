import os
import sqlite3

DB_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "database"))
DB_PATH = os.path.join(DB_DIR, "water_monitor.db")


def get_connection():
    """Create and return a SQLite connection for the project database."""
    os.makedirs(DB_DIR, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    """Create the required tables if they do not already exist."""
    conn = get_connection()
    try:
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                water_level_cm REAL,
                water_level_percent REAL,
                tds REAL,
                flow_rate_lpm REAL,
                total_consumption_liters REAL,
                pump_status TEXT,
                alert_status TEXT,
                system_status TEXT,
                raw_data TEXT
            )
            """
        )
        conn.commit()
    finally:
        conn.close()


def add_reading(reading):
    """Insert one sensor reading into the database."""
    conn = get_connection()
    try:
        conn.execute(
            """
            INSERT INTO readings (
                timestamp,
                water_level_cm,
                water_level_percent,
                tds,
                flow_rate_lpm,
                total_consumption_liters,
                pump_status,
                alert_status,
                system_status,
                raw_data
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                reading.get("timestamp"),
                reading.get("water_level_cm"),
                reading.get("water_level_percent"),
                reading.get("tds"),
                reading.get("flow_rate_lpm"),
                reading.get("total_consumption_liters"),
                reading.get("pump_status"),
                reading.get("alert_status"),
                reading.get("system_status"),
                reading.get("raw_data"),
            ),
        )
        conn.commit()
    finally:
        conn.close()


def get_latest_reading():
    """Return the newest sensor reading."""
    conn = get_connection()
    try:
        row = conn.execute(
            """
            SELECT * FROM readings
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()
        return dict(row) if row else None
    finally:
        conn.close()


def get_recent_readings(limit=20):
    """Return the most recent readings used for charts and dashboard widgets."""
    conn = get_connection()
    try:
        rows = conn.execute(
            """
            SELECT * FROM readings
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
        return [dict(row) for row in reversed(rows)]
    finally:
        conn.close()


def get_active_alerts():
    """Return the latest active alert list from the most recent reading."""
    latest = get_latest_reading()
    if not latest:
        return []
    alert_status = latest.get("alert_status") or "NORMAL"
    if alert_status in ("NORMAL", ""):
        return []
    return [item.strip() for item in str(alert_status).split(";") if item.strip()]


def set_manual_pump_state(state):
    """Update the latest reading with a manual pump command state."""
    conn = get_connection()
    try:
        latest = conn.execute(
            "SELECT id FROM readings ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if latest is None:
            return False
        conn.execute(
            "UPDATE readings SET pump_status = ? WHERE id = ?",
            (state, latest["id"]),
        )
        conn.commit()
        return True
    finally:
        conn.close()


if __name__ == "__main__":
    init_db()
    print(f"Database initialized at: {DB_PATH}")
