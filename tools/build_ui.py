#!/usr/bin/env python3
"""Builds the plugin's panel (ui/index.html) from the browser version (ui/xtreme-lab.html) plus ui/bridge.js.
The browser version stays the single source: every card and editor is the same code; the bridge swaps its Web Audio
engine for the plugin's. Run after changing either file:  python3 tools/build_ui.py"""
import pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
src = (root / 'ui/xtreme-lab.html').read_text()
bridge = (root / 'ui/bridge.js').read_text()

def sub(text, old, new, count=1):
    if old not in text: sys.exit('build_ui: not found in xtreme-lab.html: ' + old[:80])
    return text.replace(old, new, count)

# settings the page keeps (favourites, page, MIDI numbers...) live in the plugin; the editor puts them here when it serves the page
head = '''<script>
window.__EXW_LS = /*EXW_LS*/{};
(() => {
  const store = new Map(Object.entries(window.__EXW_LS || {}));
  const send = o => { try { window.__JUCE__ && window.__JUCE__.backend && window.__JUCE__.backend.emitEvent('exw', o); } catch (e) {} };
  const ls = { getItem: k => store.has(k) ? store.get(k) : null,
    setItem: (k, v) => { v = String(v); if (store.get(k) === v) return; store.set(k, v); send({ type: 'lsSet', k, v }); },
    removeItem: k => { if (store.delete(k)) send({ type: 'lsDel', k }); }, clear() {}, key: i => [...store.keys()][i] ?? null, get length() { return store.size; } };
  window.__exwLS = ls;
  try { Object.defineProperty(window, 'localStorage', { value: ls, configurable: true }); } catch (e) {}
})();
</script>
'''
out = sub(src, '<head>', '<head>\n' + head)
out = sub(out, '<script>\n(() => {\nconst $ = s =>', '<script>\n(() => {\nconst localStorage = window.__exwLS || window.localStorage; // the plugin keeps these settings\nconst $ = s =>')
out = sub(out, 'makeDemo(); renderControls(); bind();', 'renderControls(); bind();')
out = sub(out, "selectSample(1).then(() => { state.current.demo = true; libRestore(); libBadge(); });", bridge + '\nbridgeStart();\n')
(root / 'ui/index.html').write_text(out)
print('ui/index.html', len(out), 'bytes')
