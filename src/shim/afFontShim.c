#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// afFontShim.dll v4 - 字体列表：单遍枚举 + 结果缓存 + 回放期抑制 combo 去重循环
// tcc 编译，导出 afUILib 需要的全部 gdi32 函数名

static HMODULE g_gdi = NULL;
static HMODULE g_u32 = NULL;
static CRITICAL_SECTION g_cs;
static int g_enabled = 1, g_dedupe = 1, g_log = 1, g_useMem = 1, g_fastCombo = 1;
static BYTE g_charset_ok[256];
static char g_inipath[MAX_PATH];
static char g_logpath[MAX_PATH];

typedef COLORREF (WINAPI *PFN_GetTextColor)(HDC hdc);
static PFN_GetTextColor p_GetTextColor;
typedef BOOL (WINAPI *PFN_Polygon)(HDC hdc, const POINT *apt, int cpt);
static PFN_Polygon p_Polygon;
typedef HBRUSH (WINAPI *PFN_CreateSolidBrush)(COLORREF color);
static PFN_CreateSolidBrush p_CreateSolidBrush;
typedef DWORD (WINAPI *PFN_GetObjectType)(HGDIOBJ h);
static PFN_GetObjectType p_GetObjectType;
typedef int (WINAPI *PFN_GetDeviceCaps)(HDC hdc, int index);
static PFN_GetDeviceCaps p_GetDeviceCaps;
typedef int (WINAPI *PFN_SaveDC)(HDC hdc);
static PFN_SaveDC p_SaveDC;
typedef BOOL (WINAPI *PFN_RestoreDC)(HDC hdc, int nSavedDC);
static PFN_RestoreDC p_RestoreDC;
typedef HRGN (WINAPI *PFN_CreateRectRgn)(int x1, int y1, int x2, int y2);
static PFN_CreateRectRgn p_CreateRectRgn;
typedef HPEN (WINAPI *PFN_CreatePen)(int style, DWORD width, COLORREF color);
static PFN_CreatePen p_CreatePen;
typedef BOOL (WINAPI *PFN_Rectangle)(HDC hdc, int left, int top, int right, int bottom);
static PFN_Rectangle p_Rectangle;
typedef BOOL (WINAPI *PFN_PatBlt)(HDC hdc, int x, int y, int w, int h, DWORD rop);
static PFN_PatBlt p_PatBlt;
typedef HRGN (WINAPI *PFN_CreateRectRgnIndirect)(const RECT *lprc);
static PFN_CreateRectRgnIndirect p_CreateRectRgnIndirect;
typedef BOOL (WINAPI *PFN_GetTextMetricsW)(HDC hdc, LPTEXTMETRICW lptm);
static PFN_GetTextMetricsW p_GetTextMetricsW;
typedef BOOL (WINAPI *PFN_GetTextExtentPoint32W)(HDC hdc, LPCWSTR str, int c, LPSIZE size);
static PFN_GetTextExtentPoint32W p_GetTextExtentPoint32W;
typedef HBITMAP (WINAPI *PFN_CreateCompatibleBitmap)(HDC hdc, int w, int h);
static PFN_CreateCompatibleBitmap p_CreateCompatibleBitmap;
typedef HDC (WINAPI *PFN_CreateCompatibleDC)(HDC hdc);
static PFN_CreateCompatibleDC p_CreateCompatibleDC;
typedef int (WINAPI *PFN_GetTextCharset)(HDC hdc);
static PFN_GetTextCharset p_GetTextCharset;
typedef DWORD (WINAPI *PFN_SetLayout)(HDC hdc, DWORD layout);
static PFN_SetLayout p_SetLayout;
typedef int (WINAPI *PFN_SelectClipRgn)(HDC hdc, HRGN hrgn);
static PFN_SelectClipRgn p_SelectClipRgn;
typedef int (WINAPI *PFN_ExcludeClipRect)(HDC hdc, int left, int top, int right, int bottom);
static PFN_ExcludeClipRect p_ExcludeClipRect;
typedef HBRUSH (WINAPI *PFN_CreateHatchBrush)(int style, COLORREF color);
static PFN_CreateHatchBrush p_CreateHatchBrush;
typedef BOOL (WINAPI *PFN_LineTo)(HDC hdc, int x, int y);
static PFN_LineTo p_LineTo;
typedef BOOL (WINAPI *PFN_MoveToEx)(HDC hdc, int x, int y, LPPOINT lppt);
static PFN_MoveToEx p_MoveToEx;
typedef DWORD (WINAPI *PFN_GetGlyphIndicesW)(HDC hdc, LPCWSTR str, int c, LPWORD gi, DWORD flags);
static PFN_GetGlyphIndicesW p_GetGlyphIndicesW;
typedef BOOL (WINAPI *PFN_ExtTextOutW)(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCWSTR str, UINT c, const INT *dx);
static PFN_ExtTextOutW p_ExtTextOutW;
typedef COLORREF (WINAPI *PFN_GetBkColor)(HDC hdc);
static PFN_GetBkColor p_GetBkColor;
typedef COLORREF (WINAPI *PFN_GetPixel)(HDC hdc, int x, int y);
static PFN_GetPixel p_GetPixel;
typedef BOOL (WINAPI *PFN_StretchBlt)(HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, int w1, int h1, DWORD rop);
static PFN_StretchBlt p_StretchBlt;
typedef COLORREF (WINAPI *PFN_SetPixel)(HDC hdc, int x, int y, COLORREF color);
static PFN_SetPixel p_SetPixel;
typedef BOOL (WINAPI *PFN_SetPixelV)(HDC hdc, int x, int y, COLORREF color);
static PFN_SetPixelV p_SetPixelV;
typedef BOOL (WINAPI *PFN_ExtFloodFill)(HDC hdc, int x, int y, COLORREF color, UINT type);
static PFN_ExtFloodFill p_ExtFloodFill;
typedef BOOL (WINAPI *PFN_TextOutW)(HDC hdc, int x, int y, LPCWSTR str, int c);
static PFN_TextOutW p_TextOutW;
typedef HRGN (WINAPI *PFN_ExtCreateRegion)(const XFORM *lpx, DWORD nCount, const RGNDATA *lpData);
static PFN_ExtCreateRegion p_ExtCreateRegion;
typedef HGDIOBJ (WINAPI *PFN_GetStockObject)(int i);
static PFN_GetStockObject p_GetStockObject;
typedef int (WINAPI *PFN_GetObjectW)(HANDLE h, int c, LPVOID pv);
static PFN_GetObjectW p_GetObjectW;
typedef DWORD (WINAPI *PFN_GetFontData)(HDC hdc, DWORD table, DWORD offset, PVOID buffer, DWORD cbData);
static PFN_GetFontData p_GetFontData;
typedef HGDIOBJ (WINAPI *PFN_SelectObject)(HDC hdc, HGDIOBJ h);
static PFN_SelectObject p_SelectObject;
typedef HGDIOBJ (WINAPI *PFN_GetCurrentObject)(HDC hdc, DWORD type);
static PFN_GetCurrentObject p_GetCurrentObject;
typedef COLORREF (WINAPI *PFN_SetTextColor)(HDC hdc, COLORREF color);
static PFN_SetTextColor p_SetTextColor;
typedef COLORREF (WINAPI *PFN_SetBkColor)(HDC hdc, COLORREF color);
static PFN_SetBkColor p_SetBkColor;
typedef int (WINAPI *PFN_SetBkMode)(HDC hdc, int mode);
static PFN_SetBkMode p_SetBkMode;
typedef BOOL (WINAPI *PFN_BitBlt)(HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, DWORD rop);
static PFN_BitBlt p_BitBlt;
typedef BOOL (WINAPI *PFN_DeleteDC)(HDC hdc);
static PFN_DeleteDC p_DeleteDC;
typedef HBITMAP (WINAPI *PFN_CreateBitmap)(int w, int h, UINT planes, UINT bpp, const VOID *bits);
static PFN_CreateBitmap p_CreateBitmap;
typedef HBRUSH (WINAPI *PFN_CreatePatternBrush)(HBITMAP hbm);
static PFN_CreatePatternBrush p_CreatePatternBrush;
typedef BOOL (WINAPI *PFN_DeleteObject)(HGDIOBJ h);
static PFN_DeleteObject p_DeleteObject;
typedef HFONT (WINAPI *PFN_CreateFontIndirectW)(const LOGFONTW *lplf);
static PFN_CreateFontIndirectW p_CreateFontIndirectW;
typedef int (WINAPI *PFN_EnumFontFamiliesExW)(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD);
static PFN_EnumFontFamiliesExW p_EnumFontFamiliesExW;
typedef int (WINAPI *PFN_EnumFontFamiliesW)(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);
static PFN_EnumFontFamiliesW p_EnumFontFamiliesW;
typedef LRESULT (WINAPI *PFN_SendMessageW)(HWND, UINT, WPARAM, LPARAM);
static PFN_SendMessageW p_SendMessageW;


#define NTM_FLAGS(p)  (*(const DWORD *)((const BYTE *)(p) + 0x3c))
#define NTM_PITCH(p)  (*(const BYTE *)((const BYTE *)(p) + 0x37))
#define MAX_CACHE     8
#define MAX_ENT       20000

#define CB_ADDSTRING_          0x0143
#define CB_GETCOUNT_           0x0146
#define CB_GETITEMDATA_        0x0150
#define CB_SETITEMDATA_        0x0151

/* ---------- 回放期标记：只影响"发起回放的那个线程" ---------- */
static volatile LONG g_replayTid = 0;
static volatile LONG g_replayDepth = 0;
static int  g_hookTried = 0, g_hookOk = 0;
static void *g_smSlot = NULL;          /* afUILib 里 SendMessageW 的 IAT 槽 */

/* 一条唯一字体记录：完整保存 GDI 给的 LOGFONT/TEXTMETRIC 副本 */
typedef struct {
    DWORD ft;
    DWORD ntm;
    BYTE  pitch;
    WCHAR face[LF_FACESIZE];
    ENUMLOGFONTEXW  lf;
    NEWTEXTMETRICEXW tm;
} ENT;

typedef struct {
    LOGFONTW key;
    DWORD    flags;
    int      tech;
    int      useMem;
    int      n;
    ENT     *ents;
    int      seen;
    DWORD    enum_ms;
} CENT;

static int g_dbg_cs = 0, g_dbg_pitch = 0, g_dbg_w = 0;
static CENT g_cache[MAX_CACHE];
static int  g_cacheN = 0;

typedef struct {
    int  n;
    int  cap;
    ENT *ents;
    int  seen;
    int  oom;
} CCTX;

static int lf_same(const LOGFONTW *a, const LOGFONTW *b)
{
    if (a->lfHeight != b->lfHeight) return 0;
    if (a->lfWidth != b->lfWidth) return 0;
    if (a->lfWeight != b->lfWeight) return 0;
    if (a->lfItalic != b->lfItalic) return 0;
    if (a->lfUnderline != b->lfUnderline) return 0;
    if (a->lfStrikeOut != b->lfStrikeOut) return 0;
    if (a->lfCharSet != b->lfCharSet) return 0;
    if (a->lfOutPrecision != b->lfOutPrecision) return 0;
    if (a->lfClipPrecision != b->lfClipPrecision) return 0;
    if (a->lfQuality != b->lfQuality) return 0;
    if (a->lfPitchAndFamily != b->lfPitchAndFamily) return 0;
    return lstrcmpiW(a->lfFaceName, b->lfFaceName) == 0;
}

/* 去重键 = 字体名（不分大小写），与 afUILib 自己的判重口径完全一致。
   实测依据：按"名字+类型"去重会多留 4 条（783 vs 应用侧 779），
   说明它只比名字；这里对齐后，两种模式下最终列表条目数完全相同。
   顺序保持首次出现优先，与应用逻辑一致：先到的那条被加入，后来的跳过。 */
static int ent_find(CCTX *c, LPCWSTR face)
{
    int i;
    for (i = 0; i < c->n; i++) {
        ENT *u = &c->ents[i];
        if (u->face[0] == face[0] && lstrcmpiW(u->face, face) == 0)
            return i;
    }
    return -1;
}

static int CALLBACK CollectEx(const ENUMLOGFONTEXW *e, const NEWTEXTMETRICEXW *n, DWORD ft, LPARAM lp)
{
    CCTX *c = (CCTX *)lp;
    const WCHAR *face = e->elfLogFont.lfFaceName;
    DWORD ntm;
    BYTE pitch;
    ENT *u;

    c->seen++;
    if (!face[0]) return 1;
    if (!g_charset_ok[e->elfLogFont.lfCharSet & 0xFF]) return 1;

    ntm = NTM_FLAGS(n);
    pitch = NTM_PITCH(n);
    if (ent_find(c, face) >= 0) return 1;

    if (c->n == c->cap) {
        int nc = c->cap ? c->cap * 2 : 512;
        void *p;
        if (nc > MAX_ENT) { c->oom = 1; return 0; }
        p = c->ents
            ? HeapReAlloc(GetProcessHeap(), 0, c->ents, (SIZE_T)nc * sizeof(ENT))
            : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nc * sizeof(ENT));
        if (!p) { c->oom = 1; return 0; }
        c->ents = (ENT *)p;
        c->cap = nc;
    }

    u = &c->ents[c->n];
    u->ft = ft;
    u->ntm = ntm;
    u->pitch = pitch;
    lstrcpynW(u->face, face, LF_FACESIZE);
    u->face[LF_FACESIZE - 1] = 0;
    CopyMemory(&u->lf, e, sizeof(ENUMLOGFONTEXW));
    CopyMemory(&u->tm, n, sizeof(NEWTEXTMETRICEXW));
    c->n++;
    return 1;
}

/* 回放：把缓存里的条目逐个交还给应用的回调 */
static int replay_ent(const CENT *e, FONTENUMPROCW proc, LPARAM lp, int suppress)
{
    int i, ret = 1;
    if (suppress) {
        g_replayTid = (LONG)GetCurrentThreadId();
        InterlockedIncrement(&g_replayDepth);
    }
    for (i = 0; i < e->n; i++) {
        ret = proc((const LOGFONTW *)&e->ents[i].lf,
                   (const TEXTMETRICW *)&e->ents[i].tm,
                   e->ents[i].ft, lp);
        if (ret == 0) break;
    }
    if (suppress) InterlockedDecrement(&g_replayDepth);
    return ret;
}

static void write_log(const char *tag, int seen, int kept, int hit,
                      DWORD enum_ms, DWORD replay_ms, const char *dc)
{
    HANDLE h;
    char buf[384];
    int n;
    DWORD wr;
    if (!g_log || !g_logpath[0]) return;
    n = wsprintfA(buf,
        "[%lu] %s seen=%d kept=%d cache=%s enum_ms=%lu replay_ms=%lu dc=%s cs=%d pitch=0x%02X w=%d\r\n",
        GetTickCount(), tag, seen, kept, hit ? "hit" : "miss",
        enum_ms, replay_ms, dc, g_dbg_cs, g_dbg_pitch, g_dbg_w);
    h = CreateFileA(g_logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, buf, (DWORD)n, &wr, NULL);
        CloseHandle(h);
    }
}

/* ---------- SendMessageW 钩子 ---------- */

static LRESULT WINAPI hook_SendMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_fastCombo && g_replayDepth > 0 && (DWORD)g_replayTid == GetCurrentThreadId()) {
        /* 回放期间：告诉 afUILib "列表里还没有条目"，它的逐条比对循环就不跑了。
           Shim 已按 字体名+字体类型 去重，最终 CB_ADDSTRING 的结果与原逻辑一致。 */
        if (msg == CB_GETCOUNT_) return 0;
        if (msg == CB_GETITEMDATA_) return 0;
    }
    return p_SendMessageW(hwnd, msg, wp, lp);
}

/* 运行时把 afUILib 的 SendMessageW IAT 槽改指钩子（只动这一个槽） */
static int install_combo_hook(void)
{
    HMODULE mod;
    BYTE *b;
    DWORD pe_off, imp_rva, i;
    DWORD old;

    if (g_hookTried) return g_hookOk;
    g_hookTried = 1;

    mod = GetModuleHandleA("afUILib.dll");
    if (!mod) mod = GetModuleHandleA(NULL);   /* 没有 afUILib 时退回宿主模块，便于独立测试 */
    if (!mod) return 0;
    b = (BYTE *)mod;
    if (*(WORD *)b != 0x5A4D) return 0;
    pe_off = *(DWORD *)(b + 0x3C);
    if (*(DWORD *)(b + pe_off) != 0x00004550) return 0;
    imp_rva = *(DWORD *)(b + pe_off + 0x80);      /* DataDirectory[1].VirtualAddress */
    if (!imp_rva) return 0;

    for (i = 0; ; i++) {
        BYTE *d = b + imp_rva + i * 20;
        DWORD name_rva = *(DWORD *)(d + 12);
        DWORD oft = *(DWORD *)(d);
        DWORD ft = *(DWORD *)(d + 16);
        if (!name_rva && !oft && !ft) break;
        if (lstrcmpiA((const char *)(b + name_rva), "USER32.dll") == 0) {
            BYTE *names = b + (oft ? oft : ft);
            DWORD k;
            for (k = 0; ; k++) {
                DWORD t = *(DWORD *)(names + k * 4);
                if (!t) break;
                if (t & 0x80000000) continue;
                if (lstrcmpA((const char *)(b + t + 2), "SendMessageW") == 0) {
                    g_smSlot = b + ft + k * 4;
                    break;
                }
            }
        }
        if (g_smSlot) break;
    }
    if (!g_smSlot) return 0;

    if (!VirtualProtect(g_smSlot, 4, PAGE_READWRITE, &old)) return 0;
    *(void **)g_smSlot = (void *)hook_SendMessageW;
    VirtualProtect(g_smSlot, 4, old, &old);
    g_hookOk = 1;
    write_log("SendMessageW hook installed", 0, 0, 1, 0, 0, "-");
    return 1;
}

/* ---------------- 被劫持的枚举函数 ---------------- */

static int shim_enum_ex(HDC hdc, LPLOGFONTW plf, FONTENUMPROCW proc, LPARAM lp, DWORD flags)
{
    CENT *ce = NULL;
    HDC use;
    int i, tech, useMem, ret;
    DWORD t0, enum_ms = 0, replay_ms = 0;
    const char *dcs = "caller";

    if (!g_enabled || !proc)
        return p_EnumFontFamiliesExW(hdc, plf, proc, lp, flags);
    if (!plf || plf->lfFaceName[0] != 0 || flags != 0)
        return p_EnumFontFamiliesExW(hdc, plf, proc, lp, flags);

    g_dbg_cs = plf->lfCharSet;
    g_dbg_pitch = plf->lfPitchAndFamily;
    g_dbg_w = plf->lfWeight;

    if (!g_hookTried && g_fastCombo) install_combo_hook();

    tech = hdc ? (int)p_GetDeviceCaps(hdc, TECHNOLOGY) : 0;

    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_cacheN; i++) {
        if (g_cache[i].flags == flags && g_cache[i].tech == tech &&
            g_cache[i].useMem == g_useMem && lf_same(&g_cache[i].key, plf)) {
            ce = &g_cache[i];
            break;
        }
    }

    if (!ce) {
        CCTX c;
        ZeroMemory(&c, sizeof(c));
        useMem = (g_useMem && tech != DT_RASPRINTER) ? 1 : 0;
        use = hdc;
        if (useMem) {
            use = p_CreateCompatibleDC(NULL);
            dcs = "memory";
            if (!use) { useMem = 0; use = hdc; dcs = "caller"; }
        }
        t0 = GetTickCount();
        p_EnumFontFamiliesExW(use, plf, CollectEx, (LPARAM)&c, flags);
        enum_ms = GetTickCount() - t0;
        if (useMem && use) p_DeleteDC(use);

        if (!c.oom && g_cacheN < MAX_CACHE) {
            ce = &g_cache[g_cacheN++];
            CopyMemory(&ce->key, plf, sizeof(LOGFONTW));
            ce->flags = flags;
            ce->tech = tech;
            ce->useMem = g_useMem;
            ce->n = c.n;
            ce->ents = c.ents;
            ce->seen = c.seen;
            ce->enum_ms = enum_ms;
            ce->key.lfFaceName[LF_FACESIZE - 1] = 0;
        } else {
            CENT tmp;
            CopyMemory(&tmp.key, plf, sizeof(LOGFONTW));
            tmp.flags = flags; tmp.tech = tech; tmp.useMem = g_useMem;
            tmp.n = c.n; tmp.ents = c.ents; tmp.seen = c.seen; tmp.enum_ms = enum_ms;
            LeaveCriticalSection(&g_cs);
            t0 = GetTickCount();
            ret = replay_ent(&tmp, proc, lp, g_fastCombo && g_hookOk);
            write_log("EnumFontFamiliesExW", tmp.seen, tmp.n, 0,
                      enum_ms, GetTickCount() - t0, dcs);
            if (c.ents) HeapFree(GetProcessHeap(), 0, c.ents);
            return ret;
        }
        write_log("EnumFontFamiliesExW", ce->seen, ce->n, 0, enum_ms, 0, dcs);
    } else {
        dcs = ce->useMem ? "memory" : "caller";
    }
    LeaveCriticalSection(&g_cs);

    t0 = GetTickCount();
    ret = replay_ent(ce, proc, lp, g_fastCombo && g_hookOk);
    replay_ms = GetTickCount() - t0;
    if (replay_ms >= 20)
        write_log("  replay", ce->seen, ce->n, 1, ce->enum_ms, replay_ms, dcs);
    return ret;
}

static int shim_enum_w(HDC hdc, LPCWSTR family, FONTENUMPROCW proc, LPARAM lp)
{
    return p_EnumFontFamiliesW(hdc, family, proc, lp);
}

/* ---------------- 其余 47 个函数原样透传 ---------------- */

__declspec(dllexport) COLORREF WINAPI w_GetTextColor(HDC hdc)
{
    return p_GetTextColor(hdc);
}

__declspec(dllexport) BOOL WINAPI w_Polygon(HDC hdc, const POINT *apt, int cpt)
{
    return p_Polygon(hdc, apt, cpt);
}

__declspec(dllexport) HBRUSH WINAPI w_CreateSolidBrush(COLORREF color)
{
    return p_CreateSolidBrush(color);
}

__declspec(dllexport) DWORD WINAPI w_GetObjectType(HGDIOBJ h)
{
    return p_GetObjectType(h);
}

__declspec(dllexport) int WINAPI w_GetDeviceCaps(HDC hdc, int index)
{
    return p_GetDeviceCaps(hdc, index);
}

__declspec(dllexport) int WINAPI w_SaveDC(HDC hdc)
{
    return p_SaveDC(hdc);
}

__declspec(dllexport) BOOL WINAPI w_RestoreDC(HDC hdc, int nSavedDC)
{
    return p_RestoreDC(hdc, nSavedDC);
}

__declspec(dllexport) HRGN WINAPI w_CreateRectRgn(int x1, int y1, int x2, int y2)
{
    return p_CreateRectRgn(x1, y1, x2, y2);
}

__declspec(dllexport) HPEN WINAPI w_CreatePen(int style, DWORD width, COLORREF color)
{
    return p_CreatePen(style, width, color);
}

__declspec(dllexport) BOOL WINAPI w_Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    return p_Rectangle(hdc, left, top, right, bottom);
}

__declspec(dllexport) BOOL WINAPI w_PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop)
{
    return p_PatBlt(hdc, x, y, w, h, rop);
}

__declspec(dllexport) HRGN WINAPI w_CreateRectRgnIndirect(const RECT *lprc)
{
    return p_CreateRectRgnIndirect(lprc);
}

__declspec(dllexport) BOOL WINAPI w_GetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm)
{
    return p_GetTextMetricsW(hdc, lptm);
}

__declspec(dllexport) BOOL WINAPI w_GetTextExtentPoint32W(HDC hdc, LPCWSTR str, int c, LPSIZE size)
{
    return p_GetTextExtentPoint32W(hdc, str, c, size);
}

__declspec(dllexport) HBITMAP WINAPI w_CreateCompatibleBitmap(HDC hdc, int w, int h)
{
    return p_CreateCompatibleBitmap(hdc, w, h);
}

__declspec(dllexport) HDC WINAPI w_CreateCompatibleDC(HDC hdc)
{
    return p_CreateCompatibleDC(hdc);
}

__declspec(dllexport) int WINAPI w_GetTextCharset(HDC hdc)
{
    return p_GetTextCharset(hdc);
}

__declspec(dllexport) DWORD WINAPI w_SetLayout(HDC hdc, DWORD layout)
{
    return p_SetLayout(hdc, layout);
}

__declspec(dllexport) int WINAPI w_SelectClipRgn(HDC hdc, HRGN hrgn)
{
    return p_SelectClipRgn(hdc, hrgn);
}

__declspec(dllexport) int WINAPI w_ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    return p_ExcludeClipRect(hdc, left, top, right, bottom);
}

__declspec(dllexport) HBRUSH WINAPI w_CreateHatchBrush(int style, COLORREF color)
{
    return p_CreateHatchBrush(style, color);
}

__declspec(dllexport) BOOL WINAPI w_LineTo(HDC hdc, int x, int y)
{
    return p_LineTo(hdc, x, y);
}

__declspec(dllexport) BOOL WINAPI w_MoveToEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    return p_MoveToEx(hdc, x, y, lppt);
}

__declspec(dllexport) DWORD WINAPI w_GetGlyphIndicesW(HDC hdc, LPCWSTR str, int c, LPWORD gi, DWORD flags)
{
    return p_GetGlyphIndicesW(hdc, str, c, gi, flags);
}

__declspec(dllexport) BOOL WINAPI w_ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCWSTR str, UINT c, const INT *dx)
{
    return p_ExtTextOutW(hdc, x, y, options, lprect, str, c, dx);
}

__declspec(dllexport) COLORREF WINAPI w_GetBkColor(HDC hdc)
{
    return p_GetBkColor(hdc);
}

__declspec(dllexport) COLORREF WINAPI w_GetPixel(HDC hdc, int x, int y)
{
    return p_GetPixel(hdc, x, y);
}

__declspec(dllexport) BOOL WINAPI w_StretchBlt(HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, int w1, int h1, DWORD rop)
{
    return p_StretchBlt(hdc, x, y, w, h, hdcSrc, x1, y1, w1, h1, rop);
}

__declspec(dllexport) COLORREF WINAPI w_SetPixel(HDC hdc, int x, int y, COLORREF color)
{
    return p_SetPixel(hdc, x, y, color);
}

__declspec(dllexport) BOOL WINAPI w_SetPixelV(HDC hdc, int x, int y, COLORREF color)
{
    return p_SetPixelV(hdc, x, y, color);
}

__declspec(dllexport) BOOL WINAPI w_ExtFloodFill(HDC hdc, int x, int y, COLORREF color, UINT type)
{
    return p_ExtFloodFill(hdc, x, y, color, type);
}

__declspec(dllexport) BOOL WINAPI w_TextOutW(HDC hdc, int x, int y, LPCWSTR str, int c)
{
    return p_TextOutW(hdc, x, y, str, c);
}

__declspec(dllexport) HRGN WINAPI w_ExtCreateRegion(const XFORM *lpx, DWORD nCount, const RGNDATA *lpData)
{
    return p_ExtCreateRegion(lpx, nCount, lpData);
}

__declspec(dllexport) HGDIOBJ WINAPI w_GetStockObject(int i)
{
    return p_GetStockObject(i);
}

__declspec(dllexport) int WINAPI w_GetObjectW(HANDLE h, int c, LPVOID pv)
{
    return p_GetObjectW(h, c, pv);
}

__declspec(dllexport) DWORD WINAPI w_GetFontData(HDC hdc, DWORD table, DWORD offset, PVOID buffer, DWORD cbData)
{
    return p_GetFontData(hdc, table, offset, buffer, cbData);
}

__declspec(dllexport) HGDIOBJ WINAPI w_SelectObject(HDC hdc, HGDIOBJ h)
{
    return p_SelectObject(hdc, h);
}

__declspec(dllexport) HGDIOBJ WINAPI w_GetCurrentObject(HDC hdc, DWORD type)
{
    return p_GetCurrentObject(hdc, type);
}

__declspec(dllexport) COLORREF WINAPI w_SetTextColor(HDC hdc, COLORREF color)
{
    return p_SetTextColor(hdc, color);
}

__declspec(dllexport) COLORREF WINAPI w_SetBkColor(HDC hdc, COLORREF color)
{
    return p_SetBkColor(hdc, color);
}

__declspec(dllexport) int WINAPI w_SetBkMode(HDC hdc, int mode)
{
    return p_SetBkMode(hdc, mode);
}

__declspec(dllexport) BOOL WINAPI w_BitBlt(HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    return p_BitBlt(hdc, x, y, w, h, hdcSrc, x1, y1, rop);
}

__declspec(dllexport) BOOL WINAPI w_DeleteDC(HDC hdc)
{
    return p_DeleteDC(hdc);
}

__declspec(dllexport) HBITMAP WINAPI w_CreateBitmap(int w, int h, UINT planes, UINT bpp, const VOID *bits)
{
    return p_CreateBitmap(w, h, planes, bpp, bits);
}

__declspec(dllexport) HBRUSH WINAPI w_CreatePatternBrush(HBITMAP hbm)
{
    return p_CreatePatternBrush(hbm);
}

__declspec(dllexport) BOOL WINAPI w_DeleteObject(HGDIOBJ h)
{
    return p_DeleteObject(h);
}

__declspec(dllexport) HFONT WINAPI w_CreateFontIndirectW(const LOGFONTW *lplf)
{
    return p_CreateFontIndirectW(lplf);
}


/* ---------------- 初始化 ---------------- */

static void split_path(char *dst, DWORD n, const char *full, const char *leaf)
{
    char *p;
    lstrcpynA(dst, full, (int)n);
    p = dst;
    while (*p) p++;
    while (p > dst && p[-1] != '\\' && p[-1] != '/') p--;
    lstrcpynA(p, leaf, (int)(n - (DWORD)(p - dst)));
}

static void load_cfg(const char *dllpath)
{
    char buf[512];
    int i;
    split_path(g_inipath, MAX_PATH, dllpath, "afFontShim.ini");
    split_path(g_logpath, MAX_PATH, dllpath, "afFontShim.log");

    g_enabled   = GetPrivateProfileIntA("general", "enabled", 1, g_inipath);
    g_dedupe    = GetPrivateProfileIntA("general", "dedupe_family", 1, g_inipath);
    g_log       = GetPrivateProfileIntA("general", "log", 1, g_inipath);
    g_useMem    = GetPrivateProfileIntA("general", "use_memory_dc", 1, g_inipath);
    g_fastCombo = GetPrivateProfileIntA("general", "fast_combo", 1, g_inipath);

    for (i = 0; i < 256; i++) g_charset_ok[i] = 1;
    buf[0] = 0;
    GetPrivateProfileStringA("general", "charset_whitelist", "", buf, sizeof(buf), g_inipath);
    if (buf[0]) {
        char *p = buf;
        for (i = 0; i < 256; i++) g_charset_ok[i] = 0;
        while (*p) {
            int v = 0, got = 0;
            while (*p == ' ' || *p == ',') p++;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; got = 1; }
            if (got && v >= 0 && v < 256) g_charset_ok[v] = 1;
            while (*p && *p != ',') p++;
        }
    }
}

static void resolve_gdi(void)
{
    g_gdi = GetModuleHandleA("gdi32.dll");
    if (!g_gdi) g_gdi = LoadLibraryA("gdi32.dll");
    if (!g_gdi) { g_enabled = 0; return; }

    p_GetTextColor = (PFN_GetTextColor)GetProcAddress(g_gdi, "GetTextColor");
    p_Polygon = (PFN_Polygon)GetProcAddress(g_gdi, "Polygon");
    p_CreateSolidBrush = (PFN_CreateSolidBrush)GetProcAddress(g_gdi, "CreateSolidBrush");
    p_GetObjectType = (PFN_GetObjectType)GetProcAddress(g_gdi, "GetObjectType");
    p_GetDeviceCaps = (PFN_GetDeviceCaps)GetProcAddress(g_gdi, "GetDeviceCaps");
    p_SaveDC = (PFN_SaveDC)GetProcAddress(g_gdi, "SaveDC");
    p_RestoreDC = (PFN_RestoreDC)GetProcAddress(g_gdi, "RestoreDC");
    p_CreateRectRgn = (PFN_CreateRectRgn)GetProcAddress(g_gdi, "CreateRectRgn");
    p_CreatePen = (PFN_CreatePen)GetProcAddress(g_gdi, "CreatePen");
    p_Rectangle = (PFN_Rectangle)GetProcAddress(g_gdi, "Rectangle");
    p_PatBlt = (PFN_PatBlt)GetProcAddress(g_gdi, "PatBlt");
    p_CreateRectRgnIndirect = (PFN_CreateRectRgnIndirect)GetProcAddress(g_gdi, "CreateRectRgnIndirect");
    p_GetTextMetricsW = (PFN_GetTextMetricsW)GetProcAddress(g_gdi, "GetTextMetricsW");
    p_GetTextExtentPoint32W = (PFN_GetTextExtentPoint32W)GetProcAddress(g_gdi, "GetTextExtentPoint32W");
    p_CreateCompatibleBitmap = (PFN_CreateCompatibleBitmap)GetProcAddress(g_gdi, "CreateCompatibleBitmap");
    p_CreateCompatibleDC = (PFN_CreateCompatibleDC)GetProcAddress(g_gdi, "CreateCompatibleDC");
    p_GetTextCharset = (PFN_GetTextCharset)GetProcAddress(g_gdi, "GetTextCharset");
    p_SetLayout = (PFN_SetLayout)GetProcAddress(g_gdi, "SetLayout");
    p_SelectClipRgn = (PFN_SelectClipRgn)GetProcAddress(g_gdi, "SelectClipRgn");
    p_ExcludeClipRect = (PFN_ExcludeClipRect)GetProcAddress(g_gdi, "ExcludeClipRect");
    p_CreateHatchBrush = (PFN_CreateHatchBrush)GetProcAddress(g_gdi, "CreateHatchBrush");
    p_LineTo = (PFN_LineTo)GetProcAddress(g_gdi, "LineTo");
    p_MoveToEx = (PFN_MoveToEx)GetProcAddress(g_gdi, "MoveToEx");
    p_GetGlyphIndicesW = (PFN_GetGlyphIndicesW)GetProcAddress(g_gdi, "GetGlyphIndicesW");
    p_ExtTextOutW = (PFN_ExtTextOutW)GetProcAddress(g_gdi, "ExtTextOutW");
    p_GetBkColor = (PFN_GetBkColor)GetProcAddress(g_gdi, "GetBkColor");
    p_GetPixel = (PFN_GetPixel)GetProcAddress(g_gdi, "GetPixel");
    p_StretchBlt = (PFN_StretchBlt)GetProcAddress(g_gdi, "StretchBlt");
    p_SetPixel = (PFN_SetPixel)GetProcAddress(g_gdi, "SetPixel");
    p_SetPixelV = (PFN_SetPixelV)GetProcAddress(g_gdi, "SetPixelV");
    p_ExtFloodFill = (PFN_ExtFloodFill)GetProcAddress(g_gdi, "ExtFloodFill");
    p_TextOutW = (PFN_TextOutW)GetProcAddress(g_gdi, "TextOutW");
    p_ExtCreateRegion = (PFN_ExtCreateRegion)GetProcAddress(g_gdi, "ExtCreateRegion");
    p_GetStockObject = (PFN_GetStockObject)GetProcAddress(g_gdi, "GetStockObject");
    p_GetObjectW = (PFN_GetObjectW)GetProcAddress(g_gdi, "GetObjectW");
    p_GetFontData = (PFN_GetFontData)GetProcAddress(g_gdi, "GetFontData");
    p_SelectObject = (PFN_SelectObject)GetProcAddress(g_gdi, "SelectObject");
    p_GetCurrentObject = (PFN_GetCurrentObject)GetProcAddress(g_gdi, "GetCurrentObject");
    p_SetTextColor = (PFN_SetTextColor)GetProcAddress(g_gdi, "SetTextColor");
    p_SetBkColor = (PFN_SetBkColor)GetProcAddress(g_gdi, "SetBkColor");
    p_SetBkMode = (PFN_SetBkMode)GetProcAddress(g_gdi, "SetBkMode");
    p_BitBlt = (PFN_BitBlt)GetProcAddress(g_gdi, "BitBlt");
    p_DeleteDC = (PFN_DeleteDC)GetProcAddress(g_gdi, "DeleteDC");
    p_CreateBitmap = (PFN_CreateBitmap)GetProcAddress(g_gdi, "CreateBitmap");
    p_CreatePatternBrush = (PFN_CreatePatternBrush)GetProcAddress(g_gdi, "CreatePatternBrush");
    p_DeleteObject = (PFN_DeleteObject)GetProcAddress(g_gdi, "DeleteObject");
    p_CreateFontIndirectW = (PFN_CreateFontIndirectW)GetProcAddress(g_gdi, "CreateFontIndirectW");
    p_EnumFontFamiliesExW = (PFN_EnumFontFamiliesExW)GetProcAddress(g_gdi, "EnumFontFamiliesExW");
    p_EnumFontFamiliesW = (PFN_EnumFontFamiliesW)GetProcAddress(g_gdi, "EnumFontFamiliesW");
    if (!p_EnumFontFamiliesExW || !p_CreateCompatibleDC || !p_DeleteDC || !p_GetDeviceCaps)
        g_enabled = 0;

    g_u32 = GetModuleHandleA("user32.dll");
    if (!g_u32) g_u32 = LoadLibraryA("user32.dll");
    if (g_u32) p_SendMessageW = (PFN_SendMessageW)GetProcAddress(g_u32, "SendMessageW");
    if (!p_SendMessageW) g_enabled = 0;
}


__declspec(dllexport) int WINAPI w_EnumFontFamiliesExW(HDC hdc, LPLOGFONTW plf, FONTENUMPROCW proc, LPARAM lp, DWORD flags)
{
    if (!p_EnumFontFamiliesExW) return 0;
    return shim_enum_ex(hdc, plf, proc, lp, flags);
}

__declspec(dllexport) int WINAPI w_EnumFontFamiliesW(HDC hdc, LPCWSTR family, FONTENUMPROCW proc, LPARAM lp)
{
    if (!p_EnumFontFamiliesW) return 0;
    return shim_enum_w(hdc, family, proc, lp);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    char path[MAX_PATH];
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        InitializeCriticalSection(&g_cs);
        path[0] = 0;
        GetModuleFileNameA((HMODULE)hinst, path, MAX_PATH);
        load_cfg(path);
        resolve_gdi();
        if (g_log) {
            HANDLE h = CreateFileA(g_logpath, GENERIC_WRITE, FILE_SHARE_READ,
                                   NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    }
    return TRUE;
}

