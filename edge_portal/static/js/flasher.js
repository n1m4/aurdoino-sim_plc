import { ESPLoader, Transport } from "https://unpkg.com/@esp-tool/esptool-js@0.5.6/bundle.js?module";

const logEl = document.getElementById("flash_log");
const profileSelect = document.getElementById("profile_select");

let port = null;
let transport = null;
const FW_VERSION = "v1";

function log(msg) {
  logEl.textContent += `${msg}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}

function clearLog() {
  logEl.textContent = "";
}

async function api(path, opts = {}) {
  const r = await fetch(path, opts);
  if (!r.ok) {
    const t = await r.text();
    throw new Error(t || r.statusText);
  }
  const ct = r.headers.get("content-type") || "";
  if (ct.includes("application/json")) return r.json();
  return r.arrayBuffer();
}

async function loadProfiles() {
  const rows = await api("/api/profiles");
  profileSelect.innerHTML = '<option value="">— انتخاب —</option>';
  for (const p of rows) {
    const opt = document.createElement("option");
    opt.value = String(p.id);
    opt.textContent = `${p.name} (#${p.id})`;
    profileSelect.appendChild(opt);
  }
}

async function getLoader() {
  if (!port) throw new Error("اول اتصال USB");
  if (!transport) {
    transport = new Transport(port, true);
  }
  const loader = new ESPLoader({
    transport,
    baudrate: 115200,
    romBaudrate: 115200,
    enableTracing: false,
  });
  await loader.main();
  return loader;
}

document.getElementById("btn_connect").onclick = async () => {
  try {
    clearLog();
    if (!("serial" in navigator)) {
      throw new Error("Web Serial پشتیبانی نمی‌شود — Chrome/Edge + HTTPS");
    }
    port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    log("پورت انتخاب شد — برای فلش دکمه firmware یا LittleFS");
  } catch (e) {
    log(String(e.message || e));
  }
};

async function fetchBin(version, name) {
  return api(`/api/firmware/${version}/${name}`);
}

document.getElementById("btn_flash_fw").onclick = async () => {
  try {
    clearLog();
    const manifest = await api(`/api/firmware/${FW_VERSION}/manifest.json`);
    const loader = await getLoader();
    const fileArray = [];
    for (const f of manifest.files) {
      log(`دانلود ${f.path} ...`);
      const data = await fetchBin(FW_VERSION, f.path);
      fileArray.push({ data, address: f.offset });
    }
    log("در حال نوشتن firmware ...");
    await loader.writeFlash({
      fileArray,
      flashSize: manifest.flashSize || "keep",
      flashMode: manifest.flashMode || "dio",
      flashFreq: manifest.flashFreq || "40m",
      eraseAll: false,
      compress: true,
      reportProgress: (_p, written, total) => {
        if (total && written % 65536 < 4096) {
          log(`progress ${Math.floor((100 * written) / total)}%`);
        }
      },
    });
    log("Firmware OK — ESP ریست می‌شود");
    await api("/api/profiles/flash-log", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ firmware_version: FW_VERSION, success: true }),
    }).catch(() => {});
    transport = null;
    try {
      await port.close();
    } catch {
      /* ignore */
    }
    port = null;
  } catch (e) {
    log(`خطا: ${e.message || e}`);
  }
};

document.getElementById("btn_flash_fs").onclick = async () => {
  try {
    clearLog();
    const pid = profileSelect.value;
    if (!pid) throw new Error("یک پروفایل انتخاب کنید");
    const detail = await api(`/api/profiles/${pid}`);
    const manifest = await api(`/api/firmware/${FW_VERSION}/manifest.json`);
    if (!manifest.littlefs?.offset) {
      throw new Error("manifest.littlefs.offset تعریف نشده");
    }

    let bin;
    try {
      log("ساخت littlefs.bin روی سرور ...");
      bin = await api("/api/config/build-littlefs", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(detail.config),
      });
    } catch (e) {
      log("build-littlefs در دسترس نیست — از فایل از پیش ساخته استفاده نمی‌شود");
      throw e;
    }

    const loader = await getLoader();
    log("در حال نوشتن LittleFS ...");
    await loader.writeFlash({
      fileArray: [{ data: bin, address: manifest.littlefs.offset }],
      flashSize: manifest.flashSize || "keep",
      flashMode: manifest.flashMode || "dio",
      flashFreq: manifest.flashFreq || "40m",
      eraseAll: false,
      compress: true,
    });
    log("LittleFS / config.json OK");
    await api("/api/profiles/flash-log", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ profile_id: Number(pid), firmware_version: FW_VERSION, success: true }),
    }).catch(() => {});
    transport = null;
    try {
      await port.close();
    } catch {
      /* ignore */
    }
    port = null;
  } catch (e) {
    log(`خطا: ${e.message || e}`);
  }
};

loadProfiles().catch((e) => log(String(e)));
