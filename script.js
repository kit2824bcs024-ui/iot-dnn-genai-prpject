// ===============================
// SMART WATER MONITOR - SCRIPT
// ===============================

document.addEventListener("DOMContentLoaded", () => {

    // -------------------------------
    // ELEMENTS
    // -------------------------------
    const pumpOnBtn = document.getElementById("pump-on-btn");
    const pumpOffBtn = document.getElementById("pump-off-btn");

    const pumpStatusText = document.getElementById("pump-status-text");
    const pumpTag = document.getElementById("pump-tag");

    const safetyCheckBtn = document.getElementById("safety-check-btn");

    const safetyStatus = document.getElementById("safety-status");
    const safetyBadge = document.getElementById("safety-badge");
    const safetyMessage = document.getElementById("safety-message");
    const safetyResult = document.getElementById("safety-result");

    const tdsValue = document.getElementById("tds-value");
    const levelPercent = document.getElementById("level-percent");
    const levelCm = document.getElementById("level-cm");
    const levelReading = document.getElementById("level-reading");

    const flowRate = document.getElementById("flow-rate");
    const totalConsumption = document.getElementById("total-consumption");

    // -------------------------------
    // PUMP CONTROL
    // -------------------------------

    pumpOnBtn.addEventListener("click", () => {
        setPumpStatus("ON");
    });

    pumpOffBtn.addEventListener("click", () => {
        setPumpStatus("OFF");
    });

    function setPumpStatus(status) {

        if (status === "ON") {
            pumpStatusText.textContent = "ON";
            pumpTag.textContent = "RUNNING";

            pumpTag.className = "tag success";

            pumpOnBtn.classList.add("active");
            pumpOffBtn.classList.remove("active");

        } else {
            pumpStatusText.textContent = "OFF";
            pumpTag.textContent = "OFF";

            pumpTag.className = "tag";

            pumpOffBtn.classList.add("active");
            pumpOnBtn.classList.remove("active");
        }
    }


    // -------------------------------
    // WATER SAFETY CHECK
    // -------------------------------

    safetyCheckBtn.addEventListener("click", () => {

        const ph = parseFloat(
            document.getElementById("water-ph").value
        );

        const turbidity = parseFloat(
            document.getElementById("water-turbidity").value
        );

        const chlorine = parseFloat(
            document.getElementById("water-chlorine").value
        );

        const microbialRisk =
            document.getElementById("microbial-risk").value;

        const contaminationRisk =
            document.getElementById("contamination-risk").value;


        // Validate values
        if (
            isNaN(ph) ||
            isNaN(turbidity) ||
            isNaN(chlorine)
        ) {
            showSafetyResult(
                "INVALID",
                "Please enter valid water-quality values.",
                "neutral"
            );
            return;
        }


        // -------------------------------
        // SAFETY LOGIC
        // -------------------------------

        let unsafeReasons = [];

        // Educational prototype thresholds
        if (ph < 6.5 || ph > 8.5) {
            unsafeReasons.push("pH is outside the selected safe range");
        }

        if (turbidity > 5) {
            unsafeReasons.push("Turbidity is high");
        }

        if (chlorine < 0.2 || chlorine > 4) {
            unsafeReasons.push("Chlorine level is outside the selected range");
        }

        if (microbialRisk === "high") {
            unsafeReasons.push("High microbial risk detected");
        }

        if (contaminationRisk === "high") {
            unsafeReasons.push("High contamination risk detected");
        }


        // -------------------------------
        // RESULT
        // -------------------------------

        if (unsafeReasons.length === 0) {

            showSafetyResult(
                "POTENTIALLY SAFE",
                "The entered values are within the prototype's configured ranges.",
                "safe"
            );

        } else {

            showSafetyResult(
                "NOT SAFE",
                unsafeReasons.join(". ") + ".",
                "danger"
            );
        }
    });


    function showSafetyResult(status, message, type) {

        safetyStatus.textContent = status;
        safetyMessage.textContent = message;

        safetyResult.className =
            "safety-result " + type;

        safetyResult.querySelector("span").textContent =
            message;

        if (type === "safe") {

            safetyBadge.textContent = "SAFE";
            safetyBadge.className = "tag success";

        } else if (type === "danger") {

            safetyBadge.textContent = "UNSAFE";
            safetyBadge.className = "tag danger";

        } else {

            safetyBadge.textContent = "CHECK";
            safetyBadge.className = "tag warning";
        }
    }


    // -------------------------------
    // DEMO SENSOR VALUES
    // -------------------------------

    function updateDemoData() {

        const level = Math.floor(Math.random() * 40) + 50;
        const tds = Math.floor(Math.random() * 150) + 100;
        const flow = (Math.random() * 5).toFixed(2);

        levelPercent.textContent = level + "%";
        levelCm.textContent = level + " cm";

        levelReading.textContent =
            "Current water level: " + level + "%";

        tdsValue.textContent =
            tds + " ppm";

        flowRate.textContent =
            flow;

        totalConsumption.textContent =
            (parseFloat(flow) * 10).toFixed(2) + " L";
    }

    updateDemoData();

});