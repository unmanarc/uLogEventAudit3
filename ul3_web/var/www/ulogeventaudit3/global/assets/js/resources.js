"use strict";

/** @brief Shared dashboard state; only one history request may be in flight. */
const resourceDashboard = {
    resolution: "3s", pending: false, timer: null, stopped: false, initialized: false,
    intervals: {"3s": 3, "1m": 60, "60m": 3600},
    components: [
        {id: "web", label: "Web", color: "#2264c5"},
        {id: "alerts", label: "Alerts", color: "#a34b13"},
        {id: "log2json", label: "Log2JSON", color: "#188363"}
    ],
    metrics: [
        {id: "cpu", title: "CPU usage", unit: "%", fields: ["processCpuPercent"], maximum: 100},
        {id: "memory", title: "Resident memory", unit: "MiB", fields: ["residentMemoryBytes"], divisor: 1048576},
        {id: "io", title: "Logical I/O", unit: "KiB/s", fields: ["processIO.readBytesPerSecond", "processIO.writeBytesPerSecond"], divisor: 1024},
        {id: "storage", title: "Storage I/O", unit: "KiB/s", fields: ["processIO.storageReadBytesPerSecond", "processIO.storageWriteBytesPerSecond"], divisor: 1024},
        {id: "threads", title: "Threads", unit: "", fields: ["threadCount"]},
        {id: "handles", title: "Open descriptors / handles", unit: "", fields: ["handleCount"]}
    ]
};

/** @brief Read a nested numeric measurement without coercing unavailable nulls to zero. */
function resourceValue(sample, field) {
    const value = field.split(".").reduce((current, key) => current?.[key], sample);
    return typeof value === "number" && Number.isFinite(value) ? value : null;
}

/** @brief Format a measurement for labels and accessible readouts. */
function resourceNumber(value, unit = "") {
    if (value === null || !Number.isFinite(value)) {
        return "Unavailable";
    }
    return value.toLocaleString(undefined, {maximumFractionDigits: 2}) + (unit ? " " + unit : "");
}

/** @brief Create an SVG element with fixed, application-owned attributes. */
function resourceSVG(tag, attributes = {}, content) {
    const element = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [name, value] of Object.entries(attributes)) {
        element.setAttribute(name, value);
    }
    if (content !== undefined) {
        element.textContent = content;
    }
    return element;
}

/** @brief Select the nearest real sample in a series for the pointer readout. */
function resourceNearest(points, timestamp) {
    return points.reduce((nearest, point) => !nearest || Math.abs(point.timestamp - timestamp) < Math.abs(nearest.timestamp - timestamp)
        ? point : nearest, null);
}

/** @brief Build aligned series using actual sample timestamps and gaps for missing values. */
function resourceChartSeries(components, metric) {
    return resourceDashboard.components.flatMap(component => {
        const history = components[component.id];
        const samples = history?.available && Array.isArray(history.samples) ? history.samples : [];
        return metric.fields.map((field, index) => ({
            label: component.label + (metric.fields.length > 1 ? (index === 0 ? " read" : " write") : ""),
            color: component.color, dashed: index > 0,
            interval: (history?.intervalSeconds || resourceDashboard.intervals[resourceDashboard.resolution]) * 1000,
            points: samples.filter(sample => Number.isFinite(sample.timestamp)).map(sample => ({
                timestamp: sample.timestamp,
                value: resourceValue(sample, field) === null ? null : resourceValue(sample, field) / (metric.divisor || 1)
            }))
        }));
    });
}

/** @brief Draw a responsive local SVG chart with shared time coordinates and a pointer readout. */
function resourceDrawChart(components, metric) {
    const svg = document.getElementById("resource-chart-" + metric.id);
    const readout = document.getElementById("resource-readout-" + metric.id);
    const series = resourceChartSeries(components, metric);
    const timestamps = series.flatMap(item => item.points.map(point => point.timestamp));
    const values = series.flatMap(item => item.points.filter(point => point.value !== null).map(point => point.value));
    const latest = timestamps.reduce((maximum, timestamp) => Math.max(maximum, timestamp), 0) || Date.now();
    const earliest = timestamps.reduce((minimum, timestamp) => Math.min(minimum, timestamp), latest);
    const start = Math.min(earliest, latest - resourceDashboard.intervals[resourceDashboard.resolution] * 1000);
    const maximum = metric.maximum || Math.max(1, values.reduce((max, value) => Math.max(max, value), 0) * 1.15);
    const plot = {left: 68, right: 644, top: 16, bottom: 196};
    const x = timestamp => plot.left + (timestamp - start) / (latest - start) * (plot.right - plot.left);
    const y = value => plot.bottom - value / maximum * (plot.bottom - plot.top);
    svg.replaceChildren(resourceSVG("title", {}, metric.title + ": Web, Alerts and Log2JSON"));
    for (let tick = 0; tick <= 4; ++tick) {
        const value = maximum * tick / 4;
        svg.append(resourceSVG("line", {x1: plot.left, x2: plot.right, y1: y(value), y2: y(value), class: "resource-gridline"}));
        svg.append(resourceSVG("text", {x: plot.left - 8, y: y(value) + 4, "text-anchor": "end"}, resourceNumber(value)));
    }
    for (let tick = 0; tick <= 2; ++tick) {
        const timestamp = start + (latest - start) * tick / 2;
        const options = resourceDashboard.resolution === "60m"
            ? {month: "short", day: "numeric", hour: "2-digit", minute: "2-digit"}
            : {hour: "2-digit", minute: "2-digit", ...(resourceDashboard.resolution === "3s" ? {second: "2-digit"} : {})};
        svg.append(resourceSVG("text", {x: x(timestamp), y: 224, "text-anchor": tick === 0 ? "start" : tick === 2 ? "end" : "middle"},
            new Date(timestamp).toLocaleString(undefined, options)));
    }
    for (const item of series) {
        let path = "", previous = null;
        for (const point of item.points) {
            if (point.value === null) {
                previous = null;
                continue;
            }
            const connected = previous && point.timestamp - previous.timestamp <= item.interval * 1.75
                && point.timestamp > previous.timestamp;
            path += (connected ? "L" : "M") + x(point.timestamp).toFixed(2) + " " + y(point.value).toFixed(2) + " ";
            previous = point;
        }
        const line = resourceSVG("path", {d: path, fill: "none", stroke: item.color, "stroke-width": 2,
            "stroke-dasharray": item.dashed ? "6 4" : "none", "vector-effect": "non-scaling-stroke"});
        line.append(resourceSVG("title", {}, item.label));
        svg.append(line);
        const last = item.points.at(-1);
        if (last?.value !== null && last?.value !== undefined) {
            svg.append(resourceSVG("circle", {cx: x(last.timestamp), cy: y(last.value), r: 3, fill: item.color}));
        }
    }
    if (values.length === 0) {
        svg.append(resourceSVG("text", {x: 356, y: 105, "text-anchor": "middle"}, "Waiting for available measurements"));
    }
    const cursor = resourceSVG("line", {x1: plot.left, x2: plot.left, y1: plot.top, y2: plot.bottom,
        class: "resource-cursor", visibility: "hidden"});
    svg.append(cursor);
    /** @brief Report the closest actual values, including missing intervals and unavailable fields. */
    function describe(timestamp) {
        readout.textContent = new Date(timestamp).toLocaleString() + " · " + series.map(item => {
            const point = resourceNearest(item.points, timestamp);
            const value = point && Math.abs(point.timestamp - timestamp) <= item.interval * 0.75 ? point.value : null;
            return item.label + ": " + resourceNumber(value, metric.unit);
        }).join(" · ");
    }
    describe(latest);
    svg.onpointermove = event => {
        const bounds = svg.getBoundingClientRect();
        const position = Math.max(plot.left, Math.min(plot.right, (event.clientX - bounds.left) / bounds.width * 660));
        cursor.setAttribute("x1", position);
        cursor.setAttribute("x2", position);
        cursor.setAttribute("visibility", "visible");
        describe(start + (position - plot.left) / (plot.right - plot.left) * (latest - start));
    };
    svg.onpointerleave = () => {
        cursor.setAttribute("visibility", "hidden");
        describe(latest);
    };
}

/** @brief Format process/host uptime without treating zero seconds as a missing counter. */
function resourceUptime(seconds) {
    if (typeof seconds !== "number" || !Number.isFinite(seconds)) {
        return "Unavailable";
    }
    return Math.floor(seconds / 86400) + "d " + Math.floor(seconds % 86400 / 3600) + "h " + Math.floor(seconds % 3600 / 60) + "m";
}

/** @brief Update per-process health and OS details; disconnected histories are never plotted. */
function resourceRender(response) {
    const components = response.components || {};
    const unavailable = [];
    for (const component of resourceDashboard.components) {
        const history = components[component.id];
        const latest = history?.available ? history.latest : null;
        const status = document.getElementById("resource-status-" + component.id);
        const stale = latest && Date.now() - latest.timestamp > history.intervalSeconds * 2000 + 5000;
        status.textContent = !history?.available ? "Disconnected" : stale ? "Stale samples" : latest ? "Sensing" : "Starting sensor";
        status.dataset.state = !history?.available || stale ? "error" : "ready";
        if (!history?.available || stale) {
            unavailable.push(component.label);
        }
        const details = document.getElementById("resource-details-" + component.id);
        details.replaceChildren();
        const fields = [
            ["PID", latest ? resourceNumber(latest.processId) : "Unavailable"],
            ["Process uptime", resourceUptime(latest?.processUptimeSeconds)],
            ["CPU", resourceNumber(resourceValue(latest, "processCpuPercent"), "%")],
            ["Resident memory", resourceNumber(latest?.residentMemoryBytes == null ? null : latest.residentMemoryBytes / 1048576, "MiB")],
            ["Peak memory", resourceNumber(latest?.peakResidentMemoryBytes == null ? null : latest.peakResidentMemoryBytes / 1048576, "MiB")],
            ["Host CPU", resourceNumber(resourceValue(latest, "systemCpuPercent"), "%")],
            ["Host RAM used", resourceNumber(resourceValue(latest, "systemMemory.usedPercent"), "%")],
            ["Host RAM total", resourceNumber(latest?.systemMemory?.totalBytes == null ? null : latest.systemMemory.totalBytes / 1073741824, "GiB")],
            ["Logical processors", resourceNumber(resourceValue(latest, "logicalProcessorCount"))],
            ["Host uptime", resourceUptime(latest?.systemUptimeSeconds)]
        ];
        for (const [label, value] of fields) {
            const term = document.createElement("dt"), description = document.createElement("dd");
            term.textContent = label;
            description.textContent = value;
            details.append(term, description);
        }
        document.getElementById("resource-samples-" + component.id).textContent = history?.available
            ? (history.samples?.length || 0) + " / " + history.capacity + " samples · latest "
                + (latest ? new Date(latest.timestamp).toLocaleString() : "pending") : "History unavailable";
    }
    for (const metric of resourceDashboard.metrics) {
        resourceDrawChart(components, metric);
    }
    document.getElementById("resource-message").textContent = unavailable.length
        ? "Unavailable or stale: " + unavailable.join(", ") + ". Other components continue updating."
        : "Live OS measurements. CPU is a percentage of total host capacity. I/O rates cover the selected interval; dashed lines show writes.";
}

/** @brief Refresh one resolution, discard obsolete responses and schedule nonoverlapping polling. */
function resourceRefresh() {
    clearTimeout(resourceDashboard.timer);
    if (resourceDashboard.stopped || resourceDashboard.pending) {
        return;
    }
    if (document.hidden) {
        resourceDashboard.timer = setTimeout(resourceRefresh, 3000);
        return;
    }
    const resolution = resourceDashboard.resolution;
    resourceDashboard.pending = true;
    let failed = false;
    auditRequest({method: "GET", resource: "resources/" + resolution}, {
        success: response => {
            if (resolution === resourceDashboard.resolution && !resourceDashboard.stopped) {
                resourceRender(response);
            }
        },
        error: xhr => {
            failed = true;
            if (resolution === resourceDashboard.resolution) {
                document.getElementById("resource-message").textContent = "Refresh failed. Displayed measurements may be stale; retrying.";
            }
            if (xhr.status === 401 || xhr.status === 403) {
                resourceDashboard.stopped = true;
                commonFunctionError(xhr);
            }
        },
        complete: () => {
            resourceDashboard.pending = false;
            if (!resourceDashboard.stopped) {
                const delay = resolution !== resourceDashboard.resolution ? 0
                    : failed ? 10000 : Math.min(60, resourceDashboard.intervals[resolution]) * 1000;
                resourceDashboard.timer = setTimeout(resourceRefresh, delay);
            }
        }
    });
}

/** @brief Initialize scoped dashboard controls after authentication, or refresh an existing view. */
function auditReady() {
    if (!document.getElementById("resource-dashboard") || !auditHasScope("stats")) {
        return;
    }
    if (!resourceDashboard.initialized) {
        resourceDashboard.initialized = true;
        document.getElementById("resource-resolution").addEventListener("change", event => {
            resourceDashboard.resolution = event.target.value;
            document.getElementById("resource-message").textContent = "Loading selected resolution…";
            resourceRefresh();
        });
        document.addEventListener("visibilitychange", () => {
            if (!document.hidden) {
                resourceRefresh();
            }
        });
        window.addEventListener("pagehide", () => {
            resourceDashboard.stopped = true;
            clearTimeout(resourceDashboard.timer);
        });
        window.addEventListener("pageshow", event => {
            if (event.persisted) {
                resourceDashboard.stopped = false;
                resourceRefresh();
            }
        });
    }
    resourceRefresh();
}
