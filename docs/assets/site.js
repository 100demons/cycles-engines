/* model27 (from Modded-Cycles) — shared page behaviour: language, version, the Model:Cycles drawings and the
 * small demos of the guide. Each part runs only if its elements are on the page.
 * The flasher handles its own language switch (app.js); here it only gets the version. */
(function () {
"use strict";

const root = document.documentElement;
const page = document.body.getAttribute("data-page") || "";
const $$ = (sel, el) => Array.from((el || document).querySelectorAll(sel));
const D = window.MCDevice;
const reduced = D ? D.reduced() : false;

// ---------------------------------------------------------------------------
// Language (same storage key as the flasher)
// ---------------------------------------------------------------------------
const lang = () => (root.lang === "fr" ? "fr" : "en");

function setLang(l) {
  root.lang = l === "fr" ? "fr" : "en";
  try { localStorage.setItem("mc-lang", root.lang); } catch (e) { /* private mode */ }
  $$(".lang button[data-lang]").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.lang === root.lang)));
  renderRelease();
}

if (page !== "flasher") {
  $$(".lang button[data-lang]").forEach((b) => b.addEventListener("click", () => setLang(b.dataset.lang)));
  setLang(lang());
}

// ---------------------------------------------------------------------------
// Theme: follows the system until the moon/sun button is used, then remembers the choice
// (all pages, flasher included)
// ---------------------------------------------------------------------------
const darkMQ = window.matchMedia ? window.matchMedia("(prefers-color-scheme: dark)") : null;
const theme = () => root.getAttribute("data-theme") || (darkMQ && darkMQ.matches ? "dark" : "light");
function paintTheme() {
  const fr = root.lang === "fr", dark = theme() === "dark";
  $$(".theme-btn").forEach((b) => {
    b.setAttribute("aria-label", dark ? (fr ? "Mode clair" : "Light mode") : (fr ? "Mode sombre" : "Dark mode"));
    b.title = b.getAttribute("aria-label");
  });
}
$$(".theme-btn").forEach((b) => b.addEventListener("click", () => {
  const next = theme() === "dark" ? "light" : "dark";
  root.setAttribute("data-theme", next);
  try { localStorage.setItem("mc-theme", next); } catch (e) { /* private mode */ }
  paintTheme();
}));
if (darkMQ && darkMQ.addEventListener) darkMQ.addEventListener("change", paintTheme);
new MutationObserver(paintTheme).observe(root, { attributes: true, attributeFilter: ["lang"] });
paintTheme();

// ---------------------------------------------------------------------------
// Version (assets/release.js)
// ---------------------------------------------------------------------------
function fmtDate(iso) {
  try {
    return new Date(iso + "T12:00:00Z").toLocaleDateString(lang() === "fr" ? "fr-FR" : "en-GB",
      { day: "numeric", month: "long", year: "numeric", timeZone: "UTC" });
  } catch (e) { return iso; }
}

function renderRelease() {
  const rels = window.MC_RELEASES || [];
  const rel = rels[0];
  if (!rel) return;
  $$("[data-rel=version]").forEach((el) => { el.textContent = rel.version; });
  $$("[data-rel=date]").forEach((el) => { el.textContent = fmtDate(rel.date); el.setAttribute("datetime", rel.date); });
  $$("[data-rel=changes]").forEach((el) => {
    el.innerHTML = "";
    if (!rel.changes.length) {
      const p = document.createElement("p");
      p.textContent = lang() === "fr" ? "Première version : rien à comparer pour l'instant."
        : "First release: nothing to compare with yet.";
      el.appendChild(p);
      return;
    }
    const p = document.createElement("p");
    p.textContent = (lang() === "fr" ? "Changements depuis la " : "Changes since ") + (rels[1] ? rels[1].version : "");
    const ul = document.createElement("ul");
    rel.changes.forEach((c) => {
      const li = document.createElement("li");
      li.textContent = c[lang()] || c.en;
      ul.appendChild(li);
    });
    el.append(p, ul);
  });
}
renderRelease();

// ---------------------------------------------------------------------------
// Model:Cycles drawings. data-device="seq" plays a pattern; data-lit="func,track" lights controls;
// data-lcd sets the screen ("logo", "menu", "~long name", "LINE 1/>LINE 2");
// data-scenes="track | track,func @MUTE/>LOCKED" steps through controls and screens.
// ---------------------------------------------------------------------------
const split = (s) => (s || "").split(",").map((x) => x.trim()).filter(Boolean);
const devices = $$("[data-device]").map((host) => {
  if (!D) return null;
  const dev = D.render(host, { seq: host.dataset.device === "seq", lit: split(host.dataset.lit), lcd: host.dataset.lcd });
  if (host.dataset.scenes) dev.scenes(host.dataset.scenes, +host.dataset.ms || 1100);
  return dev;
});

// home: hovering a mod lights the controls it touches and changes the screen
const bench = devices[0];
$$("[data-hl]").forEach((a) => {
  const on = () => { if (bench) { bench.light(split(a.dataset.hl)); bench.show(a.dataset.lcd); } };
  const off = () => { if (bench) { bench.light([]); bench.show("logo"); } };
  a.addEventListener("mouseenter", on);
  a.addEventListener("focus", on);
  a.addEventListener("mouseleave", off);
  a.addEventListener("blur", off);
});

// ---------------------------------------------------------------------------
// Guide: table of contents follows the reading
// ---------------------------------------------------------------------------
(function tocSpy() {
  const links = $$(".toc a[href^='#']");
  if (!links.length || !("IntersectionObserver" in window)) return;
  const byId = {};
  links.forEach((a) => { byId[a.getAttribute("href").slice(1)] = a; });
  const spy = new IntersectionObserver((entries) => {
    entries.forEach((e) => {
      if (!e.isIntersecting) return;
      links.forEach((a) => a.removeAttribute("aria-current"));
      if (byId[e.target.id]) byId[e.target.id].setAttribute("aria-current", "true");
    });
  }, { rootMargin: "-15% 0px -75% 0px" });
  Object.keys(byId).forEach((id) => { const s = document.getElementById(id); if (s) spy.observe(s); });
})();

// ---------------------------------------------------------------------------
// Guide: 6 channels, stock vs mod
// ---------------------------------------------------------------------------
$$("[data-usb]").forEach((fig) => {
  $$("[data-usb-mode]", fig).forEach((b) => b.addEventListener("click", () => {
    fig.setAttribute("data-usb", b.dataset.usbMode);
    $$("[data-usb-mode]", fig).forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
  }));
});

// ---------------------------------------------------------------------------
// Guide: a long name scrolling in the sound browser (about 0.17 s per character and
// 0.5 s at each end, as drumkilla's browser-scroll does)
// ---------------------------------------------------------------------------
$$("[data-marquee]").forEach((el) => {
  const name = el.getAttribute("data-marquee");
  const win = +el.getAttribute("data-chars") || 12;
  const last = Math.max(0, name.length - win);
  let i = 0, wait = 3;
  const show = () => { el.textContent = name.slice(i, i + win); };
  show();
  if (reduced || !last || !D) return;
  D.loop(el, 170, () => {
    if (wait > 0) { wait--; return; }
    if (i >= last) { i = 0; wait = 3; show(); return; }
    i++;
    show();
    if (i >= last) wait = 3;
  });
});

// ---------------------------------------------------------------------------
// Guide: removing a trig. A step that has a trig loses it when the key is let go before the hold:
// 0.2 s on the stock OS, 0.5 s with the mod (notes/33). An empty step gets a trig at once.
// ---------------------------------------------------------------------------
$$("[data-hold]").forEach((box) => {
  const key = box.querySelector("[data-hold-key]");
  const fill = box.querySelector("[data-hold-fill]");
  const out = box.querySelector("[data-hold-ms]");
  const states = $$("[data-hold-state]", box);
  const STOCK = 200, MOD = 500, FULL = 1000;
  let trig = true, had = true, down = false, t0 = 0, raf = 0;
  const state = (s) => {
    box.setAttribute("data-hold-st", s);
    states.forEach((el) => { el.hidden = el.dataset.holdState !== s; });
  };
  const paint = (d) => {
    fill.style.width = (Math.min(d, FULL) * 100 / FULL).toFixed(2) + "%";
    out.textContent = (d / 1000).toFixed(2).replace(".", lang() === "fr" ? "," : ".") + " s";
  };
  function press(e) {
    if (down) return;
    if (e) e.preventDefault();
    down = true;
    had = trig;
    trig = true;
    t0 = performance.now();
    key.classList.add("down");
    key.setAttribute("aria-pressed", "true");
    state(had ? "held" : "placed");
    const step = () => {
      const d = performance.now() - t0;
      paint(d);
      if (had && d >= MOD && box.getAttribute("data-hold-st") === "held") state("shown");
      raf = down && d < FULL ? requestAnimationFrame(step) : 0;
    };
    step();
  }
  function release() {
    if (!down) return;
    down = false;
    cancelAnimationFrame(raf);
    key.classList.remove("down");
    const d = performance.now() - t0;
    paint(d);
    if (!had) return;
    if (d < MOD) trig = false;
    state(d < STOCK ? "both" : d < MOD ? "mod" : "kept");
    key.setAttribute("aria-pressed", String(trig));
  }
  key.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    try { key.setPointerCapture(e.pointerId); } catch (err) { /* pointer already gone */ }
    press(e);
  });
  key.addEventListener("pointerup", release);
  key.addEventListener("pointercancel", release);
  key.addEventListener("lostpointercapture", release);
  key.addEventListener("keydown", (e) => { if ((e.key === " " || e.key === "Enter") && !e.repeat) press(e); });
  key.addEventListener("keyup", (e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); release(); } });
  key.addEventListener("click", (e) => e.preventDefault());
  key.addEventListener("contextmenu", (e) => e.preventDefault());
  state("idle");
});

// ---------------------------------------------------------------------------
// Guide: which engines, in which order, in the MACHINES menu
// ---------------------------------------------------------------------------
$$("[data-engines]").forEach((box) => {
  const out = box.querySelector("[data-engines-out]");
  const checks = $$("input[type=checkbox]", box);
  const orig = (D ? D.MACHINES : []).slice(0, 6);
  function render() {
    const on = checks.filter((c) => c.checked).map((c) => c.value);
    out.innerHTML = orig.map((m, k) => `<li class="o"><span>${String(k + 1).padStart(2, "0")}</span>${m}</li>`)
      .concat(on.map((m, k) => `<li class="a"><span>${String(k + 7).padStart(2, "0")}</span>${m}</li>`)).join("");
  }
  checks.forEach((c) => c.addEventListener("change", render));
  render();
});

// ---------------------------------------------------------------------------
// Guide: CPU time of one audio block, measured on the hardware (notes/31 §7, notes/36):
// mix, effects and the rest ≈ 40 %, an original voice ≈ 6 %, a Syntakt voice ≈ 7 %. The governor stops dying
// voices earlier above 72 % and fades a voice out when the load stays above 86 %.
// ---------------------------------------------------------------------------
$$("[data-load]").forEach((box) => {
  const COST = { off: 0, orig: 6, syn: 7 };
  const NEXT = { off: "orig", orig: "syn", syn: "off" };
  const slots = $$("[data-slot]", box);
  const fill = box.querySelector("[data-load-fill]");
  const pct = box.querySelector("[data-load-pct]");
  const states = $$("[data-load-state]", box);
  function render() {
    const segs = [["fx", 40]].concat(slots.map((s) => s.dataset.slot).filter((s) => s !== "off").map((s) => [s, COST[s]]));
    const total = segs.reduce((a, s) => a + s[1], 0);
    fill.innerHTML = segs.map(([k, v]) => `<i class="s-${k}" style="width:${(v * 100 / 120).toFixed(3)}%"></i>`).join("");
    pct.textContent = total + " %";
    const regime = total > 100 ? "over" : total >= 86 ? "cut" : total >= 72 ? "early" : "ok";
    box.setAttribute("data-regime", regime);
    states.forEach((el) => { el.hidden = el.dataset.loadState !== regime; });
  }
  slots.forEach((s) => s.addEventListener("click", () => { s.dataset.slot = NEXT[s.dataset.slot]; render(); }));
  render();
});
})();
