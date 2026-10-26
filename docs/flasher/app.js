/* Cycles Engines flasher — page logic.
 *
 * Flow: drop the official OS 1.13 file -> tick engines -> compose in the browser -> build the .syx -> flash over
 * USB (fast, or classic SysEx). No firmware is committed or uploaded; the engines are packages (engines.js, made by
 * tools/make_catalog.py) and docs/flasher/composer.js composes them, docs/flasher/builder.js builds, docs/flasher/
 * flasher.js sends. Nothing here hard-codes a machine count: the capacity is one constant (core/registrar/layout.json
 * -> frame.json), and any combination of engines composes together.
 */
(function () {
"use strict";

var E = window.MC_ENGINES, B = window.MCBuilder, C = window.MCComposer, F = window.MCFlasher;
var $ = function (id) { return document.getElementById(id); };

var state = {
  raw: null,            // Uint8Array of the official .syx
  mainOs: null,         // the MAIN OS, depacked (MCBuilder.mainOsOf)
  midi: null,           // MIDIAccess
  output: null,         // chosen MIDI output
  busy: false,
  cancelling: false,
};

// --- engine cards -------------------------------------------------------------------------------------
var ticked = [];         // engine ids, in tick order (slot 7, 8, ...)

function el(tag, cls, text) {
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function renderCards() {
  var box = $("features");
  box.textContent = "";
  E.engines.forEach(function (en) {
    var d = document.createElement("div");
    d.className = "mod";

    var row = document.createElement("label");
    row.className = "mod-row";

    var check = document.createElement("input");
    check.type = "checkbox";
    check.value = en.id;
    check.addEventListener("change", function () {
      if (check.checked) ticked.push(en.id);
      else ticked = ticked.filter(function (x) { return x !== en.id; });
      refreshSlots();
      refreshSummary();
    });
    row.appendChild(check);

    var ttl = el("span", "ttl");
    ttl.appendChild(el("span", "nm", en.card.label));
    row.appendChild(ttl);

    d.appendChild(row);

    var ext = document.createElement("div");
    ext.className = "ext";
    var det = document.createElement("div");
    det.className = "det";
    var desc = en.card.description, vid = null, mark = "\n\n[[video]]\n\n", at;
    if (en.card.video) {
      vid = document.createElement("div");
      vid.className = "vid";
      var ifr = document.createElement("iframe");
      ifr.src = "https://www.youtube-nocookie.com/embed/" + en.card.video;
      ifr.title = en.card.label + ", in video";
      ifr.loading = "lazy";
      ifr.allow = "accelerometer; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share";
      ifr.referrerPolicy = "strict-origin-when-cross-origin";
      ifr.allowFullscreen = true;
      vid.appendChild(ifr);
    }
    at = desc.indexOf(mark);
    if (vid && at >= 0) {
      det.appendChild(el("p", null, desc.slice(0, at)));
      det.appendChild(vid);
      det.appendChild(el("p", null, desc.slice(at + mark.length)));
    } else {
      det.appendChild(el("p", null, at >= 0 ? desc.split(mark).join("\n\n") : desc));
      if (vid) det.appendChild(vid);
    }
    ext.appendChild(det);
    d.appendChild(ext);

    box.appendChild(d);
  });
}

function refreshSlots() {
  var note = $("slot-note");
  if (!ticked.length) { note.hidden = true; return; }
  var slots = Object.assign({}, E.slots);
  ticked.forEach(function (id) { if (!(id in slots)) slots[id] = null; });
  var ordered = Object.keys(slots).sort(function (a, b) { return slots[a] - slots[b]; });
  var highest = Math.max.apply(null, ordered.filter(function (id) { return ticked.indexOf(id) >= 0; })
    .map(function (id) { return slots[id]; }).concat([6]));
  var parts = [];
  for (var s = 7; s <= highest; s++) {
    var id = null;
    Object.keys(slots).forEach(function (k) { if (slots[k] === s) id = k; });
    if (id && ticked.indexOf(id) >= 0) parts.push(s + " = " + id);
    else if (id) parts.push(s + " = ---- (placeholder)");
    else parts.push(s + " = ---- (placeholder)");
  }
  note.hidden = false;
  note.textContent = "Slots: " + parts.join(", ") +
    "  (a sound saved on a slot finds the same engine in any later build that includes it).";
}

function refreshSummary() {
  var s = $("summary");
  if (!state.raw || !ticked.length) { s.hidden = true; return; }
  try {
    var pkg = ticked.map(function (id) { return E.packages[id]; });
    var r = C.compose(state.mainOs, E.registrar, pkg, [], E.slots, E.placeholder);
    var budget = E.registrar.layout.end_limit - E.registrar.layout.image_end;
    s.hidden = false;
    s.textContent = "Engines: " + ticked.join(", ") + "  ·  " + r.info.packages.length +
      " packages in the build  ·  " + r.info.image + " bytes added to the image (" +
      Math.round(100 * r.info.image / budget) + "% of the " + Math.round(budget / 1024) + " KB budget)";
  } catch (e) {
    s.hidden = false;
    s.textContent = "This build can't be made: " + e.message;
  }
}

// --- OS file ------------------------------------------------------------------------------------------
function onFile(files) {
  var f = files && files[0];
  if (!f) return;
  var st = $("file-status");
  st.className = "status";
  st.textContent = "Reading " + f.name + "…";
  f.arrayBuffer().then(function (buf) {
    var raw = new Uint8Array(buf);
    try {
      var mainOs = B.mainOsOf(raw, E.device);
      state.raw = raw;
      state.mainOs = mainOs;
      st.className = "status good";
      st.textContent = "Official Model:Cycles OS 1.13, " + raw.length + " bytes. Ready.";
      refreshSummary();
      updateFlash();
    } catch (e) {
      st.className = "status bad";
      st.textContent = "Not the official OS 1.13: " + e.message;
      state.raw = null;
      state.mainOs = null;
    }
  }).catch(function (e) {
    st.className = "status bad";
    st.textContent = "Could not read the file: " + e.message;
  });
}

function bindFile() {
  var drop = $("drop"), input = $("file");
  input.addEventListener("change", function () { onFile(input.files); });
  ["dragenter", "dragover"].forEach(function (t) {
    drop.addEventListener(t, function (e) { e.preventDefault(); drop.classList.add("over"); });
  });
  ["dragleave", "drop"].forEach(function (t) {
    drop.addEventListener(t, function (e) { e.preventDefault(); drop.classList.remove("over"); });
  });
  drop.addEventListener("drop", function (e) { onFile(e.dataTransfer.files); });
  drop.addEventListener("click", function () { input.click(); });
  drop.addEventListener("keydown", function (e) {
    if (e.key === "Enter" || e.key === " ") { e.preventDefault(); input.click(); }
  });
}

// --- MIDI ---------------------------------------------------------------------------------------------
function bindMidi() {
  $("allow").addEventListener("click", function () {
    if (!navigator.requestMIDIAccess) {
      $("midi-status").className = "status bad";
      $("midi-status").textContent = "This browser has no Web MIDI (use Chrome or Edge).";
      return;
    }
    navigator.requestMIDIAccess({ sysex: true }).then(function (midi) {
      state.midi = midi;
      listPorts();
    }).catch(function (e) {
      $("midi-status").className = "status bad";
      $("midi-status").textContent = "MIDI access denied: " + e.message;
    });
  });
  $("refresh").addEventListener("click", listPorts);
  $("port").addEventListener("change", function () {
    var id = $("port").value;
    state.output = Array.from(state.midi.outputs.values()).find(function (o) { return o.id === id; }) || null;
    updateFlash();
  });
  $("m-fast").addEventListener("click", function () { selectMethod(true); });
  $("m-slow").addEventListener("click", function () { selectMethod(false); });
}

function selectMethod(fast) {
  $("m-fast").setAttribute("aria-checked", fast ? "true" : "false");
  $("m-slow").setAttribute("aria-checked", fast ? "false" : "true");
  state.fast = fast;
  var note = $("method-note");
  if (note) {
    note.textContent = fast
      ? "The update protocol of Elektron Transfer, straight from this page: about 30 seconds. At the end, the Model:Cycles asks you to confirm (YES or NO)."
      : "The firmware is sent at MIDI speed while CONFIG › UPGRADE waits for it: 5 to 10 minutes. The fallback if the fast method doesn't work.";
  }
  var hf = $("howto-fast"), hu = $("howto-usb");
  if (hf && hu) {
    hf.hidden = !fast;
    hu.hidden = fast;
  }
}

function listPorts() {
  var sel = $("port"), outs = Array.from(state.midi.outputs.values());
  sel.textContent = "";
  outs.forEach(function (o) {
    var opt = document.createElement("option");
    opt.value = o.id;
    opt.textContent = o.name;
    sel.appendChild(opt);
  });
  var st = $("midi-status");
  if (!outs.length) {
    sel.hidden = true;
    st.className = "status bad";
    st.textContent = "No MIDI output found. Connect the Model:Cycles over USB and press Refresh.";
    state.output = null;
  } else {
    sel.hidden = false;
    $("refresh").hidden = false;
    st.className = "status good";
    st.textContent = outs.length + " MIDI output" + (outs.length > 1 ? "s" : "") + ".";
    state.output = outs[0];
  }
  updateFlash();
}

// --- build + flash ------------------------------------------------------------------------------------
function updateFlash() {
  var ready = !!state.raw && ticked.length > 0 && !!state.output && $("ack").checked && !state.busy;
  $("flash").disabled = !ready;
}

$("ack").addEventListener("change", updateFlash);

function composeTweak() {
  var pkg = ticked.map(function (id) { return E.packages[id]; });
  return C.compose(state.mainOs, E.registrar, pkg, [], E.slots, E.placeholder).tweak;
}

function buildSyx() {
  return B.build(state.raw, E.device, [composeTweak()]).raw;
}

function seedFaces() {
  var cells = $("barwrap").querySelectorAll("i");
  cells.forEach(function (c) {
    var face = document.createElement("span");
    face.textContent = "💀";
    c.textContent = "";
    c.appendChild(face);
    c.setAttribute("aria-hidden", "true");
  });
}

function leds(pct) {
  var cells = $("barwrap").querySelectorAll("i");
  var lit = Math.floor((pct / 100) * cells.length + 1e-9);
  cells.forEach(function (c, k) {
    c.className = k < lit ? "on" : (k === lit && pct < 100 ? "cur" : "");
  });
}

function fmtTime(s) {
  s = Math.max(0, Math.round(s));
  var m = Math.floor(s / 60), r = s % 60;
  return m ? m + " min " + ("0" + r).slice(-2) + " s" : r + " s";
}

function flash() {
  if (state.busy) return;
  state.busy = true;
  state.cancelling = false;
  updateFlash();
  $("flash").hidden = true;
  $("stop").hidden = false;
  $("result").className = "result";
  $("result").textContent = "";
  $("progress").hidden = false;
  $("bar").style.width = "0%";
  seedFaces();
  leds(0);
  $("pct").textContent = "0 %";
  $("eta").textContent = "";
  var watch = $("watch");
  watch.className = "callout";
  watch.hidden = false;
  watch.innerHTML = (state.fast
    ? "The Model:Cycles checks every block as it arrives. At the end it asks you to confirm the update on its screen."
    : "Watch the Model:Cycles screen: it should show that it is receiving. If nothing happens within a few seconds, press <b>Stop</b>.")
    + "<br>Keep this tab in the foreground: browsers slow down background tabs.";

  var syx;
  try {
    syx = buildSyx();
  } catch (e) {
    finishFlash(false, "Build failed: " + e.message);
    return;
  }

  var pace = parseFloat($("pace").value) || 1.4;
  var total = state.fast ? F.fastSeconds(syx) : F.transferSeconds(syx, pace);
  var t0 = Date.now();
  var onProgress = function (n, tot) {
    var pct = (100 * n) / tot;
    $("bar").style.width = pct.toFixed(1) + "%";
    leds(pct);
    $("barwrap").setAttribute("aria-valuenow", pct.toFixed(0));
    $("pct").textContent = pct.toFixed(0) + " %";
    var el = (Date.now() - t0) / 1000;
    var left = n <= 0 ? total : state.fast ? (el / n) * (tot - n) : Math.max(total - el, (el / n) * (tot - n));
    $("eta").textContent = fmtTime(left) + " left";
  };

  var done = function (ok, msg) {
    if (state.cancelling) return;
    finishFlash(ok, msg);
  };

  if (state.fast) {
    fastFlash(syx, onProgress).then(function () { done(true, "Flashed."); }, function (e) { done(false, e.message); });
  } else {
    classicFlash(syx, pace, onProgress).then(function () { done(true, "Flashed."); }, function (e) { done(false, e.message); });
  }
}

function classicFlash(syx, pace, onProgress) {
  return new Promise(function (resolve, reject) {
    F.sendSysex(state.output, syx, { pace: pace, onProgress: onProgress }).then(resolve, reject);
  });
}

function fastFlash(syx, onProgress) {
  return new Promise(function (resolve, reject) {
    var input = state.midi && Array.from(state.midi.inputs.values()).find(function (i) { return true; });
    var session;
    F.xferOpen(input, state.output).then(function (s) {
      session = s;
      return F.xferIdentify(session);
    }).then(function () {
      return F.upgradeFast(session, syx, { onProgress: onProgress });
    }).then(function () { resolve(true); }, reject);
  });
}

function finishFlash(ok, msg) {
  state.busy = false;
  state.cancelling = false;
  $("progress").hidden = true;
  $("flash").hidden = false;
  $("stop").hidden = true;
  var r = $("result");
  r.className = "result " + (ok ? "good" : "bad");
  r.textContent = msg || (ok ? "Flashed." : "Failed.");
  updateFlash();
}

$("flash").addEventListener("click", flash);
$("stop").addEventListener("click", function () {
  state.cancelling = true;
  finishFlash(false, "Stopped.");
});

$("download").addEventListener("click", function (e) {
  e.preventDefault();
  if (!state.raw || !ticked.length) return;
  try {
    var syx = buildSyx();
    var blob = new Blob([syx], { type: "application/octet-stream" });
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "model-cycles_OS1.13-" + ticked.join("+") + ".syx";
    a.click();
    URL.revokeObjectURL(a.href);
  } catch (err) {
    $("result").className = "result bad";
    $("result").textContent = "Build failed: " + err.message;
  }
});

// --- credits + boot -----------------------------------------------------------------------------------
function credits() {
  var list = $("credits-list");
  var seen = {};
  E.engines.forEach(function (en) {
    var c = en.card.credit;
    if (!c || seen[c.who]) return;
    seen[c.who] = true;
    var li = el("li");
    li.textContent = c.who + (c.repo ? " — github.com/" + c.repo : "");
    list.appendChild(li);
  });
  $("build-stamp").textContent = window.MC_BUILD;
}

// --- step 1 tabs (Mods / Official firmware / Samples OS) and the machine selector -------------------------
function selectTab(id) {
  var tabs = ["mods", "restore", "samples"];
  tabs.forEach(function (t) {
    var btn = $("tab-" + t), panel = $("panel-" + t);
    if (!btn || !panel) return;
    var on = t === id;
    btn.setAttribute("aria-selected", on ? "true" : "false");
    panel.hidden = !on;
  });
}

function selectMachine(id) {
  ["cyc", "smp", "cos"].forEach(function (m) {
    var b = $("mach-" + m);
    if (b) b.setAttribute("aria-checked", m === id ? "true" : "false");
  });
}

function bindTabs() {
  ["mods", "restore", "samples"].forEach(function (t) {
    var btn = $("tab-" + t);
    if (btn) btn.addEventListener("click", function () { selectTab(t); });
  });
  ["cyc", "smp", "cos"].forEach(function (m) {
    var b = $("mach-" + m);
    if (b) b.addEventListener("click", function () { selectMachine(m); });
  });
}

function init() {
  renderCards();
  bindFile();
  bindMidi();
  bindTabs();
  selectMethod(true);
  credits();
}

document.addEventListener("DOMContentLoaded", init);
})();
