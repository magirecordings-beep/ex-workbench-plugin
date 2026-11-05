#include "Bank.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace exw {
namespace {

using Bytes = std::vector<uint8_t>;

bool readFile (const std::string& path, Bytes& out, size_t limit = 0) {
    std::ifstream f (fs::u8path (path), std::ios::binary);
    if (! f) return false;
    f.seekg (0, std::ios::end); auto size = (size_t) f.tellg(); f.seekg (0);
    if (limit && size > limit) size = limit;
    out.resize (size);
    return (bool) f.read ((char*) out.data(), (std::streamsize) size) || f.gcount() == (std::streamsize) size;
}

struct Reader {
    const Bytes& b;
    size_t n() const { return b.size(); }
    uint8_t u8 (size_t o) const { return o < b.size() ? b[o] : 0; }
    int8_t i8 (size_t o) const { return (int8_t) u8 (o); }
    uint16_t u16be (size_t o) const { return (uint16_t) ((u8 (o) << 8) | u8 (o + 1)); }
    uint16_t u16le (size_t o) const { return (uint16_t) (u8 (o) | (u8 (o + 1) << 8)); }
    uint32_t u32be (size_t o) const { return ((uint32_t) u8 (o) << 24) | ((uint32_t) u8 (o + 1) << 16) | ((uint32_t) u8 (o + 2) << 8) | u8 (o + 3); }
    uint32_t u32le (size_t o) const { return u8 (o) | ((uint32_t) u8 (o + 1) << 8) | ((uint32_t) u8 (o + 2) << 16) | ((uint32_t) u8 (o + 3) << 24); }
    float f32be (size_t o) const { uint32_t v = u32be (o); float f; std::memcpy (&f, &v, 4); return std::isfinite (f) ? f : 0.0f; }
    std::string id (size_t o) const { std::string s (4, ' '); for (int i = 0; i < 4; ++i) s[i] = (char) u8 (o + i); return s; }
};

bool printable (const std::string& s) { for (char c : s) if (c < 0x20 || c > 0x7e) return false; return s.size() == 4; }

void appendUtf8 (std::string& s, uint32_t c) {
    if (c < 0x80) s += (char) c;
    else if (c < 0x800) { s += (char) (0xc0 | (c >> 6)); s += (char) (0x80 | (c & 63)); }
    else { s += (char) (0xe0 | (c >> 12)); s += (char) (0x80 | ((c >> 6) & 63)); s += (char) (0x80 | (c & 63)); }
}
std::string trim (std::string s) {
    while (! s.empty() && (unsigned char) s.back() <= ' ') s.pop_back();
    size_t i = 0; while (i < s.size() && (unsigned char) s[i] <= ' ') ++i;
    return s.substr (i);
}
std::string readName (const Reader& r, size_t o, size_t len, bool wide) {
    std::string s;
    size_t end = std::min (o + len, r.n());
    if (wide) { for (size_t p = o; p + 1 < end; p += 2) { uint16_t c = r.u16le (p); if (! c) break; if (c >= 0x20) appendUtf8 (s, c); } }
    else { for (size_t p = o; p < end; ++p) { uint8_t c = r.u8 (p); if (! c) break; if (c >= 0x20) s += (char) c; } }
    return trim (s);
}

struct Chunk { std::string id, name; size_t off = 0, start = 0; uint32_t len = 0; int index = -1; };
struct Form { bool ok = false; std::string type, error; bool wide = false; std::vector<Chunk> chunks; };

// FORM <size> <type>, then <id><size BE><data> chunks, with a TOC naming them
Form parseForm (const Reader& r) {
    Form f;
    if (r.n() < 12 || r.id (0) != "FORM") { f.error = "Not an E-MU file (no FORM header)."; return f; }
    f.type = r.id (8); f.wide = f.type.rfind ("E5", 0) == 0;
    for (size_t o = 12; o + 8 <= r.n();) {
        auto id = r.id (o); if (! printable (id)) break;
        uint32_t len = r.u32be (o + 4);
        f.chunks.push_back ({ id, {}, o, o + 8, len, -1 });
        if (o + 8 + (size_t) len > r.n()) break;
        o += 8 + len;
    }
    const Chunk* tc = nullptr;
    for (auto& c : f.chunks) if (c.id.rfind ("TOC", 0) == 0) { tc = &c; break; }
    if (tc) {
        std::vector<int> cands = f.wide ? std::vector<int> { 78, 30, 32 } : std::vector<int> { 30, 32, 78 };
        int es = cands[0];
        for (int c : cands) if (tc->len % c == 0 && printable (r.id (tc->start))) { es = c; break; }
        int nameLen = es - 14;
        struct T { std::string id; uint32_t len, off; int index; std::string name; };
        std::vector<T> toc;
        for (size_t p = tc->start; p + es <= tc->start + tc->len && p + es <= r.n(); p += es) {
            auto id = r.id (p); if (! printable (id)) break;
            toc.push_back ({ id, r.u32be (p + 4), r.u32be (p + 8), r.u16be (p + 12), readName (r, p + 14, (size_t) nameLen, nameLen >= 64) });
        }
        std::map<size_t, const T*> byOff; for (auto& t : toc) byOff[t.off] = &t;
        for (auto& c : f.chunks) if (auto it = byOff.find (c.off); it != byOff.end()) { c.index = it->second->index; c.name = it->second->name; }
    }
    for (auto& c : f.chunks)
        if (c.name.empty() && c.id.rfind ("TOC", 0) != 0 && c.start + 22 <= r.n())
            c.name = f.wide ? (readName (r, c.start + 6, 64, true).empty() ? readName (r, c.start + 2, 64, true) : readName (r, c.start + 6, 64, true))
                            : readName (r, c.start + 2, 16, false);
    f.ok = true;
    return f;
}

// Sample chunk: eight LE byte offsets [startL, startR, endL, endR, loopStartL, loopStartR, loopEndL, loopEndR]
// (counted from the byte after the 2-byte sample index), then the rate. Scan for the self-consistent layout.
bool sampleFromChunk (const Reader& r, const Chunk& c, Sample& s, size_t& dataL, size_t& dataR) {
    if (c.start + 64 > r.n()) return false;
    int limit = (int) std::min<int64_t> (260, (int64_t) c.len - 44);
    for (int k = 0; k <= limit; k += 2) {
        size_t base = c.start + (size_t) k;
        if (base + 44 > r.n()) break;
        uint32_t a[8]; for (int i = 0; i < 8; ++i) a[i] = r.u32le (base + (size_t) i * 4);
        uint32_t rate = r.u32le (base + 32);
        if (rate < 4000 || rate > 192000) continue;
        uint32_t sL = a[0], sR = a[1], eL = a[2], eR = a[3];
        if (sL < (uint32_t) k + 36 || sR < sL || eL < sL || eR < sR) continue;
        if ((uint64_t) eL + 4 > c.len || (uint64_t) eR + 4 > c.len) continue;
        if ((eL - sL) % 2 || (eR - sR) % 2) continue;
        bool stereo = sR != sL;
        int framesL = (int) (eL - sL) / 2 + 1, framesR = stereo ? (int) (eR - sR) / 2 + 1 : framesL;
        s.frames = std::min (framesL, framesR); s.rate = (int) rate; s.channels = stereo ? 2 : 1;
        int64_t lsL = a[4], leL = a[6];
        int loopStart = (int) std::max<int64_t> (0, (lsL - (int64_t) sL) / 2), loopEnd = (int) std::max<int64_t> (0, (leL - (int64_t) sL) / 2);
        s.loop = lsL >= (int64_t) sL && leL > lsL && loopEnd < s.frames;
        s.loopStart = s.loop ? loopStart : 0; s.loopEnd = s.loop ? std::min (loopEnd, s.frames - 1) : s.frames - 1;
        dataL = c.start + 2 + sL; dataR = stereo ? c.start + 2 + sR : 0;
        return true;
    }
    return false;
}

void decodePcm (const Reader& r, size_t off, int frames, std::vector<int16_t>& out) {
    out.assign ((size_t) frames, 0);
    int n = (int) std::min<int64_t> (frames, (int64_t) (r.n() > off ? (r.n() - off) / 2 : 0));
    for (int i = 0; i < n; ++i) out[(size_t) i] = (int16_t) r.u16le (off + (size_t) i * 2);
}

// ---- presets (E5P1): big-endian nested chunks, LIST <type> containers ----
template <typename F>
void walkSubs (const Reader& r, size_t o, size_t end, F&& cb) {
    while (o + 8 <= end) {
        auto id = r.id (o); if (! printable (id)) return;
        uint32_t len = r.u32be (o + 4); size_t ds = o + 8, de = std::min (ds + (size_t) len, end);
        if (id == "LIST") cb ("LIST:" + r.id (ds), ds + 4, de); else cb (id, ds, de);
        o = ds + len;
    }
}
float clampf (float v, float a, float b) { return std::max (a, std::min (b, v)); }
Range win (const Reader& r, size_t o) { return { r.u8 (o), r.u8 (o + 3) }; }
template <typename T> void windows (const Reader& r, size_t a, size_t b, T& target) {
    int i = 0;
    walkSubs (r, a, b, [&] (const std::string& w, size_t wa, size_t) {
        if (w != "ETW ") return;
        if (i == 0) target.key = win (r, wa + 4); else if (i == 1) target.vel = win (r, wa + 4);
        ++i;
    });
}
Env6 parseEnv (const Reader& r, size_t ds) {
    Env6 e; for (int i = 0; i < 6; ++i) { size_t o = ds + 10 + (size_t) i * 9; e.t[i] = clampf (r.f32be (o), 0, 60); e.l[i] = clampf (r.f32be (o + 4), -100, 100); }
    return e;
}
Preset parsePreset (const Reader& r, const Chunk& c) {
    Preset pre; pre.name = c.name;
    walkSubs (r, c.start + 2, c.start + c.len, [&] (const std::string& id, size_t ds, size_t de) {
        if (id == "E5IC" && de - ds >= 20) { pre.hasIc = true; for (int i = 0; i < 16; ++i) pre.ic[(size_t) i] = (uint8_t) std::min<int> (127, r.u8 (ds + 4 + (size_t) i)); return; }
        if (id == "E5CL" && de - ds >= 516) { for (int i = 0; i < 16; ++i) pre.labels[(size_t) i] = readName (r, ds + 4 + (size_t) i * 32, 32, true); return; }
        if (id != "LIST:E5VL") return;
        walkSubs (r, ds, de, [&] (const std::string& vid, size_t vs, size_t ve) {
            if (vid != "E5V1") return;
            Voice v;
            walkSubs (r, vs, ve, [&] (const std::string& t, size_t a, size_t b) {
                if (t == "LIST:TWL ") windows (r, a, b, v);
                else if (t == "Vhdr" && b - a >= 15) { v.bendUp = clampf (r.f32be (a + 7), 0, 36); v.bendDn = clampf (r.f32be (a + 11), 0, 36); }
                else if (t == "E5Oc" && b - a >= 31) {
                    v.transpose = r.i8 (a + 14); v.coarse = r.i8 (a + 15); v.fine = clampf (r.f32be (a + 16), -100, 100);
                    v.glide = clampf (r.f32be (a + 20), 0, 32.738f); v.glideCurve = r.u8 (a + 24);
                    int km = r.u8 (a + 25); v.kb = km <= 8 ? km : 0;
                }
                else if (t == "E5Am" && b - a >= 9) { v.volDb = clampf (r.f32be (a + 4), -96, 24); v.pan = r.i8 (a + 8); }
                else if (t == "E5Fl" && b - a >= 13) { v.hasFilter = true; v.ftype = r.u8 (a + 4); v.ffreq = clampf (r.f32be (a + 5), 0, 1); v.fq = clampf (r.f32be (a + 9), 0, 1); }
                else if (t == "LIST:EvL ") walkSubs (r, a, b, [&] (const std::string& e, size_t ea, size_t eb) { if (e == "E5Ev" && eb - ea >= 64) v.envs.push_back (parseEnv (r, ea)); });
                else if (t == "LIST:CrdL") walkSubs (r, a, b, [&] (const std::string& e, size_t ea, size_t eb) { if (e == "E5Cd" && eb - ea >= 10) v.cords.push_back ({ r.u8 (ea + 4), r.u8 (ea + 5), clampf (r.f32be (ea + 6), -100, 100) }); });
                else if (t == "LIST:LFOL") walkSubs (r, a, b, [&] (const std::string& e, size_t ea, size_t eb) { if (e == "E5LF" && eb - ea >= 22) v.plfos.push_back ({ clampf (r.f32be (ea + 4), 0, 1), clampf (r.f32be (ea + 8), .01f, 50), r.u8 (ea + 12) }); });
                else if (t == "LIST:E5ZL") {
                    Zone* z = nullptr;
                    walkSubs (r, a, b, [&] (const std::string& zt, size_t za, size_t zb) {
                        if (zt == "Zhdr" && zb - za >= 15) {
                            float fine = r.f32be (za + 11);
                            Zone nz; nz.sample = r.u16be (za + 4); nz.root = r.u8 (za + 10); nz.fine = std::fabs (fine) <= 100 ? fine : 0;
                            v.zones.push_back (nz); z = &v.zones.back();
                        } else if (zt == "LIST:TWL " && z) windows (r, za, zb, *z);
                    });
                }
            });
            pre.voices.push_back (std::move (v));
        });
    });
    return pre;
}

std::string norm (std::string s) {
    std::string o; bool sp = false;
    for (char c : s) { if (std::isspace ((unsigned char) c)) { sp = ! o.empty(); continue; } if (sp) { o += ' '; sp = false; } o += (char) std::tolower ((unsigned char) c); }
    return o;
}

} // namespace

std::unique_ptr<Sample> loadEbl (const std::string& path) {
    Bytes b; if (! readFile (path, b)) return nullptr;
    Reader r { b };
    Form f = parseForm (r); if (! f.ok) return nullptr;
    for (auto& c : f.chunks) {
        if (c.id.size() == 4 && c.id.compare (2, 2, "S1") == 0) {
            auto s = std::make_unique<Sample>();
            size_t dl = 0, dr = 0;
            if (! sampleFromChunk (r, c, *s, dl, dr)) return nullptr;
            s->name = c.name.empty() ? fs::u8path (path).stem().u8string() : c.name;
            decodePcm (r, dl, s->frames, s->L);
            if (s->channels == 2) decodePcm (r, dr, s->frames, s->R);
            return s;
        }
    }
    return nullptr;
}

std::string presetCategory (const std::string& name) {
    auto p = name.find (':');
    if (p == std::string::npos || p < 2 || p > 4) return {};
    std::string c = name.substr (0, p); for (auto& ch : c) ch = (char) std::tolower ((unsigned char) ch);
    return c;
}

std::shared_ptr<Bank> loadBank (const std::string& exbPath, const std::function<void (float)>& progress) {
    auto bank = std::make_shared<Bank>();
    bank->path = exbPath; bank->name = fs::u8path (exbPath).filename().u8string();
    Bytes b;
    if (! readFile (exbPath, b)) { bank->error = "Couldn't read " + bank->name + "."; return bank; }
    Reader r { b };
    Form f = parseForm (r);
    if (! f.ok) { bank->error = f.error; return bank; }
    for (auto& c : f.chunks) if (c.id == "E5SL") bank->slNames[c.index] = c.name;
    for (auto& c : f.chunks) if (c.id == "E5P1") { try { bank->presets.push_back (parsePreset (r, c)); } catch (...) {} }
    if (bank->presets.empty()) { bank->error = "No Emulator X presets found in " + bank->name + "."; return bank; }

    // which pool samples the presets use
    std::set<int> wanted;
    for (auto& p : bank->presets) for (auto& v : p.voices) for (auto& z : v.zones) wanted.insert (z.sample);

    // SamplePool folder next to the bank (or the bank's own folder)
    auto dir = fs::u8path (exbPath).parent_path();
    std::vector<fs::path> pools { dir / "SamplePool", dir };
    std::map<int, fs::path> byIndex; std::map<std::string, fs::path> byStem;
    static const std::regex slRe (R"(SL(\d+)\.ebl$)", std::regex::icase);
    for (auto& pd : pools) {
        std::error_code ec;
        if (! fs::is_directory (pd, ec)) continue;
        for (auto& e : fs::directory_iterator (pd, ec)) {
            auto fn = e.path().filename().u8string();
            if (fn.size() < 5 || fn[0] == '.') continue;
            std::string low = fn; for (auto& ch : low) ch = (char) std::tolower ((unsigned char) ch);
            if (low.size() < 4 || low.compare (low.size() - 4, 4, ".ebl") != 0) continue;
            std::smatch m;
            if (std::regex_search (fn, m, slRe)) byIndex.emplace (std::stoi (m[1].str()), e.path());
            byStem.emplace (norm (e.path().stem().u8string()), e.path());
        }
    }
    if (byIndex.empty() && byStem.empty()) { bank->error = "Found the presets, but no SamplePool folder with .ebl files next to " + bank->name + "."; return bank; }

    // load each wanted sample once
    std::map<int, const Sample*> loaded; std::map<std::string, const Sample*> loadedByPath;
    size_t done = 0;
    for (int idx : wanted) {
        fs::path p;
        if (auto it = byIndex.find (idx); it != byIndex.end()) p = it->second;
        else if (auto nm = bank->slNames.find (idx); nm != bank->slNames.end()) { if (auto it2 = byStem.find (norm (nm->second)); it2 != byStem.end()) p = it2->second; }
        if (! p.empty()) {
            auto key = p.u8string();
            if (auto lp = loadedByPath.find (key); lp != loadedByPath.end()) loaded[idx] = lp->second;
            else if (auto s = loadEbl (key)) { loaded[idx] = s.get(); loadedByPath[key] = s.get(); bank->samples.push_back (std::move (s)); }
        }
        if (progress && (++done % 16 == 0)) progress ((float) done / (float) wanted.size());
    }
    for (auto& pr : bank->presets) for (auto& v : pr.voices) for (auto& z : v.zones) {
        if (auto it = loaded.find (z.sample); it != loaded.end()) z.ref = it->second; else ++bank->missingZones;
    }
    if (progress) progress (1.0f);
    return bank;
}

} // namespace exw
