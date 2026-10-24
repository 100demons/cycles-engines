/* Composes added machines in the browser: the port of tools/compose.py's path without the toolchain (notes/61,
 * increments B3a to B3c). The Python composer is the reference; tools/webcompose_check.sh compares the two byte for
 * byte, and a change to how one composes goes into the other in the same commit.
 *
 * B3a: a package's object reaches the page laid out once at offset 0 by tools/relocate.py (layout(), bundle()):
 * its code and tables in hex, each relocation as [position, base, addend] (base "code", "bss", "abs", or the name of
 * a patch point), the symbols the composer needs as [base, offset], the variables' size. place() puts it at the
 * build's addresses, as relocate.apply() does.
 *
 * B3b: the registrar's part of a build comes as a frame per number of machines (tools/machines/registrar/frame.json,
 * compose.frames()): the writes and the reloc that do not depend on which machines, the layout, the runtime, the boot
 * stub template. fill() adds what does (the data block, the descriptors, the parameter block, the payload parts, the
 * segments, the boot stub), reading the stock values in the user's own MAIN OS (MCBuilder.mainOsOf), as compose.fill()
 * reads them in the official file. compose() is compose.compose(): the packages (compose.web_package()) placed one
 * after the other, registered together, their hooks and vtable entries added, the conflicts computed against the
 * tweaks given. It returns the tweak, which MCBuilder.build() applies like any other.
 *
 * License: MIT (the repository's LICENSE).
 *
 * Wrapped in an IIFE: in a classic <script>, top-level declarations are global and would collide.
 */
(function () {
"use strict";

const BASE = 0x40000400;                          // where the MAIN OS runs (builder.js, tools/build.py)

function fromHex(s) {
  // two hexadecimal digits per byte, as everywhere in the page (builder.js); anything else is refused
  if (typeof s !== "string" || s.length % 2 || /[^0-9a-fA-F]/.test(s))
    throw new Error(`invalid hexadecimal: ${String(s).slice(0, 24)}`);
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
  return out;
}
function hex(bytes) {                             // bytes.hex()
  let s = "";
  for (const b of bytes) s += b.toString(16).padStart(2, "0");
  return s;
}
const hx = (v) => "0x" + v.toString(16);          // f"{v:#x}"
const align4 = (x) => Math.ceil(x / 4) * 4;
const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
function u32(b, o) { return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0; }
function put32(b, o, v) { b[o] = v >>> 24; b[o + 1] = (v >>> 16) & 255; b[o + 2] = (v >>> 8) & 255; b[o + 3] = v & 255; }
function be32(v) { const b = new Uint8Array(4); put32(b, 0, v); return b; }
function concat(...arrs) {
  const out = new Uint8Array(arrs.reduce((n, a) => n + a.length, 0));
  let o = 0;
  for (const a of arrs) { out.set(a, o); o += a.length; }
  return out;
}
function bytesOf(...xs) {                         // bytes([...]): each value a byte, or refused
  for (const x of xs) if (!Number.isInteger(x) || x < 0 || x > 255) throw new Error(`${x}: not a byte`);
  return Uint8Array.from(xs);
}
function checkOverlaps(writes) {                  // writes in address order
  for (let k = 1; k < writes.length; k++)
    if (writes[k - 1].off + writes[k - 1].new.length / 2 > writes[k].off)
      throw new Error(`writes overlap at ${hx(BASE + writes[k].off)}`);
}

// relocate.apply(): the bundle's code at codeAt, its variables at bssAt (both 4-aligned), its patch points' symbols
// from values ({ symbol: number }). Returns { blob, symbols, bssEnd }.
function place(bundle, codeAt, bssAt, values = {}) {
  if (codeAt % 4 || bssAt % 4)
    throw new Error(`placed at 0x${codeAt.toString(16)} / 0x${bssAt.toString(16)}: code and variables start 4-aligned`);
  const bases = Object.assign(Object.create(null), { code: codeAt, bss: bssAt, abs: 0 });
  const blob = fromHex(bundle.code);
  const dv = new DataView(blob.buffer);
  for (const [pos, base, add] of bundle.relocs) {
    let v;
    if (base in bases) v = bases[base] + add;
    else if (Object.prototype.hasOwnProperty.call(values, base)) v = values[base] + add;
    else throw new Error(`undefined symbol ${base}: not one of this build's patch points`);
    dv.setUint32(pos, v >>> 0, false);
  }
  const symbols = {};
  for (const [name, [base, off]] of Object.entries(bundle.symbols)) symbols[name] = bases[base] + off;
  return { blob, symbols, bssEnd: bssAt + bundle.bss };
}

// compose.frame_of(): the frame of n machines out of frame.json, its writes in address order
function frameOf(frames, n) {
  if (frames.schema !== "model27-registrar-frame/0.1") throw new Error(`registrar frame: schema ${frames.schema}`);
  if (!has(frames.by_n, String(n))) throw new Error(`no registrar frame for ${n} machines`);
  const own = frames.by_n[String(n)];
  return { n, layout: frames.layout, stock: frames.stock, runtime: frames.runtime, stub: frames.stub,
           writes: frames.writes.concat(own.writes).sort((a, b) => a.off - b.off), reloc: own.reloc };
}

// compose.parameter_block(): the runtime's parameter block for this build (registrar.S), header and one entry per
// machine
function parameterBlock(fr, machines) {
  const lay = fr.layout, rt = fr.runtime;
  const n = machines.length, first = lay.first, nm = first + n, regs = rt.regs_at;
  if (n > rt.max_machines) throw new Error(`${n} machines: the runtime's block holds ${rt.max_machines}`);
  const parts = [bytesOf(n, first, first + n - 1, nm),
                 be32(76 + 5 * n),                                   // last
                 be32(lay.data + 12 * nm),                           // knob_tab
                 be32(regs + rt.regs_header + rt.entry_size * n)];   // machines_end
  machines.forEach((m, i) => {
    const e = new Uint8Array(rt.entry_size);
    e.set(bytesOf(first + i, m.image, 76 + 5 * i, 76 + 5 * i - 51), 0);
    put32(e, 4, lay.recs + 0x60 * i);                                // rec
    put32(e, 8, m.knob_hook || 0);                                   // knob_hook
    put32(e, 12, m.state_init || 0);                                 // state object initializer
    parts.push(e);
  });
  const out = concat(...parts);
  if (out.length > rt.regs_header + rt.entry_size * rt.max_machines) throw new Error("parameter block too big");
  return out;
}

// gen_syntakt_engines.segments(): the payload's parts to store in the image (pack), their union to within 64 bytes,
// aligned to 4. The rest is zero: the boot hook clears the payload, then copies the segments.
function segments(parts, size, pay) {
  const rs = parts.map((p) => {
    const a = parseInt(p.dest, 16), r = p.syntakt || p.cycles;
    const n = p.hex !== undefined ? p.hex.length / 2 : parseInt(r[1], 16) - parseInt(r[0], 16);
    return [a - (a % 4), align4(a + n)];
  }).sort((x, y) => x[0] - y[0] || x[1] - y[1]);
  const out = [];
  for (const [lo, hi] of rs) {
    const last = out[out.length - 1];
    if (last && lo <= last[1] + 64) last[1] = Math.max(last[1], hi);
    else out.push([lo, hi]);
  }
  if (out[0][0] < pay || out[out.length - 1][1] > pay + size) throw new Error("payload segments outside the payload");
  return out.map(([lo, hi]) => [lo, hi - lo]);
}

// compose.boot_stub(): the boot stub for this build from the template, its three immediates filled in, the segment
// table (address, longs per segment, then 0, 0) appended
function bootStub(fr, src, dst, longs, segs) {
  const st = fr.stub;
  if (parseInt(st.at, 16) !== fr.layout.cave)
    throw new Error(`the stub template is for ${st.at}, the boot hook lives at ${hx(fr.layout.cave)}`);
  const code = fromHex(st.hex);
  put32(code, st.src_at, src);
  put32(code, st.dst_at, dst);
  put32(code, st.longs_at, longs);
  const table = concat(...segs.map(([a, n]) => concat(be32(a), be32(Math.floor(n / 4)))), new Uint8Array(8));
  const stub = concat(code.subarray(0, st.segs_at), table);
  if (fr.layout.cave + stub.length > parseInt(st.end_limit, 16))
    throw new Error(`boot stub of ${stub.length} bytes (${segs.length} segments) does not fit before ${st.end_limit}`);
  return stub;
}

// compose.fill(): the rest of the registrar's part, what depends on the machines: the data block (names, update and
// render addresses, the strings), five descriptors per machine (SNARE's, read in the user's MAIN OS, with the knob
// fields set), the parameter block, the payload parts in their order, the segments and the boot stub. machines: name,
// image, decay, knobs ([long, short, default] or [long, short, default, min, max]), update, render; blobs: [[address,
// bytes]]; bssEnd: end of the variables. Returns { writes, append }.
function fill(fr, mainOs, machines, blobs, bssEnd) {
  const lay = fr.layout, stock = fr.stock, rt = fr.runtime;
  const n = machines.length, first = lay.first;
  if (n !== fr.n) throw new Error(`a frame for ${fr.n} machines, ${n} given`);
  for (const m of machines)
    if (m.knobs.length !== 4) throw new Error(`${m.name}: ${m.knobs.length} knobs; a machine has 4 and DECAY, five descriptors`);
  const nm = first + n;
  const rd = (va) => u32(mainOs, va - BASE);
  const mapAt = lay.data + 16 * nm;
  // --- data (as added_machine): names, update/render tables, machine -> record, strings
  const strings = machines.map((m) => m.name).concat(machines.flatMap((m) => m.knobs.flatMap((k) => [k[0], k[1]])));
  const addr = new Map();
  let at = mapAt + align4(nm);
  for (const s of strings) {
    if (!/^[\x00-\x7f]*$/.test(s)) throw new Error(`${s}: names and knob names are ASCII`);
    if (!addr.has(s)) { addr.set(s, at); at += s.length + 1; }
  }
  const stockTable = (va) => Array.from({ length: first }, (_, i) => rd(va + 4 * i));
  const names = stockTable(stock.names).concat(machines.map((m) => addr.get(m.name)));
  const upd = stockTable(stock.update).concat(machines.map((m) => m.update));
  const rnd = stockTable(stock.render).concat(machines.map((m) => m.render));
  const recs = Array.from({ length: nm }, (_, i) => i + 1);
  const data = concat(...[names, upd, rnd, recs].map((t) => concat(...t.map((x) => be32(x)))),
                      mainOs.subarray(stock.map - BASE, stock.map - BASE + first),
                      Uint8Array.from({ length: nm - first }, (_, i) => first + i), new Uint8Array(align4(nm) - nm),
                      ...[...addr.keys()].map((s) => concat(Uint8Array.from(s, (ch) => ch.charCodeAt(0)), new Uint8Array(1))));
  if (lay.data + data.length > lay.descn) throw new Error("data past DESCN");
  // --- 5 descriptors per machine, modelled on SNARE's
  const ds = lay.dstride, descs = [];
  const snare = (k) => mainOs.slice(stock.snare_desc + k * ds - BASE, stock.snare_desc + (k + 1) * ds - BASE);
  for (const m of machines) {
    m.knobs.forEach(([long, short, dflt, ...rng], k) => {
      const e = snare(k);
      put32(e, 0x00, 6);
      if (rng.length) { put32(e, 0x08, rng[0] << 8); put32(e, 0x0c, rng[1] << 8); }
      put32(e, 0x10, dflt << 8);
      put32(e, 0x2c, addr.get(long));
      put32(e, 0x34, addr.get(short));
      descs.push(e);
    });
    const e = snare(4);
    put32(e, 0x10, m.decay << 8);
    descs.push(e);
  }
  // --- payload: the packages, the runtime, its parameter block, the data, the descriptors
  const regs = rt.regs_at, code = fromHex(rt.hex);
  if (lay.stubs + code.length > regs) throw new Error("the runtime runs into its parameter block");
  const block = parameterBlock(fr, machines);
  if (regs + block.length > lay.data) throw new Error("parameter block past DATA");
  const part = (a, b) => ({ dest: hx(a), hex: hex(b) });
  const parts = blobs.filter(([a]) => a < lay.stubs).map(([a, b]) => part(a, b)).concat([
    part(lay.stubs, code),
    part(regs, block),
    part(lay.data, data),
    { dest: hx(lay.descn), cycles: [hx(stock.desc), hx(stock.desc + lay.ndesc * ds)] },
    part(lay.descn + lay.ndesc * ds, concat(...descs)),
  ], blobs.filter(([a]) => a >= lay.stubs).map(([a, b]) => part(a, b)));
  const size = bssEnd - lay.pay;
  const segs = segments(parts, size, lay.pay);
  const end = lay.image_end, added = segs.reduce((s, [, k]) => s + k, 0);
  if (end + added > lay.end_limit) throw new Error(`${added} bytes added: the image would pass ${hx(lay.end_limit)}`);
  const stub = bootStub(fr, end, lay.pay, Math.floor(size / 4), segs);
  if (stub.length > lay.stub_room) throw new Error("boot stub too big for its mask");
  const cave = lay.cave - BASE, free = mainOs.subarray(cave, cave + stub.length);
  if (free.length !== stub.length || !free.every((b) => b === 0xff)) throw new Error("the boot stub's place is taken");
  const writes = fr.writes.map((w) => ({ ...w })).concat([{ off: cave, old: "ff".repeat(stub.length), new: hex(stub) }]);
  writes.sort((a, b) => a.off - b.off);
  checkOverlaps(writes);
  return { writes, append: { at: hx(end), dest: hx(lay.pay), size, parts, reloc: fr.reloc.map((r) => [...r]),
                             pack: segs.map(([a, k]) => [hx(a), k]) } };
}

// compose.patch_values(), the symbol form: the build's values for the package's patch points
function patchValues(p, values) {
  const out = {};
  for (const pp of p.patch_points || []) {
    if (!has(values, pp.kind)) throw new Error(`${p.id}: patch point kind ${pp.kind} unknown`);
    if (!pp.symbol) throw new Error(`${p.id}: patch points as defines cannot be applied to a committed object: use symbols`);
    out[pp.symbol] = values[pp.kind];
  }
  return out;
}

const ramClaims = (p) => p.uses.filter((u) => u.point === "memory.ram").map((u) => u.range.map((x) => parseInt(x, 16)));

// compose.package_writes(): the package's own hooks (jsr/jmp into its code) and vtable entries, each checked against
// the stock bytes. That a vtable entry is the only reference to its method is a property of the stock OS, which the
// Python composer checks on every build (notes/61 §4); here the entry's stock bytes are checked like any write's.
function packageWrites(p, mainOs, syms) {
  const sym = (s) => { if (!has(syms, s)) throw new Error(`${p.id}: no symbol ${s}`); return syms[s]; };
  const writes = [];
  for (const u of p.uses) {
    if ("hook" in u) {
      const h = u.hook, va = parseInt(h.at, 16), old = fromHex(h.stock), target = be32(sym(h.symbol));
      const nw = h.op === "jsr" ? concat(bytesOf(0x4e, 0xb9), target) : concat(bytesOf(0x4e, 0xf9), target, bytesOf(0x4e, 0x71));
      if (nw.length !== old.length) throw new Error(`${p.id}: hook at ${hx(va)}: ${nw.length} bytes for ${old.length}`);
      const here = mainOs.subarray(va - BASE, va - BASE + old.length);
      if (hex(here) !== hex(old)) throw new Error(`${p.id}: ${hx(va)}: ${hex(old)} expected, ${hex(here)} found`);
      writes.push({ off: va - BASE, old: hex(old), new: hex(nw) });
    } else if ("vtable" in u) {
      const vt = u.vtable, va = parseInt(vt.at, 16), method = parseInt(vt.stock_ptr, 16);
      if (u32(mainOs, va - BASE) !== method) throw new Error(`${p.id}: ${hx(va)}: not the stock entry ${hx(method)}`);
      writes.push({ off: va - BASE, old: hex(be32(method)), new: hex(be32(sym(vt.symbol))) });
    }
  }
  return writes;
}

// compose.computed_conflicts(): the ids of the given tweaks this build cannot be installed with (other bytes at the
// same address, or a payload in one of the RAM ranges), plus the declared ones
function computedConflicts(writes, ram, declared, tweaks) {
  const mine = new Map();
  for (const w of writes) fromHex(w.new).forEach((b, i) => mine.set(w.off + i, b));
  const ids = new Set(declared);
  for (const t of tweaks) {
    if (!t.id) continue;
    let clash = (t.writes || []).some((w) => fromHex(w.new).some((b, i) => mine.has(w.off + i) && mine.get(w.off + i) !== b));
    const ap = t.append;
    if (ap && !clash) {
      const lo = parseInt(ap.dest, 16), hi = lo + ap.size;
      clash = ram.some(([a, b]) => lo < b && a < hi);
    }
    if (clash) ids.add(t.id);
  }
  return ids;
}

// compose.compose() without the toolchain: mainOs = the user's MAIN OS (MCBuilder.mainOsOf), frames = frame.json,
// packages = compose.web_package() of each machine, in order (machines 6, 7, ...), tweaks = what the conflicts are
// computed against. Returns { tweak, info }.
function compose(mainOs, frames, packages, tweaks = []) {
  if (!packages.length) throw new Error("name at least one machine");
  const ids = packages.map((p) => p.id);
  if (new Set(ids).size !== ids.length) throw new Error("a machine is named twice");
  const lay = frames.layout, pay = lay.pay, first = lay.first;
  const machines = [], blobs = [], symsOf = {}, info = [];
  let codeCursor = pay, bssCursor = pay + lay.bss_off;
  packages.forEach((p, i) => {
    const codeAt = align4(codeCursor), bssAt = align4(bssCursor);
    const values = patchValues(p, { machine_index: first + i, descriptor_base: 76 + 5 * i });
    const { blob, symbols: syms, bssEnd } = place(p.bundle, codeAt, bssAt, values);
    if (codeAt + blob.length > lay.stubs)
      throw new Error(`${p.id}: code and tables end at ${hx(codeAt + blob.length)}, past ${hx(lay.stubs)}`);
    const mc = p.uses.find((u) => u.point === "machine");
    if (!mc) throw new Error(`${p.id}: no 'machine' entry in uses: not an added machine`);
    const state = p.uses.find((u) => u.point === "descriptor.state");
    if (state && state.descriptor !== 1) throw new Error(`${p.id}: only the SHAPE descriptor (1) can have a state override for now`);
    machines.push({ name: mc.name, image: mc.image, decay: mc.decay_default,
                    knobs: mc.knobs.map((k) => [k.long, k.short, k.default].concat("min" in k ? [k.min, k.max] : [])),
                    update: syms[p.symbols.update], render: syms[p.symbols.render],
                    state_init: state ? syms[state.init] : 0 });
    blobs.push([codeAt, blob]);
    symsOf[p.id] = syms;
    info.push({ id: p.id, index: first + i, desc: 76 + 5 * i, code_at: codeAt, code: blob.length, bss_at: bssAt,
                bss: bssEnd - bssAt });
    codeCursor = codeAt + blob.length;
    bssCursor = bssEnd;
  });
  for (const p of packages)                       // a package's RAM claim must lie outside the payload
    for (const [lo, hi] of ramClaims(p))
      if (lo < bssCursor && pay < hi)
        throw new Error(`${p.id}: the payload (${hx(pay)}..${hx(bssCursor)}) runs into its RAM claim ${hx(lo)}..${hx(hi)}`);
  const { writes, append } = fill(frameOf(frames, packages.length), mainOs, machines, blobs, bssCursor);
  for (const p of packages) writes.push(...packageWrites(p, mainOs, symsOf[p.id]));
  writes.sort((a, b) => a.off - b.off);
  checkOverlaps(writes);
  const ram = [[pay, bssCursor]].concat(...packages.map(ramClaims));
  const tid = ids.join("+");
  const declared = packages.flatMap((p) => p.conflicts_declared || []);
  const conflicts = [...computedConflicts(writes, ram, declared, tweaks)].filter((x) => x !== tid).sort();
  const symbols = {};
  for (const p of packages)
    for (const k of [p.symbols.update, p.symbols.render, ...(p.keep || [])]) symbols[k] = hx(symsOf[p.id][k]);
  const tweak = {
    id: tid,
    order: Math.min(...packages.map((p) => p.order)),
    name: packages.length > 1 ? packages.map((p) => p.name).join(" + ") : packages[0].name,
    description: [`Composed in the browser by composer.js (notes/61): ${info.map((x) => `${x.id} as machine ${x.index}`)
      .join(", ")}, registered together through the registrar runtime, as tools/compose.py composes them.`],
    device: "Model:Cycles",
    os: "1.13",
    section: 3,
    version: packages.map((p) => p.version).join("+"),
    conflicts,
    writes,
    append,
    symbols,
  };
  return { tweak, info: { pay, end: bssCursor, image: append.pack.reduce((s, [, k]) => s + k, 0), packages: info } };
}

const API = { place, frameOf, fill, segments, bootStub, parameterBlock, packageWrites, computedConflicts, compose, fromHex };
if (typeof module !== "undefined" && module.exports) module.exports = API;
if (typeof window !== "undefined") window.MCComposer = API;
})();
