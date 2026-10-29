from datetime import datetime
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

from database import add_reading, get_active_alerts, get_latest_reading, get_recent_readings, init_db, set_manual_pump_state

BASE_DIR = Path(__file__).resolve().parent.parent
FRONTEND_DIR = BASE_DIR / "frontend"

app = Flask(__name__, static_folder=str(FRONTEND_DIR), static_url_path="/static")

# =====================
# Configurable alert thresholds for the backend
# =====================
LOW_LEVEL_THRESHOLD = 20.0
HIGH_LEVEL_THRESHOLD = 80.0
OVERFLOW_LEVEL_THRESHOLD = 95.0
QUALITY_THRESHOLD = 300.0
ABNORMAL_FLOW_RATE_THRESHOLD = 10.0


def evaluate_water_safety(tds=None, ph=None, turbidity=None, chlorine=None, microbial_risk="low", contamination_risk="low"):
    """Estimate whether the water is suitable for drinking based on common educational thresholds.

    Important: this is not a laboratory-grade test and cannot prove the absence of pathogens.
    It is intended for a beginner IoT project demonstration.
    """
    try:
        tds_value = float(tds) if tds is not None else 0.0
        ph_value = float(ph) if ph is not None else 7.0
        turbidity_value = float(turbidity) if turbidity is not None else 0.0
        chlorine_value = float(chlorine) if chlorine is not None else 1.0
    except (TypeError, ValueError):
        return {
            "safe_to_drink": False,
            "status": "unsafe",
            "risk_score": 100,
            "message": "Invalid water quality values. Please enter numeric readings.",
            "issues": ["INVALID_INPUT"],
            "microbial_risk": "unknown",
            "contamination_risk": "unknown",
            "note": "This educational checker is not a certified laboratory test for drinking-water safety."
        }

    issues = []
    microbial_level = str(microbial_risk or "low").strip().lower()
    contamination_level = str(contamination_risk or "low").strip().lower()

    if tds_value < 50 or tds_value > 500:
        issues.append("TDS is outside the usual drinking-water range")
    if ph_value < 6.5 or ph_value > 8.5:
        issues.append("pH is outside the safe range for drinking water")
    if turbidity_value > 5.0:
        issues.append("Turbidity is high, which may indicate suspended particles")
    if chlorine_value < 0.2 or chlorine_value > 2.0:
        issues.append("Residual chlorine is outside the expected range")
    if microbial_level in {"high", "present", "unsafe", "yes", "positive"}:
        issues.append("Possible microbial contamination or disease-causing microorganisms")
    if contamination_level in {"high", "present", "unsafe", "yes", "positive"}:
        issues.append("Possible harmful contaminant risk")

    risk_score = min(100, max(0, len(issues) * 25))

    if not issues:
        return {
            "safe_to_drink": True,
            "status": "safe",
            "risk_score": 0,
            "message": "Water appears suitable for drinking based on the entered values.",
            "issues": [],
            "microbial_risk": microbial_level,
            "contamination_risk": contamination_level,
            "note": "This is an educational estimate only. Do not rely on it as a certified drinking-water safety result."
        }

    status = "unsafe" if risk_score >= 60 else "needs_treatment"
    message = "Water is not currently safe to drink without treatment or further testing." if status == "unsafe" else "Water may need filtration, disinfection, or lab testing before use."

    return {
        "safe_to_drink": False,
        "status": status,
        "risk_score": risk_score,
        "message": message,
        "issues": issues,
        "microbial_risk": microbial_level,
        "contamination_risk": contamination_level,
        "note": "This checker is not a substitute for certified lab analysis. It is meant for a student project demonstration."
    }


@app.route("/", methods=["GET"])
def home():
    """Serve the main dashboard website for the project."""
    return send_from_directory(str(FRONTEND_DIR), "index.html")


@app.route("/dashboard", methods=["GET"])
def dashboard_page():
    """Alias route for the website dashboard."""
    return send_from_directory(str(FRONTEND_DIR), "index.html")


@app.route("/static/<path:filename>", methods=["GET"])
def serve_static(filename):
    """Serve CSS and JavaScript assets for the frontend website."""
    return send_from_directory(str(FRONTEND_DIR), filename)


@app.route("/style.css", methods=["GET"])
def legacy_style():
    return send_from_directory(str(FRONTEND_DIR), "style.css")


@app.route("/script.js", methods=["GET"])
def legacy_script():
    return send_from_directory(str(FRONTEND_DIR), "script.js")


@app.route("/api/health", methods=["GET"])
def health_check():
    """Simple health check endpoint for project startup verification."""
    return jsonify({
        "project": "IoT Smart Water Monitoring and Alert System",
        "status": "running",
        "message": "ESP32 sensor data can be posted to /api/data"
    })


@app.route("/api/data", methods=["POST"])
def receive_sensor_data():
    """Accept sensor data from the ESP32 and store it in SQLite."""
    payload = request.get_json(silent=True) or {}

    if not payload:
        return jsonify({"status": "error", "message": "No JSON payload received"}), 400

    try:
        water_level_cm = float(payload.get("water_level_cm", 0))
        water_level_percent = float(payload.get("water_level_percent", 0))
        tds = float(payload.get("tds", 0))
        flow_rate_lpm = float(payload.get("flow_rate_lpm", 0))
        total_consumption_liters = float(payload.get("total_consumption_liters", 0))
        pump_status = str(payload.get("pump_status", "OFF")).upper()
        system_status = str(payload.get("system_status", "OK")).upper()
        raw_data = payload.get("raw_data", "")
    except (TypeError, ValueError):
        return jsonify({"status": "error", "message": "The payload contains invalid numeric values"}), 400

    # Store a timestamp in ISO format for easy charting and debugging
    timestamp = payload.get("timestamp") or datetime.utcnow().isoformat(timespec="seconds") + "Z"

    alerts = []

    if water_level_percent <= LOW_LEVEL_THRESHOLD:
        alerts.append("CRITICAL_LOW_LEVEL")
    if water_level_percent >= OVERFLOW_LEVEL_THRESHOLD:
        alerts.append("OVERFLOW_HIGH_LEVEL")
    if tds > QUALITY_THRESHOLD:
        alerts.append("POOR_WATER_QUALITY")
    if flow_rate_lpm > ABNORMAL_FLOW_RATE_THRESHOLD:
        alerts.append("ABNORMAL_WATER_CONSUMPTION")
    if str(payload.get("sensor_status", "OK")).upper() == "ERROR":
        alerts.append("SENSOR_FAILURE")
    if str(payload.get("pump_error", "OK")).upper() != "OK":
        alerts.append("PUMP_ABNORMAL_CONDITION")

    alert_status = ";".join(alerts) if alerts else "NORMAL"

    reading = {
        "timestamp": timestamp,
        "water_level_cm": water_level_cm,
        "water_level_percent": water_level_percent,
        "tds": tds,
        "flow_rate_lpm": flow_rate_lpm,
        "total_consumption_liters": total_consumption_liters,
        "pump_status": pump_status,
        "alert_status": alert_status,
        "system_status": system_status,
        "raw_data": raw_data,
    }

    add_reading(reading)

    return jsonify({
        "status": "ok",
        "message": "Sensor reading saved successfully",
        "alerts": alerts,
        "alert_status": alert_status,
        "timestamp": timestamp,
    })


@app.route("/api/latest", methods=["GET"])
def latest_reading():
    """Return the newest sensor data for dashboard widgets."""
    record = get_latest_reading()
    if record is None:
        return jsonify({"status": "empty", "message": "No data available yet"})
    return jsonify(record)


@app.route("/api/history", methods=["GET"])
def history():
    """Return recent readings for charts and trends in the dashboard."""
    try:
        limit = int(request.args.get("limit", 20))
    except ValueError:
        limit = 20
    limit = max(1, min(limit, 200))
    readings = get_recent_readings(limit=limit)
    return jsonify({"count": len(readings), "readings": readings})


@app.route("/api/alerts", methods=["GET"])
def alerts():
    """Return the latest alert status and active alert list."""
    active_alerts = get_active_alerts()
    latest = get_latest_reading()
    return jsonify({
        "status": "ok",
        "alert_status": latest.get("alert_status") if latest else "NORMAL",
        "alerts": active_alerts,
    })


@app.route("/api/pump-control", methods=["POST"])
def pump_control():
    """Allow the dashboard to send manual pump ON/OFF commands."""
    payload = request.get_json(silent=True) or {}
    state = str(payload.get("state", "")).upper()

    if state not in {"ON", "OFF"}:
        return jsonify({"status": "error", "message": "State must be 'ON' or 'OFF'"}), 400

    success = set_manual_pump_state(state)
    if not success:
        return jsonify({"status": "error", "message": "No sensor data found to update"}), 404

    return jsonify({"status": "ok", "pump_state": state})


@app.route("/api/dashboard", methods=["GET"])
def dashboard_data():
    """Convenience endpoint used by the frontend to load current data and history in one call."""
    latest = get_latest_reading()
    history_data = get_recent_readings(limit=20)

    return jsonify({
        "latest": latest,
        "history": history_data,
        "alerts": get_active_alerts(),
    })


@app.route("/api/water-safety", methods=["POST"])
def water_safety_endpoint():
    """Evaluate whether water is likely safe to drink using a simple educational model."""
    payload = request.get_json(silent=True) or {}
    latest = get_latest_reading()

    result = evaluate_water_safety(
        tds=payload.get("tds", latest.get("tds") if latest else 0),
        ph=payload.get("ph", 7.0),
        turbidity=payload.get("turbidity", 1.0),
        chlorine=payload.get("chlorine", 1.0),
        microbial_risk=payload.get("microbial_risk", "low"),
        contamination_risk=payload.get("contamination_risk", "low"),
    )
    return jsonify(result)


if __name__ == "__main__":
    init_db()
    app.run(host="0.0.0.0", port=5000, debug=True)
