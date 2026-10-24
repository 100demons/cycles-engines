/* A Model:Cycles front panel in SVG, after the panel drawing of the Elektron manual (§3.1).
 * Coordinates: the panel is 1000 × 667 (the unit is 270 × 180 mm).
 *
 *   MCDevice.render(el, { seq, lcd, lit })   -> device
 *   device.light(["func", "track"])           lights these controls (keys turn red, knobs get a ring)
 *   device.show("menu")                       what the screen shows (see show() below)
 *   device.scenes("track | track,func @MUTE/>LOCKED", 900)   steps through several states
 *
 * Control names: level, lcd, machines, punch, gate, lfo, back, settings, tempo, rec, play, stop,
 * func, retrig, pattern, track, pitch, decay, color, shape, sweep, contour, delay, reverb, lfospeed,
 * volume, swing, chance, mainvol, revsize, deltime, page, t1..t6, s1..s16;
 * groups: pads, trigs, synth (the four machine knobs).
 */
(function () {
"use strict";

const NS = "http://www.w3.org/2000/svg";
const GROUPS = {
  pads: ["t1", "t2", "t3", "t4", "t5", "t6"],
  trigs: Array.from({ length: 16 }, (_, i) => "s" + (i + 1)),
  synth: ["color", "shape", "sweep", "contour"],
};
const MACHINES = ["Kick", "Snare", "Metal", "Perc", "Tone", "Chord", "SDVtg", "CPVtg", "SYToy", "SYBit", "SYSwm"];

// small key icons, drawn in a 20 × 20 box centred on the key
const ICON = {
  back: '<path d="M6 7h6a4 4 0 0 1 0 8H8"/><path d="M8.5 4.5 6 7l2.5 2.5"/>',
  settings: '<path d="M7 4.5v3.5a3 3 0 0 0 6 0V4.5M10 11v5"/><path d="M8 16h4"/>',
  tempo: '<path d="M7.5 15.5 9.5 4.5h1l2 11z"/><path d="M10 12l4.5-5.5"/>',
  machines: '<ellipse cx="10" cy="6.5" rx="5" ry="2"/><path d="M5 6.5v7c0 1.1 2.2 2 5 2s5-.9 5-2v-7"/><path d="M5 10c0 1.1 2.2 2 5 2s5-.9 5-2"/>',
  punch: '<path d="M6 6.5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0z"/><path d="M12 7.5a3 3 0 0 1 3 3v1a3 3 0 0 1-3 3"/>',
  gate: '<path d="M4.5 14.5h3v-9h5l3 9"/><path d="M8 4h5"/>',
  lfo: '<path d="M4 10c1.5-4 4.5-4 6 0s4.5 4 6 0"/>',
  rec: '<circle cx="10" cy="10" r="4.5"/>',
  play: '<path d="M7 5.5v9l7.5-4.5z"/>',
  stop: '<rect x="5.5" y="5.5" width="9" height="9" rx="1.5"/>',
};

function el(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}
function text(parent, x, y, s, cls, attrs) {
  const t = el("text", Object.assign({ x, y, class: cls || "lb" }, attrs || {}), parent);
  t.textContent = s;
  return t;
}

function render(host, opts) {
  opts = opts || {};
  const svg = el("svg", { viewBox: "-6 -6 1012 690", class: "mc", role: "img",
    "aria-label": "Model:Cycles" });
  const parts = {};                                   // name -> <g>
  const part = (name, cls) => (parts[name] = el("g", { class: cls, "data-k": name }, svg));

  // body: a flat shadow for the thickness, the top plate, the raised control area
  el("rect", { x: 0, y: 10, width: 1000, height: 667, rx: 26, class: "mc-edge" }, svg);
  el("rect", { x: 0, y: 0, width: 1000, height: 667, rx: 26, class: "mc-body" }, svg);
  el("path", { class: "mc-plate", d: "M17 180a36 36 0 0 1 36-36H918a42 42 0 0 1 42 42V636a16 16 0 0 1-16 16H33a16 16 0 0 1-16-16z" }, svg);

  // label tape where the maker's logo sits on the real unit
  const tape = el("g", { class: "mc-tape", transform: "translate(808 70) rotate(-2.5)" }, svg);
  el("rect", { x: -74, y: -17, width: 148, height: 34, rx: 3 }, tape);
  text(tape, 0, 6.5, "MODDED", "tape-t", { "text-anchor": "middle" });

  // recessed areas
  el("rect", { x: 397, y: 157, width: 469, height: 312, rx: 8, class: "mc-well" }, svg);
  el("rect", { x: 295, y: 475, width: 572, height: 97, rx: 8, class: "mc-well" }, svg);
  el("rect", { x: 876, y: 475, width: 84, height: 97, rx: 8, class: "mc-well" }, svg);
  el("rect", { x: 34, y: 578, width: 930, height: 55, rx: 8, class: "mc-well" }, svg);

  // LEVEL/DATA
  let g = part("level", "knob big");
  el("circle", { cx: 79, cy: 185, r: 25, class: "kn-ring" }, g);
  el("circle", { cx: 79, cy: 185, r: 21, class: "kn-cap" }, g);
  text(svg, 79, 222, "LEVEL / DATA", "lb", { "text-anchor": "middle" });
  text(svg, 79, 234, "TRIG / PAN", "lb2", { "text-anchor": "middle" });

  // screen (128 × 64 LCD)
  g = part("lcd", "lcd");
  el("rect", { x: 137, y: 163, width: 135, height: 78, rx: 8, class: "lcd-bezel" }, g);
  el("rect", { x: 145, y: 170, width: 119, height: 64, rx: 2, class: "lcd-glass" }, g);
  const lcd = el("g", { class: "lcd-px" }, g);
  text(svg, 204.5, 259, "Model:Cycles", "name", { "text-anchor": "middle" });

  // small keys
  const small = (name, x, y, sub) => {
    const k = part(name, "key sm");
    el("rect", { x: x - 21, y: y - 21, width: 42, height: 42, rx: 10, class: "key-cap" }, k);
    el("g", { class: "ico", transform: `translate(${x - 10} ${y - 10})` }, k).innerHTML = ICON[name];
    if (sub) text(svg, x, y + 34, sub, "lb2", { "text-anchor": "middle" });
  };
  small("back", 69, 305, "PAD MENU");
  small("settings", 160, 305, "TEMP SAVE");
  small("tempo", 250, 305, "TAP BPM");
  small("rec", 69, 380, "COPY");
  small("play", 160, 380, "CLEAR");
  small("stop", 250, 380, "PASTE");
  small("machines", 340, 185, "PRESET MENU");
  small("punch", 340, 263, "QUANTIZE");
  small("gate", 340, 340, "CLICK");
  small("lfo", 340, 418, "LFO SETUP");

  // wide keys
  const wide = (name, x, y, label, sub) => {
    const k = part(name, "key wd");
    el("rect", { x: x - 33, y: y - 20, width: 66, height: 40, rx: 10, class: "key-cap" }, k);
    text(k, x, y + 4.5, label, "kt", { "text-anchor": "middle" });
    if (sub) text(svg, x, y + 33, sub, "lb2", { "text-anchor": "middle" });
  };
  wide("func", 82, 455, "FUNC");
  wide("retrig", 238, 455, "RETRIG", "RETRIG MENU");
  wide("pattern", 82, 530, "PATTERN", "RELOAD PTN");
  wide("track", 238, 530, "TRACK", "CTRL ALL / TRK MENU");

  // knobs: 12 track parameters, then the right column
  const knob = (name, x, y, label, led) => {
    const k = part(name, "knob");
    el("circle", { cx: x, cy: y, r: 23, class: "kn-ring" }, k);
    el("circle", { cx: x, cy: y, r: 19, class: "kn-cap" }, k);
    if (led) el("rect", { x: x - 47, y: y - 14, width: 12, height: 12, rx: 2.5, class: "kn-led" }, k);
    text(svg, x, y + 38, label, "lb", { "text-anchor": "middle" });
  };
  const KN = [["pitch", "PITCH"], ["decay", "DECAY"], ["color", "COLOR"], ["shape", "SHAPE"],
    ["sweep", "SWEEP"], ["contour", "CONTOUR"], ["delay", "DELAY SEND"], ["reverb", "REVERB SEND"],
    ["lfospeed", "LFO SPEED"], ["volume", "VOLUME + DIST"], ["swing", "SWING / NUDGE"], ["chance", "CHANCE / COND"]];
  KN.forEach(([n, l], i) => knob(n, 453 + 117.5 * (i % 4), 185 + 117 * Math.floor(i / 4), l, true));
  knob("mainvol", 919, 185, "MAIN VOLUME", false);
  knob("revsize", 919, 302, "REVERB SIZE", false);
  knob("deltime", 919, 419, "DELAY TIME", false);
  text(svg, 919, 434 + 34, "DEL FEEDBACK", "lb2", { "text-anchor": "middle" });
  text(svg, 919, 317 + 34, "REV TONE", "lb2", { "text-anchor": "middle" });
  el("path", { d: "M881 166a7 7 0 1 0 6 0M884 162v6", class: "pwr" }, svg);

  // PAGE and its four pattern page LEDs
  g = part("page", "key wd");
  el("rect", { x: 885, y: 512, width: 67, height: 40, rx: 10, class: "key-cap" }, g);
  text(g, 918.5, 536.5, "PAGE", "kt", { "text-anchor": "middle" });
  ["1:4", "2:4", "3:4", "4:4"].forEach((s, i) => {
    el("rect", { x: 886 + i * 17, y: 482, width: 12, height: 10, rx: 2, class: "pg-led" + (i === 0 ? " on" : "") }, svg);
    text(svg, 892 + i * 17, 503, s, "lb3", { "text-anchor": "middle" });
  });
  text(svg, 918, 566, "FILL / SCALE", "lb2", { "text-anchor": "middle" });

  // pads T1-T6
  for (let i = 0; i < 6; i++) {
    const x = 336 + 98 * i;
    const k = part("t" + (i + 1), "key pad");
    el("rect", { x: x - 34, y: 485, width: 68, height: 68, rx: 11, class: "key-cap" }, k);
    text(k, x, 525, "T" + (i + 1), "pt", { "text-anchor": "middle" });
    text(svg, x, 566, "BANK " + "ABCDEF"[i] + " / MUTE", "lb2", { "text-anchor": "middle" });
  }

  // trig keys 1-16
  for (let i = 0; i < 16; i++) {
    const x = 69.5 + 57.4 * i;
    const k = part("s" + (i + 1), "key trig");
    el("rect", { x: x - 22, y: 583, width: 44, height: 44, rx: 9, class: "key-cap" }, k);
    const t = text(k, x, 610, String(i + 1), "kt", { "text-anchor": "middle" });
    if (i % 4 === 0) el("rect", { x: x - 6, y: 614, width: 12, height: 1.6, class: "ul" }, k);
    t.setAttribute("class", "kt num");
  }

  host.innerHTML = "";
  host.appendChild(svg);

  // ---- behaviour ----------------------------------------------------------
  const names = (list) => [].concat(...(list || []).map((n) => GROUPS[n] || [n]));
  let lit = new Set();
  // Lights these controls; while some are lit, everything else on the panel steps back.
  function light(list) {
    const want = new Set(names(list));
    for (const n of lit) if (!want.has(n) && parts[n]) parts[n].classList.remove("lit");
    for (const n of want) if (parts[n]) parts[n].classList.add("lit");
    lit = want;
    svg.classList.toggle("focus", want.size > 0);
  }

  // LCD. A screen is "logo", "menu" (the MACHINES menu, going through the added machines),
  // "~A long name" (a sound browser line scrolling) or lines "LINE 1/>LINE 2" (">" selects a line).
  let lcdTimer = null;
  const row = (r, s, sel, x) => {
    const y = 175 + r * 14.5;
    if (sel) el("rect", { x: 147, y: y - 1, width: 115, height: 14, class: "lcd-sel" }, lcd);
    text(lcd, x || 151, y + 10, s, "lcd-t" + (sel ? " inv" : ""));
  };
  function show(spec) {
    clearInterval(lcdTimer);
    lcdTimer = null;
    spec = (spec || "logo").trim();
    lcd.innerHTML = "";
    if (spec === "logo") {
      text(lcd, 204.5, 209, "elektron", "lcd-logo", { "text-anchor": "middle" });
    } else if (spec === "menu") {
      let sel = 6;
      const draw = () => {
        lcd.innerHTML = "";
        const top = Math.max(0, Math.min(sel - 2, MACHINES.length - 4));
        for (let r = 0; r < 4; r++) {
          const n = top + r;
          row(r, String(n + 1).padStart(2, "0"), n === sel);
          text(lcd, 170, 185 + r * 14.5, MACHINES[n], "lcd-t" + (n === sel ? " inv" : ""));
        }
      };
      draw();
      if (!reduced()) lcdTimer = setInterval(() => { sel = sel >= MACHINES.length - 1 ? 5 : sel + 1; draw(); }, 700);
    } else if (spec[0] === "~") {
      // about 0.17 s per character and 0.5 s at each end, as drumkilla's browser-scroll does
      const name = spec.slice(1), win = 15, last = Math.max(0, name.length - win);
      let i = 0, wait = 3;
      const draw = () => {
        lcd.innerHTML = "";
        row(0, "02  Rim Tick", false);
        row(1, "03  " + name.slice(i, i + win), true);
        row(2, "04  Zap Lo", false);
      };
      draw();
      if (!reduced() && last) lcdTimer = setInterval(() => {
        if (wait > 0) { wait--; return; }
        if (i >= last) { i = 0; wait = 3; draw(); return; }
        i++;
        draw();
        if (i >= last) wait = 3;
      }, 170);
    } else {
      const lines = spec.split("/");
      const y0 = Math.max(0, (4 - lines.length) / 2);
      lines.forEach((l, r) => row(r + y0, l[0] === ">" ? l.slice(1) : l, l[0] === ">"));
    }
  }

  // Steps "track | track,func @MUTE/>LOCKED | func": the controls to light, and the screen after "@".
  let sceneTimer = null;
  function scenes(spec, ms) {
    const steps = spec.split("|").map((s) => {
      const [k, screen] = s.split("@");
      return { lit: k.split(",").map((x) => x.trim()).filter(Boolean), screen };
    });
    const go = (st) => { light(st.lit); if (st.screen != null) show(st.screen); };
    clearInterval(sceneTimer);
    let i = 0;
    go(steps[0]);
    if (reduced()) return;
    sceneTimer = setInterval(() => { i = (i + 1) % steps.length; go(steps[i]); }, ms || 900);
  }

  show(opts.lcd);

  // sequencer: placed trigs lit, a light running along the 16 steps
  const pattern = opts.pattern || [1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0];
  function trigs(step) {
    for (let i = 0; i < 16; i++) {
      const on = !!pattern[i] !== (i === step);         // the running light inverts the step it is on
      parts["s" + (i + 1)].classList.toggle("on", on);
    }
  }
  if (opts.seq) {
    trigs(-1);
    parts.t1.classList.add("on");
    if (!reduced()) {
      let step = -1;
      loop(host, 125, () => { step = (step + 1) % 16; trigs(step); });
    }
  }
  if (opts.lit) light(opts.lit);

  return { light, show, scenes, svg };
}

function reduced() {
  return !!(window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches);
}

// Runs fn every `ms` while el is on screen and the tab is visible.
function loop(target, ms, fn) {
  let timer = null, visible = true;
  const start = () => { if (!timer && visible && !document.hidden) timer = setInterval(fn, ms); };
  const stop = () => { clearInterval(timer); timer = null; };
  if ("IntersectionObserver" in window)
    new IntersectionObserver((es) => { visible = es[0].isIntersecting; visible ? start() : stop(); }).observe(target);
  document.addEventListener("visibilitychange", () => (document.hidden ? stop() : start()));
  start();
}

window.MCDevice = { render, loop, reduced, MACHINES };
})();
