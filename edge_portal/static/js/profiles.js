const $ = (id) => document.getElementById(id);
const out = $("validate_out");
const list = $("profile_list");

function log(msg) {
  out.textContent = msg;
}

async function api(path, opts = {}) {
  const r = await fetch(path, {
    headers: { "Content-Type": "application/json", ...opts.headers },
    ...opts,
  });
  if (r.status === 204) return null;
  const text = await r.text();
  let data;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = text;
  }
  if (!r.ok) {
    const err = data?.detail?.errors || data?.detail || data || r.statusText;
    throw new Error(typeof err === "string" ? err : JSON.stringify(err, null, 2));
  }
  return data;
}

function parseConfigText() {
  const raw = $("config_json").value.trim();
  if (!raw) throw new Error("JSON خالی است");
  return JSON.parse(raw);
}

$("btn_validate").onclick = async () => {
  try {
    const config = parseConfigText();
    const res = await api("/api/config/validate", {
      method: "POST",
      body: JSON.stringify(config),
    });
    log(res.ok ? "OK — معتبر" : res.errors.join("\n"));
  } catch (e) {
    log(String(e.message || e));
  }
};

$("btn_save").onclick = async () => {
  try {
    const name = $("profile_name").value.trim();
    if (!name) throw new Error("نام پروفایل لازم است");
    const config = parseConfigText();
    await api("/api/profiles", {
      method: "POST",
      body: JSON.stringify({ name, config }),
    });
    log("ذخیره شد");
    $("config_json").value = "";
    loadProfiles();
  } catch (e) {
    log(String(e.message || e));
  }
};

$("btn_import").onclick = () => $("file_import").click();
$("file_import").onchange = async (ev) => {
  const file = ev.target.files?.[0];
  if (!file) return;
  const text = await file.text();
  $("config_json").value = text;
  try {
    JSON.parse(text);
    log("فایل بارگذاری شد — Validate یا Save بزنید");
  } catch {
    log("JSON نامعتبر در فایل");
  }
  ev.target.value = "";
};

async function loadProfiles() {
  const rows = await api("/api/profiles");
  list.innerHTML = "";
  for (const p of rows) {
    const li = document.createElement("li");
    li.innerHTML = `<span><strong>${p.name}</strong> <small>#${p.id}</small></span>`;
    const actions = document.createElement("span");
    const loadBtn = document.createElement("button");
    loadBtn.textContent = "بارگذاری";
    loadBtn.onclick = async () => {
      const detail = await api(`/api/profiles/${p.id}`);
      $("profile_name").value = detail.name;
      $("config_json").value = JSON.stringify(detail.config, null, 2);
    };
    const delBtn = document.createElement("button");
    delBtn.textContent = "حذف";
    delBtn.onclick = async () => {
      if (!confirm(`حذف ${p.name}?`)) return;
      await api(`/api/profiles/${p.id}`, { method: "DELETE" });
      loadProfiles();
    };
    actions.append(loadBtn, delBtn);
    li.appendChild(actions);
    list.appendChild(li);
  }
}

loadProfiles().catch((e) => log(String(e)));
