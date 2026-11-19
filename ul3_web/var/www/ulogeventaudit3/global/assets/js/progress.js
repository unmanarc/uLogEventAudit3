"use strict";

function percentageToDegrees(percentage) { return percentage / 100 * 360; }

function redrawProgressBars() {
    document.querySelectorAll(".progress[data-value]").forEach(progress => {
        const value = Math.min(100, Math.max(0, Number(progress.dataset.value) || 0));
        progress.style.setProperty("--queue-value", value + "%");
        const left = progress.querySelector(".progress-left .progress-bar");
        const right = progress.querySelector(".progress-right .progress-bar");
        // Reset both halves on every update, including when the queue becomes empty.
        if (right) right.style.transform = `rotate(${percentageToDegrees(Math.min(value, 50))}deg)`;
        if (left) left.style.transform = `rotate(${percentageToDegrees(Math.max(value - 50, 0))}deg)`;
    });
}

function fillPercentage(textId, progressId, leftId, rightId, percentage) {
    const available = Number.isFinite(percentage);
    const value = available ? Math.min(100, Math.max(0, Math.trunc(percentage))) : 0;
    document.getElementById(textId).textContent = available ? value : "—";
    const progress = document.getElementById(progressId);
    progress.dataset.value = value;
    if (available) progress.setAttribute("aria-valuenow", value);
    else progress.removeAttribute("aria-valuenow");
    progress.setAttribute("aria-valuetext", available ? `${value}% of queue capacity used` : "Queue capacity unavailable");
    progress.style.setProperty("--queue-color", value > 75 ? "#b3293b" : value > 50 ? "#ae7300" : "#0d8278");
    const color = value > 75 ? "border-danger" : value > 50 ? "border-warning" : "border-success";
    [leftId, rightId].forEach(id => {
        const half = document.getElementById(id);
        half.classList.remove("border-success", "border-warning", "border-danger");
        half.classList.add(color);
    });
}
