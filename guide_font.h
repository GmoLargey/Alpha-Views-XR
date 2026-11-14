// SPDX-License-Identifier: GPL-3.0-only
// Guide panel drawing on Linux, in place of GDI: text by stb_truetype (the desktop's sans-serif font,
// sized to take about Segoe UI's width, as the panel is laid out for it), rounded rectangles, circles
// and the built-in gesture pictures (24-bit BMPs, linux/embed_bmps.cmake). Included by layer.cpp only.
#pragma once
#ifndef _WIN32
#include <strings.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include "gesture_bmps.h"   // struct EmbeddedBmp kGestureBmps[] = { { L"FRAME", data, size }, ... }

struct GuideFontFile { std::vector<unsigned char> data; stbtt_fontinfo info{}; bool ok = false; float fit = 1; };
static bool LoadGuideFontFile(GuideFontFile& f, const std::string& path, int weight) {
    if (path.empty()) return false;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return false; }
    f.data.resize((size_t)sz);
    size_t got = fread(f.data.data(), 1, (size_t)sz, fp);
    fclose(fp);
    int off = got == (size_t)sz ? stbtt_GetFontOffsetForIndex(f.data.data(), 0) : -1;
    if (off < 0 || !stbtt_InitFont(&f.info, f.data.data(), off)) { f.data.clear(); return false; }
    f.ok = true;
    const char* abc = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    float em = stbtt_ScaleForMappingEmToPixels(&f.info, 1), w = 0;
    for (const char* p = abc; *p; ++p) { int adv, lsb; stbtt_GetCodepointHMetrics(&f.info, *p, &adv, &lsb); w += adv * em; }
    const float kSegoeWidth[3] = { 29.02f, 30.05f, 31.30f };   // Segoe UI regular, semibold, bold: the string above, 1 px em
    f.fit = std::clamp(kSegoeWidth[weight] / std::max(1.0f, w), 0.8f, 1.0f);
    return true;
}
static std::string FindGuideFont(int weight) {   // 0 regular, 1 semibold, 2 bold
    static const char* kFc[3] = { "sans-serif", "sans-serif:semibold", "sans-serif:bold" };
    std::string cmd = std::string("fc-match -f '%{file}' '") + kFc[weight] + "' 2>/dev/null";
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[1024] = ""; size_t n = fread(buf, 1, sizeof buf - 1, p); buf[n] = 0;
        pclose(p);
        std::string s = buf;
        auto ends = [&](const char* e) { size_t l = strlen(e); return s.size() > l && strcasecmp(s.c_str() + s.size() - l, e) == 0; };
        if ((ends(".ttf") || ends(".otf") || ends(".ttc")) && FileExists(s)) return s;
    }
    static const char* kDirs[] = { "/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/TTF/", "/usr/share/fonts/dejavu/", "/usr/share/fonts/dejavu-sans-fonts/",
                                   "/usr/share/fonts/noto/", "/usr/share/fonts/truetype/noto/", "/usr/share/fonts/google-noto/",
                                   "/usr/share/fonts/liberation/", "/usr/share/fonts/truetype/liberation/", "/usr/share/fonts/liberation-sans/" };
    static const char* kFiles[3][4] = {
        { "DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "" },
        { "NotoSans-SemiBold.ttf", "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf" },
        { "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf", "" } };
    for (const char* f : kFiles[weight])
        for (const char* d : kDirs) if (*f && FileExists(std::string(d) + f)) return std::string(d) + f;
    return {};
}
static GuideFontFile g_guideFontFile[3];

struct GuideFont {
    GuideFontFile* file = nullptr;
    float scale = 0;
    int ascent = 0, descent = 0, lineH = 0;   // lineH: as Segoe UI's (1.33 em)
    struct Glyph { std::vector<uint8_t> a; int w = 0, h = 0, x0 = 0, y0 = 0; };
    std::map<uint32_t, Glyph> cache;   // codepoint * 4 + quarter-pixel offset
    bool Init(int weight, float px) {
        static bool tried = false;
        if (!tried) {
            tried = true;
            for (int i = 0; i < 3; ++i) {
                std::string p = FindGuideFont(i);
                if (LoadGuideFontFile(g_guideFontFile[i], p, i)) Log("Guide font %d: %s\n", i, p.c_str());
                else Log("Guide font %d: none found\n", i);
            }
            for (int i = 0; i < 3; ++i)
                if (!g_guideFontFile[i].ok)
                    for (int j : { 0, 2, 1 })
                        if (g_guideFontFile[j].ok) {
                            g_guideFontFile[i] = g_guideFontFile[j];
                            stbtt_InitFont(&g_guideFontFile[i].info, g_guideFontFile[i].data.data(), stbtt_GetFontOffsetForIndex(g_guideFontFile[i].data.data(), 0));
                            break;
                        }
        }
        file = &g_guideFontFile[weight];
        if (!file->ok) return false;
        scale = stbtt_ScaleForMappingEmToPixels(&file->info, px * file->fit);   // as GDI's CreateFont(-px)
        int a, d, g; stbtt_GetFontVMetrics(&file->info, &a, &d, &g);
        ascent = (int)ceilf(a * scale); descent = (int)floorf(d * scale);
        lineH = std::max(ascent - descent, (int)lroundf(px * 1.33f));
        return true;
    }
    bool Ok() const { return file && file->ok; }
    float Advance(uint32_t cp) const { int adv, lsb; stbtt_GetCodepointHMetrics(&file->info, (int)cp, &adv, &lsb); return adv * scale; }
    float Kern(uint32_t a, uint32_t b) const { return stbtt_GetCodepointKernAdvance(&file->info, (int)a, (int)b) * scale; }
    bool Has(uint32_t cp) const { return stbtt_FindGlyphIndex(&file->info, (int)cp) != 0; }
    const Glyph& Get(uint32_t cp, int sub) {
        auto it = cache.find(cp * 4 + sub);
        if (it != cache.end()) return it->second;
        Glyph& g = cache[cp * 4 + sub];
        float sx = sub * 0.25f;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&file->info, (int)cp, scale, scale, sx, 0, &x0, &y0, &x1, &y1);
        g.w = x1 - x0; g.h = y1 - y0; g.x0 = x0; g.y0 = y0;
        if (g.w > 0 && g.h > 0) {
            g.a.assign((size_t)g.w * g.h, 0);
            stbtt_MakeCodepointBitmapSubpixel(&file->info, g.a.data(), g.w, g.h, g.w, scale, scale, sx, 0, (int)cp);
        }
        return g;
    }
};

// The picture: BGRA (0xFFRRGGBB), as the GDI version's DIB.
struct GuideCanvas {
    int W = 0, H = 0;
    std::vector<uint32_t> px;
    void Blend(int x, int y, COLORREF c, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
        uint32_t& d = px[(size_t)y * W + x];
        float s[3] = { float(c & 255), float((c >> 8) & 255), float((c >> 16) & 255) };   // r g b
        float o[3] = { float((d >> 16) & 255), float((d >> 8) & 255), float(d & 255) };
        a = std::min(a, 1.0f);
        uint32_t r = (uint32_t)(o[0] + (s[0] - o[0]) * a + 0.5f), g = (uint32_t)(o[1] + (s[1] - o[1]) * a + 0.5f), b = (uint32_t)(o[2] + (s[2] - o[2]) * a + 0.5f);
        d = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    void Fill(RECT r, COLORREF c) {
        uint32_t v = 0xFF000000u | ((c & 255) << 16) | (c & 0xFF00) | ((c >> 16) & 255);
        for (int y = std::max(0, (int)r.top); y < std::min(H, (int)r.bottom); ++y)
            for (int x = std::max(0, (int)r.left); x < std::min(W, (int)r.right); ++x) px[(size_t)y * W + x] = v;
    }
    // As GDI's RoundRect (rad = the corners' diameter); rad < 0: an ellipse.
    void Shape(RECT r, int rad, COLORREF c) {
        if (rad < 0) {
            float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, rx = (r.right - r.left) * 0.5f, ry = (r.bottom - r.top) * 0.5f;
            for (int y = std::max(0, (int)r.top); y < std::min(H, (int)r.bottom); ++y)
                for (int x = std::max(0, (int)r.left); x < std::min(W, (int)r.right); ++x) {
                    float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
                    float d = (sqrtf(dx * dx + dy * dy) - 1) * std::min(rx, ry);
                    Blend(x, y, c, std::clamp(0.5f - d, 0.0f, 1.0f));
                }
            return;
        }
        float R = rad * 0.5f, cx0 = r.left + R, cx1 = r.right - R, cy0 = r.top + R, cy1 = r.bottom - R;
        for (int y = std::max(0, (int)r.top); y < std::min(H, (int)r.bottom); ++y)
            for (int x = std::max(0, (int)r.left); x < std::min(W, (int)r.right); ++x) {
                float fx = x + 0.5f, fy = y + 0.5f;
                float dx = std::max({ cx0 - fx, fx - cx1, 0.0f }), dy = std::max({ cy0 - fy, fy - cy1, 0.0f });
                float d = sqrtf(dx * dx + dy * dy) - R;
                Blend(x, y, c, std::clamp(0.5f - d, 0.0f, 1.0f));
            }
    }
    static float Measure(const std::wstring& s, GuideFont* f) {
        if (!f || !f->Ok()) return 0;
        float x = 0; uint32_t prev = 0;
        for (wchar_t ch : s) { uint32_t cp = (uint32_t)ch; if (prev) x += f->Kern(prev, cp); x += f->Advance(cp); prev = cp; }
        return x;
    }
    void Line(const std::wstring& s, GuideFont* f, COLORREF c, float x, int base, RECT clip) {
        uint32_t prev = 0;
        for (wchar_t ch : s) {
            uint32_t cp = (uint32_t)ch;
            if (prev) x += f->Kern(prev, cp);
            int ix = (int)floorf(x), sub = std::min(3, (int)((x - ix) * 4));
            const GuideFont::Glyph& g = f->Get(cp, sub);
            for (int gy = 0; gy < g.h; ++gy) {
                int y = base + g.y0 + gy;
                if (y < clip.top || y >= clip.bottom) continue;
                for (int gx = 0; gx < g.w; ++gx) {
                    int xx = ix + g.x0 + gx;
                    if (xx < clip.left || xx >= clip.right) continue;
                    if (uint8_t a = g.a[(size_t)gy * g.w + gx]) Blend(xx, y, c, a / 255.0f);
                }
            }
            x += f->Advance(cp);
            prev = cp;
        }
    }
    // As DrawText's lines: \n, word wrap (DT_WORDBREAK), "..." when too long (DT_END_ELLIPSIS).
    static std::vector<std::wstring> Lines(const std::wstring& s, GuideFont* f, float width, UINT flags) {
        std::vector<std::wstring> lines;
        if (flags & DT_SINGLELINE) { std::wstring l = s; std::replace(l.begin(), l.end(), L'\n', L' '); lines.push_back(l); }
        else {
            std::wstring cur;
            for (size_t i = 0; i <= s.size(); ++i) {
                if (i < s.size() && s[i] != L'\n') { cur += s[i]; continue; }
                if (!(flags & DT_WORDBREAK) || Measure(cur, f) <= width) { lines.push_back(cur); cur.clear(); continue; }
                std::wstring line;
                size_t p = 0;
                while (p < cur.size()) {
                    size_t q = cur.find(L' ', p);
                    if (q == std::wstring::npos) q = cur.size();
                    std::wstring word = cur.substr(p, q - p);
                    std::wstring tryLine = line.empty() ? word : line + L" " + word;
                    if (!line.empty() && Measure(tryLine, f) > width) { lines.push_back(line); line = word; }
                    else line = tryLine;
                    p = q + 1;
                }
                lines.push_back(line);
                cur.clear();
            }
        }
        if (flags & DT_END_ELLIPSIS) {
            const std::wstring dots = f->Has(0x2026) ? std::wstring(1, (wchar_t)0x2026) : std::wstring(L"...");
            for (auto& l : lines) {
                if (Measure(l, f) <= width) continue;
                while (!l.empty() && Measure(l + dots, f) > width) l.pop_back();
                while (!l.empty() && l.back() == L' ') l.pop_back();
                l += dots;
            }
        }
        return lines;
    }
    void Text(const std::wstring& s, GuideFont* f, COLORREF c, RECT r, UINT flags) {
        if (!f || !f->Ok()) return;
        const float width = (float)(r.right - r.left);
        std::vector<std::wstring> lines = Lines(s, f, width, flags);
        const int pad = (f->lineH - (f->ascent - f->descent)) / 2;
        int y = r.top;
        if ((flags & DT_SINGLELINE) && (flags & DT_VCENTER)) y = r.top + (r.bottom - r.top - f->lineH) / 2;
        for (auto& l : lines) {
            float w = Measure(l, f);
            float x = (flags & DT_CENTER) ? r.left + (width - w) * 0.5f : (flags & DT_RIGHT) ? r.right - w : (float)r.left;
            Line(l, f, c, x, y + pad + f->ascent, { std::max((int)r.left, 0), std::max((int)r.top, 0), std::min((int)r.right, W), std::min((int)r.bottom, H) });
            y += f->lineH;
        }
    }
    static int Height(const std::wstring& s, GuideFont* f, RECT r) {
        if (!f || !f->Ok()) return 0;
        return (int)Lines(s, f, (float)(r.right - r.left), DT_WORDBREAK).size() * f->lineH;
    }
    // A built-in 24-bit BMP, scaled (area average) into r.
    void Picture(const wchar_t* name, RECT r) {
        const EmbeddedBmp* e = nullptr;
        for (auto& b : kGestureBmps) if (b.name && wcscmp(b.name, name) == 0) e = &b;
        if (!e || e->size < 54 || e->data[0] != 'B' || e->data[1] != 'M') return;
        auto u32 = [&](size_t o) { return (uint32_t)e->data[o] | ((uint32_t)e->data[o + 1] << 8) | ((uint32_t)e->data[o + 2] << 16) | ((uint32_t)e->data[o + 3] << 24); };
        uint32_t off = u32(10);
        int bw = (int)u32(18), bh = (int)u32(22), bpp = e->data[28] | (e->data[29] << 8);
        bool flip = bh > 0; bh = abs(bh);
        size_t stride = ((size_t)bw * 3 + 3) & ~(size_t)3;
        if (bpp != 24 || bw <= 0 || off + stride * bh > e->size) return;
        const int dw = r.right - r.left, dh = r.bottom - r.top;
        for (int y = 0; y < dh; ++y) {
            if (r.top + y < 0 || r.top + y >= H) continue;
            float sy0 = (float)y * bh / dh, sy1 = (float)(y + 1) * bh / dh;
            for (int x = 0; x < dw; ++x) {
                if (r.left + x < 0 || r.left + x >= W) continue;
                float sx0 = (float)x * bw / dw, sx1 = (float)(x + 1) * bw / dw, acc[3] = {}, wsum = 0;
                for (int sy = (int)sy0; sy < std::min(bh, (int)ceilf(sy1)); ++sy) {
                    float wy = std::min(sy1, sy + 1.0f) - std::max(sy0, (float)sy);
                    const unsigned char* row = e->data + off + stride * (flip ? bh - 1 - sy : sy);
                    for (int sx = (int)sx0; sx < std::min(bw, (int)ceilf(sx1)); ++sx) {
                        float wgt = wy * (std::min(sx1, sx + 1.0f) - std::max(sx0, (float)sx));
                        for (int k = 0; k < 3; ++k) acc[k] += row[sx * 3 + k] * wgt;   // b g r
                        wsum += wgt;
                    }
                }
                if (wsum <= 0) continue;
                uint32_t b = (uint32_t)(acc[0] / wsum + 0.5f), g = (uint32_t)(acc[1] / wsum + 0.5f), rr = (uint32_t)(acc[2] / wsum + 0.5f);
                px[(size_t)(r.top + y) * W + r.left + x] = 0xFF000000u | (rr << 16) | (g << 8) | b;
            }
        }
    }
};
#endif
