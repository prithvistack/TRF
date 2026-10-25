// Plain vanilla JS - no framework. Talks to the C++ server only through
// the REST API; all validation/business rules live server-side, this file
// just renders what comes back and shows error messages under forms.

const API = "/api";

// ---------------- small fetch helpers ----------------

async function apiGet(path) {
    const res = await fetch(API + path);
    const data = await res.json();
    if (!res.ok) {
        throw new Error(data.error || "Request failed");
    }
    return data;
}

async function apiPost(path, params) {
    const body = new URLSearchParams();
    for (const key in params) {
        body.append(key, params[key]);
    }
    const res = await fetch(API + path, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: body.toString(),
    });
    const data = await res.json();
    if (!res.ok) {
        throw new Error(data.error || "Request failed");
    }
    return data;
}

function showError(boxId, message) {
    const box = document.getElementById(boxId);
    box.textContent = message;
    box.classList.add("show");
}

function clearError(boxId) {
    const box = document.getElementById(boxId);
    box.textContent = "";
    box.classList.remove("show");
}

function escapeHtml(s) {
    const div = document.createElement("div");
    div.textContent = s == null ? "" : String(s);
    return div.innerHTML;
}

function statusBadge(status) {
    const cls = status === "Overdue" ? "badge-overdue" : status === "Returned" ? "badge-returned" : "badge-issued";
    return `<span class="badge ${cls}">${status}</span>`;
}

function todayIso() {
    const d = new Date();
    const mm = String(d.getMonth() + 1).padStart(2, "0");
    const dd = String(d.getDate()).padStart(2, "0");
    return `${d.getFullYear()}-${mm}-${dd}`;
}

// ---------------- tab switching ----------------

document.querySelectorAll(".tab-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
        document.querySelectorAll(".tab-btn").forEach((b) => b.classList.remove("active"));
        document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
        btn.classList.add("active");
        document.getElementById("tab-" + btn.dataset.tab).classList.add("active");
        refreshTab(btn.dataset.tab);
    });
});

function refreshTab(name) {
    if (name === "dashboard") loadDashboard();
    else if (name === "instruments") loadInstruments();
    else if (name === "members") loadMembers();
    else if (name === "lend") loadLendTab();
    else if (name === "records") loadRecords();
}

// ---------------- dashboard ----------------

async function loadDashboard() {
    const stats = await apiGet("/stats");
    document.getElementById("dashboard-today").textContent = "Today: " + stats.today;

    const grid = document.getElementById("stat-grid");
    // tone is a colour cue only (a left border + nothing else on the
    // number itself) - blue/teal for plain counts, green for "good"
    // availability, orange for stock currently out, red for overdue
    // (the only true problem state), amber for money owed.
    const boxes = [
        ["Total Instruments", stats.totalInstruments, "blue"],
        ["Units Available", stats.unitsAvailable, "green"],
        ["Units Issued", stats.unitsIssued, "orange"],
        ["Active Lendings", stats.activeLendings, "teal"],
        ["Overdue Records", stats.overdueCount, "red"],
        ["Total Fines Collected", "Rs " + stats.totalFinesCollected, "amber"],
    ];
    grid.innerHTML = boxes
        .map(
            ([label, value, tone]) =>
                `<div class="stat-box tone-${tone}"><div class="value">${escapeHtml(value)}</div><div class="label">${label}</div></div>`
        )
        .join("");

    const overdue = await apiGet("/records?status=Overdue");
    const body = document.getElementById("overdue-table-body");
    if (overdue.length === 0) {
        body.innerHTML = `<tr><td colspan="6" class="muted">No overdue items.</td></tr>`;
    } else {
        body.innerHTML = overdue
            .map(
                (r) => `<tr>
                    <td>${escapeHtml(r.id)}</td>
                    <td>${escapeHtml(r.instrumentName)}</td>
                    <td>${escapeHtml(r.memberName)}</td>
                    <td>${escapeHtml(r.dueDate)}</td>
                    <td>${escapeHtml(r.daysOverdue)}</td>
                    <td>Rs ${escapeHtml(r.fine)}</td>
                </tr>`
            )
            .join("");
    }
}

// ---------------- instruments ----------------

function instrumentDetails(inst) {
    if (inst.category === "Electronic Component") return "Voltage: " + inst.voltageRating;
    if (inst.category === "Tool") return "Training required: " + (inst.requiresTraining ? "Yes" : "No");
    if (inst.category === "Power Device") return "Capacity: " + inst.capacity;
    return "";
}

async function loadInstruments(q, category) {
    q = q || "";
    category = category || "";
    const params = new URLSearchParams({ q, category });
    const items = await apiGet("/instruments?" + params.toString());
    const body = document.getElementById("instruments-table-body");
    if (items.length === 0) {
        body.innerHTML = `<tr><td colspan="10" class="muted">No instruments found.</td></tr>`;
        return;
    }
    body.innerHTML = items
        .map(
            (inst) => `<tr>
                <td>${escapeHtml(inst.id)}</td>
                <td>${escapeHtml(inst.name)}</td>
                <td>${escapeHtml(inst.category)}</td>
                <td>${escapeHtml(inst.availableQuantity)}/${escapeHtml(inst.totalQuantity)}</td>
                <td>${escapeHtml(inst.condition)}</td>
                <td>${escapeHtml(instrumentDetails(inst))}</td>
                <td>${escapeHtml(inst.maxLendDays)}</td>
                <td>Rs ${escapeHtml(inst.finePerDay)}</td>
                <td>
                    <select data-condition-for="${escapeHtml(inst.id)}">
                        <option value="New" ${inst.condition === "New" ? "selected" : ""}>New</option>
                        <option value="Good" ${inst.condition === "Good" ? "selected" : ""}>Good</option>
                        <option value="Fair" ${inst.condition === "Fair" ? "selected" : ""}>Fair</option>
                        <option value="Damaged" ${inst.condition === "Damaged" ? "selected" : ""}>Damaged</option>
                    </select>
                    <button class="secondary" data-update-condition="${escapeHtml(inst.id)}">Update</button>
                </td>
                <td><button class="danger" data-delete-instrument="${escapeHtml(inst.id)}">Delete</button></td>
            </tr>`
        )
        .join("");

    body.querySelectorAll("[data-update-condition]").forEach((btn) => {
        btn.addEventListener("click", async () => {
            const id = btn.dataset.updateCondition;
            const select = body.querySelector(`[data-condition-for="${CSS.escape(id)}"]`);
            clearError("err-instruments-list");
            try {
                await apiPost("/instruments/condition", { id, condition: select.value });
                await loadInstruments(q, category);
            } catch (e) {
                showError("err-instruments-list", e.message);
            }
        });
    });

    body.querySelectorAll("[data-delete-instrument]").forEach((btn) => {
        btn.addEventListener("click", async () => {
            const id = btn.dataset.deleteInstrument;
            if (!confirm("Delete instrument " + id + "?")) return;
            clearError("err-instruments-list");
            try {
                await apiPost("/instruments/delete", { id });
                await loadInstruments(q, category);
            } catch (e) {
                showError("err-instruments-list", e.message);
            }
        });
    });
}

document.getElementById("ins-category").addEventListener("change", (e) => {
    const isTool = e.target.value === "Tool";
    document.getElementById("ins-extra-text-wrap").style.display = isTool ? "none" : "block";
    document.getElementById("ins-extra-checkbox-wrap").style.display = isTool ? "block" : "none";
    document.getElementById("ins-extra-label").textContent =
        e.target.value === "Power Device" ? "Capacity" : "Voltage Rating";
});

document.getElementById("form-add-instrument").addEventListener("submit", async (e) => {
    e.preventDefault();
    clearError("err-add-instrument");
    const category = document.getElementById("ins-category").value;
    const extraField =
        category === "Tool"
            ? document.getElementById("ins-extra-checkbox").checked
                ? "true"
                : "false"
            : document.getElementById("ins-extra-text").value;
    try {
        await apiPost("/instruments", {
            category,
            name: document.getElementById("ins-name").value,
            totalQuantity: document.getElementById("ins-qty").value,
            condition: document.getElementById("ins-condition").value,
            extraField,
        });
        e.target.reset();
        document.getElementById("ins-extra-text-wrap").style.display = "block";
        document.getElementById("ins-extra-checkbox-wrap").style.display = "none";
        await loadInstruments();
    } catch (err) {
        showError("err-add-instrument", err.message);
    }
});

document.getElementById("form-search-instruments").addEventListener("submit", (e) => {
    e.preventDefault();
    loadInstruments(document.getElementById("search-q").value, document.getElementById("search-category").value);
});

// The category dropdown re-filters the list the moment a category is
// picked, instead of waiting for the separate "Search" button click - the
// name search box still needs Search/Enter, but switching category is
// immediate.
document.getElementById("search-category").addEventListener("change", () => {
    loadInstruments(document.getElementById("search-q").value, document.getElementById("search-category").value);
});

// ---------------- members ----------------

async function loadMembers() {
    const items = await apiGet("/members");
    const body = document.getElementById("members-table-body");
    if (items.length === 0) {
        body.innerHTML = `<tr><td colspan="10" class="muted">No members found.</td></tr>`;
        return;
    }
    body.innerHTML = items
        .map(
            (m) => `<tr>
                <td>${escapeHtml(m.id)}</td>
                <td>${escapeHtml(m.name)}</td>
                <td>${escapeHtml(m.role)}</td>
                <td>${escapeHtml(m.rollNo)}</td>
                <td>${escapeHtml(m.branch)}</td>
                <td>${escapeHtml(m.division)}</td>
                <td>${escapeHtml(m.year)}</td>
                <td>${escapeHtml(m.position)}</td>
                <td>${escapeHtml(m.borrowLimit)}</td>
                <td><button class="danger" data-delete-member="${escapeHtml(m.id)}">Delete</button></td>
            </tr>`
        )
        .join("");

    body.querySelectorAll("[data-delete-member]").forEach((btn) => {
        btn.addEventListener("click", async () => {
            const id = btn.dataset.deleteMember;
            if (!confirm("Delete member " + id + "?")) return;
            clearError("err-members-list");
            try {
                await apiPost("/members/delete", { id });
                await loadMembers();
            } catch (e) {
                showError("err-members-list", e.message);
            }
        });
    });
}

document.getElementById("mem-iscore").addEventListener("change", (e) => {
    document.getElementById("mem-position-wrap").style.display = e.target.checked ? "block" : "none";
});

document.getElementById("form-add-member").addEventListener("submit", async (e) => {
    e.preventDefault();
    clearError("err-add-member");
    const isCore = document.getElementById("mem-iscore").checked;
    try {
        await apiPost("/members", {
            name: document.getElementById("mem-name").value,
            phone: document.getElementById("mem-phone").value,
            email: document.getElementById("mem-email").value,
            rollNo: document.getElementById("mem-roll").value,
            branch: document.getElementById("mem-branch").value,
            division: document.getElementById("mem-division").value,
            year: document.getElementById("mem-year").value,
            isCore: isCore ? "true" : "false",
            position: isCore ? document.getElementById("mem-position").value : "",
        });
        e.target.reset();
        document.getElementById("mem-position-wrap").style.display = "none";
        await loadMembers();
    } catch (err) {
        showError("err-add-member", err.message);
    }
});

// ---------------- lend (issue + return) ----------------

async function loadLendTab() {
    const [instruments, members, activeRecords, overdueRecords] = await Promise.all([
        apiGet("/instruments"),
        apiGet("/members"),
        apiGet("/records?status=Issued"),
        apiGet("/records?status=Overdue"),
    ]);
    const allActive = activeRecords.concat(overdueRecords);

    const instrumentSelect = document.getElementById("issue-instrument");
    instrumentSelect.innerHTML = instruments
        .map((i) => `<option value="${escapeHtml(i.id)}">${escapeHtml(i.id)} - ${escapeHtml(i.name)} (${i.availableQuantity} available)</option>`)
        .join("");

    const coreMembers = members.filter((m) => m.isCore);
    const lentBySelect = document.getElementById("issue-lentby");
    lentBySelect.innerHTML = coreMembers
        .map((m) => `<option value="${escapeHtml(m.id)}">${escapeHtml(m.id)} - ${escapeHtml(m.name)} (${escapeHtml(m.position)})</option>`)
        .join("");

    document.getElementById("issue-date").value = todayIso();

    const returnSelect = document.getElementById("return-record");
    if (allActive.length === 0) {
        returnSelect.innerHTML = `<option value="">No active records</option>`;
    } else {
        returnSelect.innerHTML = allActive
            .map(
                (r) =>
                    `<option value="${escapeHtml(r.id)}">${escapeHtml(r.id)} - ${escapeHtml(r.instrumentName)} - ${escapeHtml(r.memberName)} (due ${escapeHtml(r.dueDate)})</option>`
            )
            .join("");
    }
    document.getElementById("return-date").value = todayIso();
}

document.getElementById("form-issue").addEventListener("submit", async (e) => {
    e.preventDefault();
    clearError("err-issue");
    try {
        await apiPost("/issue", {
            instrumentId: document.getElementById("issue-instrument").value,
            borrowerName: document.getElementById("issue-borrower-name").value,
            borrowerSurname: document.getElementById("issue-borrower-surname").value,
            borrowerBranch: document.getElementById("issue-borrower-branch").value,
            borrowerDivision: document.getElementById("issue-borrower-division").value,
            borrowerPrn: document.getElementById("issue-borrower-prn").value,
            borrowerEmail: document.getElementById("issue-borrower-email").value,
            lentById: document.getElementById("issue-lentby").value,
            quantity: document.getElementById("issue-qty").value,
            days: document.getElementById("issue-days").value,
            issueDate: document.getElementById("issue-date").value,
        });
        e.target.reset();
        await loadLendTab();
    } catch (err) {
        showError("err-issue", err.message);
    }
});

document.getElementById("form-return").addEventListener("submit", async (e) => {
    e.preventDefault();
    clearError("err-return");
    try {
        await apiPost("/return", {
            recordId: document.getElementById("return-record").value,
            returnDate: document.getElementById("return-date").value,
            condition: document.getElementById("return-condition").value,
        });
        await loadLendTab();
    } catch (err) {
        showError("err-return", err.message);
    }
});

// ---------------- records ----------------

// The borrower's full profile, embedded server-side on each record (see
// main.cpp's toJsonArray) - null only if the Person was somehow removed
// after the record was created (shouldn't normally happen).
function borrowerInfoText(r) {
    if (!r.borrower) {
        return "-";
    }
    const parts = [];
    if (r.borrower.rollNo) parts.push("PRN: " + r.borrower.rollNo);
    if (r.borrower.branch) parts.push(r.borrower.branch);
    if (r.borrower.division) parts.push("Div " + r.borrower.division);
    return parts.length > 0 ? parts.join(" · ") : "-";
}

function hasUsableEmail(borrower) {
    return borrower && borrower.email && borrower.email !== "-";
}

// A "Remind" link for an overdue borrower with an email on file. No email
// is actually sent by this server (no SMTP client is part of this
// project's tech stack) - a mailto: link just opens the browser's own
// configured mail client with the message pre-filled, ready for a core
// member to review and send themselves.
function reminderCell(r) {
    if (r.status !== "Overdue") {
        return "-";
    }
    if (!hasUsableEmail(r.borrower)) {
        return '<span class="muted">No email on file</span>';
    }
    const subject = "TRF Robotics Club - Overdue Item Reminder";
    const body =
        `Dear ${r.borrower.name},\n\n` +
        `This is a reminder that you have not yet returned the following item borrowed from the TRF Robotics Club:\n\n` +
        `Instrument: ${r.instrumentName}\n` +
        `Due date: ${r.dueDate}\n` +
        `Days overdue: ${r.daysOverdue}\n` +
        `Current fine: Rs ${r.fine}\n\n` +
        `Please return it to a core member at your earliest convenience.\n\n` +
        `Thank you,\nTRF Robotics Club`;
    const mailto =
        `mailto:${encodeURIComponent(r.borrower.email)}` +
        `?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(body)}`;
    return `<a href="${mailto}">Remind</a>`;
}

async function loadRecords(status, member, instrument) {
    status = status || "";
    member = member || "";
    instrument = instrument || "";
    const params = new URLSearchParams({ status, member, instrument });
    const items = await apiGet("/records?" + params.toString());
    const body = document.getElementById("records-table-body");
    if (items.length === 0) {
        body.innerHTML = `<tr><td colspan="13" class="muted">No records found.</td></tr>`;
        return;
    }
    body.innerHTML = items
        .map(
            (r) => `<tr>
                <td>${escapeHtml(r.id)}</td>
                <td>${escapeHtml(r.instrumentName)}</td>
                <td>${escapeHtml(r.quantity)}</td>
                <td>${escapeHtml(r.memberName)}</td>
                <td>${escapeHtml(borrowerInfoText(r))}</td>
                <td>${escapeHtml(r.lentByName)}</td>
                <td>${escapeHtml(r.issueDate)}</td>
                <td>${escapeHtml(r.dueDate)}</td>
                <td>${escapeHtml(r.returnDate || "-")}</td>
                <td>${statusBadge(r.status)}</td>
                <td>${escapeHtml(r.daysOverdue)}</td>
                <td>Rs ${escapeHtml(r.fine)}${r.fineIsProjected && Number(r.fine) > 0 ? ' <span class="muted">(if returned today)</span>' : ""}</td>
                <td>${reminderCell(r)}</td>
            </tr>`
        )
        .join("");
}

function applyRecordFilters() {
    loadRecords(
        document.getElementById("rec-status").value,
        document.getElementById("rec-member").value,
        document.getElementById("rec-instrument").value
    );
}

document.getElementById("form-filter-records").addEventListener("submit", (e) => {
    e.preventDefault();
    applyRecordFilters();
});

// Status is a fixed dropdown (Issued/Overdue/Returned) - re-filter the
// instant one is picked, the same way the Instruments category filter
// does, instead of requiring a separate click on "Filter".
document.getElementById("rec-status").addEventListener("change", applyRecordFilters);

// ---------------- initial load ----------------

loadDashboard();
