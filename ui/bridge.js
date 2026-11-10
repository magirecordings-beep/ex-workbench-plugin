/* ===================== Xtreme Lab plugin bridge =====================
   Runs inside the browser version's own script (so it sees all of its functions and state). The page keeps
   doing everything it does in Chrome - library, patches, every card and editor - but the sound comes from the
   plugin: this replaces the Web Audio engine with messages to the processor, which plays exactly what the page
   describes. The bank, its samples and their audio are served by the plugin. */
const XB = window.__JUCE__ && window.__JUCE__.backend;
const toNative = o => { try { XB && XB.emitEvent('exw', o); } catch (e) { console.warn(e); } };
const PLUGIN = !!XB;

// ---------- no Web Audio: notes, panic, controllers go to the plugin ----------
audio = () => null;
unlockAudio = () => {};
applyBend = () => {};
WK.ok = true; WK.tried = true;   // the effect panels: the plugin has every FX B type
soundStatus = () => {};
startMeter = () => {};
connectMidi = async () => {};
noteOn = async (note, vel = 100) => { vel = Math.max(1, Math.min(127, vel | 0)); toNative({ type: 'note', note, vel }); keyEl(note)?.classList.add('on'); };
noteOff = note => { toNative({ type: 'note', note, vel: 0 }); keyEl(note)?.classList.remove('on'); };
noteOffNow = note => noteOff(note);
panic = () => { toNative({ type: 'panic' }); document.querySelectorAll('.key.on').forEach(k => k.classList.remove('on')); };
// the on-screen wheels: to the plugin, and shown here
onMidi = e => { const [st, d1, d2] = e.data; toNative({ type: 'midi', data: [st, d1, d2 ?? 0] }); midiShow(st, d1, d2 ?? 0, false); };
// MIDI arriving from the host (or the wheels): key lights, MIDI learn, knob readouts. The plugin plays it.
function midiShow(st, d1, d2, host) {
  const cmd = st & 0xf0;
  if (cmd >= 0x80 && cmd <= 0xe0) midiActivity();
  if (cmd === 0x90 && d2 > 0) keyEl(d1)?.classList.add('on');
  else if (cmd === 0x80 || (cmd === 0x90 && d2 === 0)) keyEl(d1)?.classList.remove('on');
  else if (cmd === 0xd0) ctrl.val[18] = d1 / 127;
  else if (cmd === 0xe0) { bendPos = Math.max(-1, Math.min(1, (((d2 << 7) | d1) - 8192) / 8191)); const el = $('#bend-read'); if (el) el.textContent = Math.abs(bendPos) > .01 ? 'bend ' + (bendPos > 0 ? '+' : '') + Math.round(bendPos * 100) + '%' : ''; }
  else if (cmd === 0xb0) {
    if (host && learnIdx >= 0 && d1 !== 64 && d1 < 120 && d1 !== 0 && d1 !== 32) learnCc(d1);
    const ap = AP_CC.indexOf(d1); if (ap >= 0) { setCtrl(MIDI_AP[ap], d2 / 127); sentKnobs[ap] = d2 / 127; }
    if (d1 === 1) setCtrl(17, d2 / 127);
    else if (d1 === 4) setCtrl(19, d2 / 127);
    else if (host && d1 === 74) filtSetCC('freq', d2 / 127 * 100);
    else if (host && d1 === 71) filtSetCC('res', d2 / 127 * 100);
    else if (host && d1 === 7) setMaster(KNOB_MIN + d2 / 127 * (KNOB_MAX - KNOB_MIN));
  }
}
engTick = () => {};
let nativeNotes = 0;
function showNotes(n) {
  nativeNotes = n;
  $('#eng-voices').style.width = Math.min(100, n / MAX_VOICES * 100) + '%'; $('#eng-voices-v').textContent = String(n);
  const now = performance.now(); eng.busy = eng.busy.filter(([t, d]) => t + d > now - 2000);
  const busy = eng.busy.reduce((a, [t, d]) => a + Math.min(d, t + d - (now - 2000)), 0) / 2000 * 100;
  $('#eng-ui').style.width = Math.min(100, busy) + '%'; $('#eng-ui-v').textContent = Math.round(busy) + '%';
}
// output meter from the plugin's peaks
const nPeak = [0, 0];
(function meterLoop() {
  const frame = now => {
    requestAnimationFrame(frame);
    if (document.hidden) return;
    const dt = meter.last ? Math.min(.1, (now - meter.last) / 1000) : 0; meter.last = now;
    for (let ch = 0; ch < 2; ch++) {
      const pk = nPeak[ch]; nPeak[ch] = 0; const db = pk > 0 ? 20 * Math.log10(pk) : -Infinity;
      if (pk >= 0.999) { meter.clip = true; $('#clip').classList.add('on'); }
      meter.level[ch] = db >= meter.level[ch] ? db : Math.max(db, meter.level[ch] - 26 * dt);
      if (db >= meter.hold[ch] || now - meter.holdT[ch] > 1500) { meter.hold[ch] = db; meter.holdT[ch] = now; }
    }
    drawMeter(meter.level, meter.hold);
    if (now - meter.readT > 120) { meter.readT = now; const p = Math.max(meter.hold[0], meter.hold[1]); $('#peak-read').textContent = p > -90 ? (p > 0 ? '+' : '') + p.toFixed(1) + ' dB' : '−∞ dB'; }
  };
  requestAnimationFrame(frame);
})();

// ---------- samples come from the plugin ----------
const ensureAudioWeb = ensureAudio;
ensureAudio = async s => {
  if (s.data) return s.data;
  if (s.src?.native == null) return ensureAudioWeb(s);
  s._pcm ||= fetch('/x/pcm/' + s.src.native).then(r => r.arrayBuffer()).then(buf => {
    const i16 = new Int16Array(buf), n = s.frames, out = [];
    for (let c = 0; c < s.channels; c++) { const f = new Float32Array(n); for (let i = 0; i < n && c * n + i < i16.length; i++) f[i] = i16[c * n + i] / 32768; out.push(f); }
    s.data = out; return out;
  }).catch(() => { s._pcm = null; return null; });
  return s._pcm;
};
async function nativeBank(info) {
  if (!info) return;
  progress(`Reading ${info.name}…`);
  try {
    const [exb, smp] = await Promise.all([fetch('/x/exb').then(r => r.arrayBuffer()), fetch('/x/samples.json').then(r => r.json())]);
    state.banks = []; state.chunks = []; state.presets = []; state.samples = []; state.poolByIndex = new Map(); state.zones = [];
    if (state.current?.type !== 'patch') state.current = null;
    await loadBank({ file: new File([exb], info.name), path: info.name });
    for (const m of smp) {
      const s = { name: m.name, rate: m.rate, channels: m.channels, frames: m.frames, loop: m.loop, loopStart: m.loopStart, loopEnd: m.loopEnd, src: { native: m.i }, nativeIdx: m.i, path: m.name, origin: 'EBL' };
      addSample(s); if (m.sl != null) state.poolByIndex.set(m.sl, s);
    }
    libVersion++;
    state.bankInfo = info;
    renderAll();
    $('#btn-forget').hidden = false;
  } finally { progress(null); }
}

// ---------- what the plugin plays: the page's whole sound description ----------
const zonesOf = zs => zs.map(z => ({ i: z.sample?.nativeIdx, root: z.root, lo: z.lo, hi: z.hi })).filter(z => z.i != null);
function srcOf(r) {
  if (!r) return null;
  if (r.type === 'preset') return { type: 'preset', name: r.preset.name, bank: r.preset.bank?.name || '' };
  return { type: 'zones', zones: zonesOf(r.zones) };
}
function engineSnapshot() {
  const c = state.current; let sel = null;
  if (c?.type === 'preset' && state.presets[c.key]?.def) { const p = state.presets[c.key]; sel = { type: 'preset', name: p.name, bank: p.bank?.name || '' }; }
  else if (c?.type === 'family' || c?.type === 'sample') sel = state.zones.length ? { type: 'zones', zones: zonesOf(state.zones) } : null;
  else if (c?.type === 'patch' && curPatch()) sel = { type: 'patch' };
  const pt = curPatch();
  const s = {
    sel,
    filt: { type: fState.type, freq: fState.freq, res: fState.res, env: fState.env, decay: fState.decay, usePreset: !!fState.usePreset },
    pOfs: fState.pOfs || P_OFS(), envSource: envState.source, env: envState.env, lfos: lfoState, fgs: fgState,
    P: { tune: P.tune.v, bend: P.bend.v, bendp: P.bendp.v, velsens: P.velsens.v, loop: P.loop.v, envscale: P.envscale.v, pmod: P.pmod.v },
    sendA: fxState.send.a, sendB: fxState.send.b,
    edit: { layer: state.layerIdx, lfo: lfoIdx, fg: fgIdx }
  };
  if (pt) {
    s.mf = pt.mfilter || newMaster();
    s.layers = pt.layers.map(ly => ({ src: ly.src ? srcOf(resolveLayer(ly)) : null, on: ly.on, solo: ly.solo, volDb: ly.volDb, pan: ly.pan, coarse: ly.coarse, fine: ly.fine,
      key: ly.key, vel: ly.vel, useSrcEnv: ly.useSrcEnv, env: ly.env, filter: ly.filter, lfos: lfosFor({ ly }), fgs: fgsFor({ ly }), sendA: ly.sendA, sendB: ly.sendB }));
  }
  return s;
}
const selKey = () => { const c = state.current; return c ? c.type + ':' + c.key + ':' + (c.type === 'preset' ? state.presets[c.key]?.name : c.type === 'patch' ? '' : state.zones.length) : ''; };
let sentEngine = '', sentSel = null;
function syncEngine() {
  if (!state.bankInfo) return;
  let s; try { s = engineSnapshot(); } catch (e) { console.warn(e); return; }
  const j = JSON.stringify(s); if (j === sentEngine) return;
  const k = selKey(), reset = k !== sentSel; sentSel = k; sentEngine = j;
  toNative({ type: 'engine', state: s, reset });
}
// the A-P knobs and their MIDI numbers
const sentKnobs = Array(16).fill(-1); let sentApcc = '';
function syncKnobs() {
  const v = MIDI_AP.map(k => ctrl.val[k] ?? 0);
  if (v.some((x, i) => Math.abs(x - sentKnobs[i]) > 1e-4)) { v.forEach((x, i) => sentKnobs[i] = x); toNative({ type: 'knobs', values: v }); }
  const a = JSON.stringify(AP_CC); if (a !== sentApcc) { sentApcc = a; toNative({ type: 'apcc', list: AP_CC }); }
}
// Performance, Effects and the master knob are host parameters (so they automate)
const ix = (list, v, d = 0) => { const i = list.indexOf(v); return i < 0 ? d : i; };
const nearest = (list, v) => list.reduce((b, x, i) => Math.abs(x - v) < Math.abs(list[b] - v) ? i : b, 0);
function paramMap() {
  const A = fxState.a, B = fxState.b, X = B.x || {}, D = X.drive || {}, F = X.flanger || {}, H = X.phaser || {}, E = fxState.eq, C = fxState.color, b = v => v ? 1 : 0;
  return {
    master: masterDb,
    keys: ix(['preset', 'poly', 'mono', 'legato'], perf.keys), glideMode: ix(['preset', 'off', 'custom'], perf.glide), glideMs: perf.glideMs,
    arpMode: ix(['off', 'on', 'latch'], perf.arp), arpPattern: ix(['up', 'down', 'updown', 'downup', 'played', 'random', 'chord'], perf.pattern),
    arpRate: ix(['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32'], perf.rate, 3), arpOct: perf.oct, arpGate: perf.gate, arpSwing: perf.swing,
    arpSync: perf.sync === 'clock' ? 0 : 1, arpTempo: perf.tempo,
    colOn: b(C.on), colMono: b(C.mono), colWarmth: C.warmth, colWeight: C.weight, colSub: C.sub, colSmooth: C.smooth,
    sendA: fxState.send.a, sendB: fxState.send.b,
    fxaOn: b(A.on), fxaType: ix(['classic', 'room', 'hall', 'plate', 'ambience'], A.type), fxaSize: A.size, fxaDecay: A.decay, fxaPre: A.pre, fxaDamp: A.damp, fxaWidth: A.width, fxaTone: A.tone, fxaRet: A.ret,
    fxbOn: b(B.on), fxbType: ix(['chorus', 'flanger', 'phaser', 'delay', 'drive'], B.type), fxbIns: b(B.insert), fxbRet: B.ret,
    chRate: B.rate, chDepth: B.depth,
    dlSync: b(B.dsync), dlDivL: nearest([1, 2, 3, 4, 5, 6, 8, 16], B.divL), dlDivR: nearest([1, 2, 3, 4, 5, 6, 8, 16], B.divR), dlOffL: B.offL, dlOffR: B.offR, dlMsL: B.time, dlMsR: B.msR,
    dlLink: b(B.link), dlPP: b(B.pp), dlFreeze: b(B.freeze), dlFb: B.fb, dlFilt: b(B.filt), dlFreq: B.ffreq, dlWidth: B.fwidth, dlMRate: B.mrate, dlMFilt: B.mfilt, dlMTime: B.mtime,
    dlMode: ix(['repitch', 'fade', 'jump'], B.dmode, 1),
    flSync: b(F.sync), flBeats: nearest([.5, 1, 2, 4, 8, 16, 32, 64], F.beats ?? 16), flRate: F.rate, flDepth: F.depth, flFb: F.fb, flSpread: F.spread, flMix: F.mix,
    phStages: nearest([4, 6, 8, 12, 16, 24], H.stages ?? 8), phFreq: H.freq, phDepth: H.depth, phShape: ix(['sine', 'triangle', 'square', 'saw', 'env'], H.shape), phSync: b(H.sync),
    phBeats: nearest([.25, .5, 1, 2, 4, 8, 16], H.beats ?? 4), phRate: H.rate, phRes: H.res, phWidth: H.width, phMix: H.mix, phAnalog: b(H.analog),
    drStyle: ix(['A', 'E', 'N', 'T', 'P'], D.style), drDrive: D.drive, drPunish: b(D.punish), drAuto: b(D.auto), drThump: b(D.thump), drSteep: b(D.steep),
    drLowcut: D.lowcut ? Math.max(10, D.lowcut) : 10, drTone: D.tone, drHighcut: D.highcut ? Math.min(20000, D.highcut) : 20000, drMix: D.mix, drOut: D.out,
    eqLow: E.low, eqMid: E.mid, eqMidHz: E.midHz, eqHigh: E.high
  };
}
const sentParams = {};
function syncParams() {
  let m; try { m = paramMap(); } catch (e) { return; }
  for (const [id, v] of Object.entries(m)) { if (typeof v !== 'number' || !isFinite(v)) continue; if (sentParams[id] !== v) { sentParams[id] = v; toNative({ type: 'param', id, value: v }); } }
}

// ---------- the panel's state, kept with the DAW project ----------
function uiSnapshot() {
  const c = state.current, pt = curPatch();
  let cur = null;
  if (c?.type === 'preset') cur = { type: 'preset', name: state.presets[c.key]?.name, bank: state.presets[c.key]?.bank?.name };
  else if (c?.type === 'patch' && pt) cur = { type: 'patch', patch: { ...serialPatch(pt), dirty: !!pt.dirty } };
  else if (c?.type === 'family') cur = { type: 'family', key: c.key };
  else if (c?.type === 'sample') cur = { type: 'sample', name: state.samples.find(s => s.id === c.key)?.name };
  return { cur, layerIdx: state.layerIdx, tab: state.tab, page: document.querySelector('.rackgrid')?.dataset.page,
    fState: { ...fState }, envState: { source: envState.source, env: envState.env }, lfos: lfoState, fgs: fgState,
    P: Object.fromEntries(Object.entries(P).map(([k, p]) => [k, p.v])), perf: { ...perf }, fx: { ...fxSnapshot(), send: { ...fxState.send } }, masterDb,
    knobs: MIDI_AP.map(k => ctrl.val[k] ?? 0) };
}
let sentUi = '';
function syncUi() { let j; try { j = JSON.stringify(uiSnapshot()); } catch (e) { return; } if (j !== sentUi) { sentUi = j; toNative({ type: 'ui', state: JSON.parse(j) }); } }
async function restoreUi(u) {
  if (!u) return;
  try {
    const c = u.cur;
    if (c?.type === 'preset') { const i = state.presets.findIndex(p => p.name === c.name && (!c.bank || p.bank?.name === c.bank)); if (i >= 0) { state.tab = 'pre'; await selectPreset(i); } }
    else if (c?.type === 'family') { state.tab = 'fam'; await selectFamily(c.key); }
    else if (c?.type === 'sample') { const s = state.samples.find(x => x.name === c.name); if (s) { state.tab = 'smp'; await selectSample(s.id); } }
    else if (c?.type === 'patch' && c.patch) {
      let i = state.patches.findIndex(p => p.name === c.patch.name);
      const n0 = state.patches.length; loadPatchJSON(JSON.stringify({ format: PATCH_FORMAT, patches: [c.patch] }), c.patch.name);
      const fresh = state.patches.pop(); fresh.dirty = !!c.patch.dirty;
      if (i >= 0) state.patches[i] = fresh; else { state.patches.push(fresh); i = n0; }
      state.tab = 'pat'; state.layerIdx = Math.max(0, Math.min(3, u.layerIdx | 0)); await selectPatch(i);
    }
    // the cards' own settings on top of what selecting brought
    if (u.fState) Object.assign(fState, u.fState);
    if (u.envState) { envState.source = u.envState.source; envState.env = u.envState.env; }
    if (u.lfos) u.lfos.forEach((l, i) => Object.assign(lfoState[i], l));
    if (u.fgs) u.fgs.forEach((f, i) => Object.assign(fgState[i], f));
    if (u.P) for (const [k, v] of Object.entries(u.P)) { const p = P[k]; if (!p) continue; p.v = v; const inp = $('#p-' + k);
      if (inp) { if (p.bool) inp.checked = !!v; else { inp.value = v; fillRange?.(inp); } } const o = $('#o-' + k); if (o && p.fmt) o.textContent = p.fmt(v); }
    if (u.perf) Object.assign(perf, u.perf);
    if (u.fx && !curPatch()) { fxLoad(u.fx); if (u.fx.send) Object.assign(fxState.send, u.fx.send); }
    if (typeof u.masterDb === 'number') setMaster(u.masterDb);
    if (Array.isArray(u.knobs)) MIDI_AP.forEach((k, i) => setCtrl(k, u.knobs[i] ?? 0));
    if (u.tab) state.tab = u.tab;
    renderList(); renderLCD(); perfRender(); fxRender(); filtRender(); envRefresh(); lfoRender(); fgRender(); ctlRender?.();
    if (u.page && u.page !== 'patch') setPage(u.page);
  } catch (e) { console.warn('restore', e); }
}

// ---------- patches are kept by the plugin (all projects share them) ----------
let savedLib = [];
savePatches = async (list, suggested) => {
  for (const pt of list) { const sp = serialPatch(pt); const i = savedLib.findIndex(p => p.name === sp.name); if (i >= 0) savedLib[i] = sp; else savedLib.push(sp); pt.dirty = false; }
  toNative({ type: 'patchesStore', patches: savedLib });
  renderList(); renderPatch();
  toast(list.length > 1 ? `Saved ${list.length} patches to My patches.` : `Saved “${list[0].name}” to My patches.`);
  return true;
};
document.addEventListener('click', e => { const b = e.target.closest?.('[data-learn]'); if (b) setTimeout(() => toNative({ type: 'learn', index: learnIdx }), 0); });
document.addEventListener('click', e => {
  const del = e.target.closest?.('#patch-del');
  if (del && del.textContent === 'Confirm delete') { const pt = curPatch(); if (pt) { savedLib = savedLib.filter(p => p.name !== pt.name); toNative({ type: 'patchesStore', patches: savedLib }); } }
}, true);

// ---------- files: opening goes through the plugin, saving too ----------
download = (bytes, name, type) => {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes.buffer || bytes);
  const CH = 3 << 20, n = Math.max(1, Math.ceil(u8.length / CH));
  for (let i = 0; i < n; i++) {
    const part = u8.subarray(i * CH, (i + 1) * CH); let bin = '';
    for (let k = 0; k < part.length; k += 32768) bin += String.fromCharCode.apply(null, part.subarray(k, k + 32768));
    toNative({ type: 'saveFile', name, i, n, chunk: btoa(bin) });
  }
};
const grab = (sel, fn) => document.querySelector(sel)?.addEventListener('click', e => { e.stopImmediatePropagation(); e.preventDefault(); fn(); }, true);
grab('#btn-folder', () => toNative({ type: 'openBank' }));
grab('#btn-files', () => toNative({ type: 'openFiles' }));
grab('#btn-addpool', () => toNative({ type: 'openBank' }));
grab('#btn-addzip', () => toNative({ type: 'openBank' }));
grab('#btn-forget', () => { toNative({ type: 'forget' }); toast('Xtreme Lab won’t reopen this bank by itself any more.'); $('#btn-forget').hidden = true; });
grab('#btn-midi', () => {});
addEventListener('drop', e => {
  const files = [...(e.dataTransfer?.files || [])];
  if (files.some(f => /\.(exb|ebl|e4b|zip)$/i.test(f.name))) { e.preventDefault(); e.stopImmediatePropagation(); toast('Use Open bank to load a bank in the plugin. Its SamplePool comes with it.'); }
}, true);

// ---------- what's different in a plugin window ----------
function pluginFace() {
  $('#btn-sound').hidden = true; $('#midi-sel').hidden = true;
  $('#midi-txt').textContent = 'MIDI from host';
  $('#btn-midi').title = 'Notes and controllers come from your DAW’s track';
  const d = $('#drop p'); if (d) d.innerHTML = '<b>Open bank</b> and pick an <span class="mono">.exb</span>: its <span class="mono">SamplePool</span> is found next to it and loaded with it. <b>Choose files</b> also opens your saved <span class="mono">.exwp</span> patches.';
  const clk = document.querySelector('#pf-sync [data-v="clock"]'); if (clk) { clk.textContent = 'Host tempo'; clk.title = 'Follow your DAW’s tempo'; }
  const s = document.createElement('style');
  s.textContent = '#btn-sound{display:none!important}';
  document.head.appendChild(s);
}

// ---------- messages from the plugin ----------
let started = false;
async function onNative(m) {
  if (!m || !m.type) return;
  if (m.type === 'init') {
    if (started) return; started = true;
    if (Array.isArray(m.apcc)) { AP_CC = m.apcc.slice(); sentApcc = JSON.stringify(AP_CC); }
    if (Array.isArray(m.patches) && m.patches.length) { savedLib = m.patches.slice(); loadPatchJSON(JSON.stringify({ format: PATCH_FORMAT, patches: savedLib }), 'Patch'); renderList(); }
    if (typeof m.params?.master === 'number') setMaster(m.params.master);
    if (m.loading) progress(m.status || 'Loading…');
    if (m.bank) {
      await nativeBank(m.bank);
      if (m.ui?.cur) await restoreUi(m.ui);
      else { // a fresh plugin: what the processor plays, else the bank's first preset
        const want = m.engine?.sel?.type === 'preset' ? m.engine.sel.name : null;
        let i = want ? state.presets.findIndex(p => p.name === want) : -1; if (i < 0) i = state.presets.findIndex(p => p.def);
        if (i >= 0) { state.tab = 'pre'; renderList(); await selectPreset(i); }
        if (Array.isArray(m.knobs) && want) MIDI_AP.forEach((k, i) => setCtrl(k, m.knobs[i]));
      }
    }
    else if (m.status && /not found/i.test(m.status)) toast(m.status + '. Press Open bank to find it.');
    $('#btn-forget').hidden = !m.bank && !m.recent;
    if (m.hostParams) onHostParams(m.hostParams);
    syncAll();
  } else if (m.type === 'bank') {
    const keep = uiSnapshot();
    await nativeBank(m.bank);
    // the same bank again (reloaded): keep what was selected; a new bank: its first preset
    if (keep.cur?.type === 'preset' && state.presets.some(p => p.name === keep.cur.name)) await restoreUi(keep);
    else if (keep.cur?.type === 'patch') await restoreUi(keep);
    else { const i = state.presets.findIndex(p => p.def); if (i >= 0) { state.tab = 'pre'; renderList(); await selectPreset(i); } }
    if (m.bank?.missing) toast(`${m.bank.missing} zones have no sample in the SamplePool found next to the bank.`);
    syncAll();
  } else if (m.type === 'status') {
    showNotes(m.notes | 0);
    if (Array.isArray(m.peak)) { nPeak[0] = Math.max(nPeak[0], m.peak[0]); nPeak[1] = Math.max(nPeak[1], m.peak[1]); }
    if (Array.isArray(m.midi)) for (const v of m.midi) midiShow(v & 255, (v >> 8) & 255, (v >> 16) & 255, true);
    if (Array.isArray(m.knobs)) MIDI_AP.forEach((k, i) => { if (Math.abs((ctrl.val[k] ?? 0) - m.knobs[i]) > .004) { setCtrl(k, m.knobs[i]); sentKnobs[i] = m.knobs[i]; } });
    if ('loading' in m) { if (m.loading) progress(m.status || 'Loading…', m.progress, 1); else progress(null); if (!m.loading && m.status && /^(Couldn|No |Not an|Bank not)/.test(m.status)) toast(m.status); }
    if (perf.arp !== 'off') { const h = $('#arp-held'); if (h) h.textContent = m.arp ? m.arp + ' key' + (m.arp > 1 ? 's' : '') + ' held' : 'hold some keys'; }
    if (m.select) { const i = state.presets.findIndex(p => p.name === m.select); if (i >= 0 && !(state.current?.type === 'preset' && state.current.key === i)) { state.tab = 'pre'; await selectPreset(i); renderList(); revealCurrent?.(); } }
  } else if (m.type === 'hostParams') { onHostParams(m.values || {});
  } else if (m.type === 'learned') { if (learnIdx === m.index) learnCc(m.cc);
  } else if (m.type === 'patchFile') {
    const n0 = state.patches.length, n = loadPatchJSON(m.text, m.name);
    if (n) { state.tab = 'pat'; renderList(); selectPatch(n0); toast(`Opened ${n} patch${n > 1 ? 'es' : ''}.`); } else toast('That file isn’t an Xtreme Lab patch.');
  } else if (m.type === 'toast') toast(m.text);
}
// ---------- panel knobs as host parameters (Ableton macros, automation, Configure) ----------
// each acts on what its card is editing (the selected layer, LFO, function generator), in the card's own units
const id1 = v => v;
const PNL = {
  pFiltFreq: ['filt-freq', id1, id1], pFiltRes: ['filt-res', id1, id1], pFiltEnv: ['filt-env', id1, id1], pFiltDecay: ['filt-decay', id1, id1],
  pEnvA: ['env-a', envU, envS], pEnvD: ['env-d', envU, envS], pEnvS: ['env-s', id1, id1], pEnvR: ['env-r', envU, envS], pVel: ['env-vel', id1, id1], pEnvTime: ['env-time', id1, id1],
  pLfoRate: ['lfo-rate', uFromRate, rateFromU], pLfoDelay: ['lfo-delay', id1, id1], pLfoPitch: ['lfo-pitch', id1, id1], pLfoFilter: ['lfo-filter', id1, id1], pLfoVol: ['lfo-vol', id1, id1], pLfoPan: ['lfo-pan', id1, id1],
  pFgRate: ['fg-rate', FGR.from, FGR.to], pFgSteps: ['fg-end', id1, id1], pFgPitch: ['fg-pitch', id1, id1], pFgFilter: ['fg-filter', id1, id1], pFgVol: ['fg-vol', id1, id1], pFgPan: ['fg-pan', id1, id1],
  pLyVol: ['ly-vol', id1, id1], pLyPan: ['ly-pan', id1, id1], pLyCoarse: ['ly-coarse', id1, id1], pLyFine: ['ly-fine', id1, id1],
  pMfFreq: ['mf-freq', id1, id1], pMfRes: ['mf-res', id1, id1], pMfRate: ['mf-rate', uFromRate, rateFromU], pMfDepth: ['mf-depth', id1, id1],
  pTune: ['p-tune', id1, id1], pBend: ['p-bend', id1, id1]
};
const PNL_BY_INPUT = Object.fromEntries(Object.entries(PNL).map(([k, [inp]]) => [inp, k]));
let hostApplying = false;
document.addEventListener('input', e => {
  if (hostApplying) return;
  const t = e.target; if (!t || !t.id) return;
  const pid = PNL_BY_INPUT[t.id];
  if (pid) { const v = PNL[pid][2](parseFloat(t.value)); if (isFinite(v)) toNative({ type: 'param', id: pid, value: v }); return; }
  // the Controllers card: knobs A-P are host parameters too
  const k = t.dataset?.ctl; if (k != null) { const i = MIDI_AP.indexOf(+k); if (i >= 0) toNative({ type: 'param', id: 'ctl' + 'ABCDEFGHIJKLMNOP'[i], value: Math.round(parseFloat(t.value)) }); }
}, true);
function applyPanel(pid, v) {
  const [inpId, toSlider] = PNL[pid]; let inp = document.getElementById(inpId); if (!inp) return;
  if (inp.disabled && inpId.startsWith('filt-') && curLayer()) { filtEdit(() => {}); inp = document.getElementById(inpId); }   // a layer on its preset's filter gets its own copy
  if (inp.disabled) return;
  const sv = toSlider(v); if (!isFinite(sv)) return;
  hostApplying = true;
  try { inp.value = String(Math.max(+inp.min, Math.min(+inp.max, sv))); inp.dispatchEvent(new Event('input', { bubbles: true })); }
  finally { hostApplying = false; }
}
// the host moved a parameter the page shows elsewhere (effects, performance, master)
const CH = { fxaType: ['classic', 'room', 'hall', 'plate', 'ambience'], fxbType: ['chorus', 'flanger', 'phaser', 'delay', 'drive'], keys: ['preset', 'poly', 'mono', 'legato'], glideMode: ['preset', 'off', 'custom'],
  arpMode: ['off', 'on', 'latch'], arpPattern: ['up', 'down', 'updown', 'downup', 'played', 'random', 'chord'], arpRate: ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32'], dlMode: ['repitch', 'fade', 'jump'],
  phShape: ['sine', 'triangle', 'square', 'saw', 'env'], drStyle: ['A', 'E', 'N', 'T', 'P'] };
function hostSet(id, v) {
  const A = fxState.a, B = fxState.b, X = B.x || {}, D = X.drive || {}, F = X.flanger || {}, H = X.phaser || {}, E = fxState.eq, C = fxState.color, bo = v > .5, c = l => l[Math.round(v)] ?? l[0];
  const set = {
    master: () => setMaster(v), sendA: () => fxState.send.a = v, sendB: () => fxState.send.b = v,
    keys: () => perf.keys = c(CH.keys), glideMode: () => perf.glide = c(CH.glideMode), glideMs: () => perf.glideMs = v,
    arpMode: () => perf.arp = c(CH.arpMode), arpPattern: () => perf.pattern = c(CH.arpPattern), arpRate: () => perf.rate = c(CH.arpRate), arpOct: () => perf.oct = Math.round(v),
    arpGate: () => perf.gate = v, arpSwing: () => perf.swing = v, arpSync: () => perf.sync = v < .5 ? 'clock' : 'int', arpTempo: () => perf.tempo = v,
    colOn: () => C.on = bo, colMono: () => C.mono = bo, colWarmth: () => C.warmth = v, colWeight: () => C.weight = v, colSub: () => C.sub = v, colSmooth: () => C.smooth = v,
    fxaOn: () => A.on = bo, fxaType: () => A.type = c(CH.fxaType), fxaSize: () => A.size = v, fxaDecay: () => A.decay = v, fxaPre: () => A.pre = v, fxaDamp: () => A.damp = v, fxaWidth: () => A.width = v, fxaTone: () => A.tone = v, fxaRet: () => A.ret = v,
    fxbOn: () => B.on = bo, fxbType: () => B.type = c(CH.fxbType), fxbIns: () => B.insert = bo, fxbRet: () => B.ret = v, chRate: () => B.rate = v, chDepth: () => B.depth = v,
    dlSync: () => B.dsync = bo, dlDivL: () => B.divL = [1, 2, 3, 4, 5, 6, 8, 16][Math.round(v)] ?? 3, dlDivR: () => B.divR = [1, 2, 3, 4, 5, 6, 8, 16][Math.round(v)] ?? 4, dlOffL: () => B.offL = v, dlOffR: () => B.offR = v,
    dlMsL: () => B.time = v, dlMsR: () => B.msR = v, dlLink: () => B.link = bo, dlPP: () => B.pp = bo, dlFreeze: () => B.freeze = bo, dlFb: () => B.fb = v, dlFilt: () => B.filt = bo, dlFreq: () => B.ffreq = v, dlWidth: () => B.fwidth = v,
    dlMRate: () => B.mrate = v, dlMFilt: () => B.mfilt = v, dlMTime: () => B.mtime = v, dlMode: () => B.dmode = c(CH.dlMode),
    flSync: () => F.sync = bo, flBeats: () => F.beats = [.5, 1, 2, 4, 8, 16, 32, 64][Math.round(v)] ?? 16, flRate: () => F.rate = v, flDepth: () => F.depth = v, flFb: () => F.fb = v, flSpread: () => F.spread = v, flMix: () => F.mix = v,
    phStages: () => H.stages = [4, 6, 8, 12, 16, 24][Math.round(v)] ?? 8, phFreq: () => H.freq = v, phDepth: () => H.depth = v, phShape: () => H.shape = c(CH.phShape), phSync: () => H.sync = bo,
    phBeats: () => H.beats = [.25, .5, 1, 2, 4, 8, 16][Math.round(v)] ?? 4, phRate: () => H.rate = v, phRes: () => H.res = v, phWidth: () => H.width = v, phMix: () => H.mix = v, phAnalog: () => H.analog = bo,
    drStyle: () => D.style = c(CH.drStyle), drDrive: () => D.drive = v, drPunish: () => D.punish = bo, drAuto: () => D.auto = bo, drThump: () => D.thump = bo, drSteep: () => D.steep = bo,
    drLowcut: () => D.lowcut = v <= 10.5 ? 0 : v, drTone: () => D.tone = v, drHighcut: () => D.highcut = v >= 19900 ? 0 : v, drMix: () => D.mix = v, drOut: () => D.out = v,
    eqLow: () => E.low = v, eqMid: () => E.mid = v, eqMidHz: () => E.midHz = v, eqHigh: () => E.high = v
  }[id];
  if (!set) return false;
  set(); sentParams[id] = paramMap()[id]; return true;
}
function onHostParams(vals) {
  let fx = false, pf = false;
  for (const [id, v] of Object.entries(vals)) {
    if (PNL[id]) { applyPanel(id, v); continue; }
    if (hostSet(id, v)) { if (/^(keys|glide|arp)/.test(id)) pf = true; else fx = true; }
  }
  if (fx) { fxRender(); fxChanged(); } if (pf) { perfRender(); if (curPatch()) curPatch().perf = { ...perf }; }
}

function syncAll() { syncEngine(); syncKnobs(); syncParams(); }
function bridgeStart() {
  if (!PLUGIN) { toast('Open this page in the Xtreme Lab plugin.'); return; }
  pluginFace();
  XB.addEventListener('exw', onNative);
  setInterval(() => { syncEngine(); syncKnobs(); syncParams(); }, 30);
  setInterval(syncUi, 700);
  toNative({ type: 'ready' });
}
