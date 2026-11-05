# Generate afFontShim.c for ArcMap 10.2.2 afUILib.dll IAT patch.
# Hooked: EnumFontFamiliesExW / EnumFontFamiliesW (cache + warm-up)
# Others: passthrough wrappers so we can rename the gdi32 import descriptor.

FUNCS = [
    ("COLORREF", "GetTextColor", "HDC hdc"),
    ("BOOL", "Polygon", "HDC hdc, const POINT *apt, int cpt"),
    ("HBRUSH", "CreateSolidBrush", "COLORREF color"),
    ("DWORD", "GetObjectType", "HGDIOBJ h"),
    ("int", "GetDeviceCaps", "HDC hdc, int index"),
    ("int", "SaveDC", "HDC hdc"),
    ("BOOL", "RestoreDC", "HDC hdc, int nSavedDC"),
    ("HRGN", "CreateRectRgn", "int x1, int y1, int x2, int y2"),
    ("HPEN", "CreatePen", "int style, DWORD width, COLORREF color"),
    ("BOOL", "Rectangle", "HDC hdc, int left, int top, int right, int bottom"),
    ("BOOL", "PatBlt", "HDC hdc, int x, int y, int w, int h, DWORD rop"),
    ("HRGN", "CreateRectRgnIndirect", "const RECT *lprc"),
    ("BOOL", "GetTextMetricsW", "HDC hdc, LPTEXTMETRICW lptm"),
    ("BOOL", "GetTextExtentPoint32W", "HDC hdc, LPCWSTR str, int c, LPSIZE size"),
    ("HBITMAP", "CreateCompatibleBitmap", "HDC hdc, int w, int h"),
    ("HDC", "CreateCompatibleDC", "HDC hdc"),
    ("int", "GetTextCharset", "HDC hdc"),
    ("DWORD", "SetLayout", "HDC hdc, DWORD layout"),
    ("int", "SelectClipRgn", "HDC hdc, HRGN hrgn"),
    ("int", "ExcludeClipRect", "HDC hdc, int left, int top, int right, int bottom"),
    ("HBRUSH", "CreateHatchBrush", "int style, COLORREF color"),
    ("BOOL", "LineTo", "HDC hdc, int x, int y"),
    ("BOOL", "MoveToEx", "HDC hdc, int x, int y, LPPOINT lppt"),
    ("DWORD", "GetGlyphIndicesW", "HDC hdc, LPCWSTR str, int c, LPWORD gi, DWORD flags"),
    ("BOOL", "ExtTextOutW", "HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCWSTR str, UINT c, const INT *dx"),
    ("COLORREF", "GetBkColor", "HDC hdc"),
    ("COLORREF", "GetPixel", "HDC hdc, int x, int y"),
    ("BOOL", "StretchBlt", "HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, int w1, int h1, DWORD rop"),
    ("COLORREF", "SetPixel", "HDC hdc, int x, int y, COLORREF color"),
    ("BOOL", "SetPixelV", "HDC hdc, int x, int y, COLORREF color"),
    ("BOOL", "ExtFloodFill", "HDC hdc, int x, int y, COLORREF color, UINT type"),
    ("BOOL", "TextOutW", "HDC hdc, int x, int y, LPCWSTR str, int c"),
    ("HRGN", "ExtCreateRegion", "const XFORM *lpx, DWORD nCount, const RGNDATA *lpData"),
    ("HGDIOBJ", "GetStockObject", "int i"),
    ("int", "GetObjectW", "HANDLE h, int c, LPVOID pv"),
    ("DWORD", "GetFontData", "HDC hdc, DWORD table, DWORD offset, PVOID buffer, DWORD cbData"),
    ("HGDIOBJ", "SelectObject", "HDC hdc, HGDIOBJ h"),
    ("HGDIOBJ", "GetCurrentObject", "HDC hdc, DWORD type"),
    ("COLORREF", "SetTextColor", "HDC hdc, COLORREF color"),
    ("COLORREF", "SetBkColor", "HDC hdc, COLORREF color"),
    ("int", "SetBkMode", "HDC hdc, int mode"),
    ("BOOL", "BitBlt", "HDC hdc, int x, int y, int w, int h, HDC hdcSrc, int x1, int y1, DWORD rop"),
    ("BOOL", "DeleteDC", "HDC hdc"),
    ("HBITMAP", "CreateBitmap", "int w, int h, UINT planes, UINT bpp, const VOID *bits"),
    ("HBRUSH", "CreatePatternBrush", "HBITMAP hbm"),
    ("BOOL", "DeleteObject", "HGDIOBJ h"),
    ("HFONT", "CreateFontIndirectW", "const LOGFONTW *lplf"),
]

HOOKED = ["EnumFontFamiliesExW", "EnumFontFamiliesW"]


def argnames(params):
    names = []
    for p in params.split(","):
        p = p.strip()
        if not p:
            continue
        # last whitespace-separated token, minus leading stars
        tok = p.split()[-1].lstrip("*")
        names.append(tok)
    return names


def parse_type(p):
    """extract type text of one parameter (drop the trailing name)"""
    p = p.strip()
    parts = p.split()
    return " ".join(parts[:-1])


def clean_type(t):
    # anonymous struct types other than known ones are fine; keep as-is
    return t


lines = []
A = lines.append

A("// afFontShim.dll - GDI font-enumeration cache shim for afUILib.dll (ArcMap 10.2.2)")
A("// Hooked: EnumFontFamiliesExW, EnumFontFamiliesW.  Others: passthrough to real gdi32.")
A("// Exports use the real GDI names via linker /EXPORT aliases (see build_shim.sh).")
A("")
A("#define WIN32_LEAN_AND_MEAN")
A("#include <windows.h>")
A("")
A("static HMODULE g_gdi = NULL;")
A("")

# real function pointer typedefs
for (r, n, prm) in FUNCS:
    A("typedef %s (WINAPI *PFN_%s)(%s);" % (r, n, prm))
    A("static PFN_%s p_%s;" % (n, n))
A("typedef int (WINAPI *PFN_EnumFontFamiliesExW)(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD);")
A("static PFN_EnumFontFamiliesExW p_EnumFontFamiliesExW;")
A("typedef int (WINAPI *PFN_EnumFontFamiliesW)(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);")
A("static PFN_EnumFontFamiliesW p_EnumFontFamiliesW;")
A("")
A(r"""
// ---------------- cache machinery ----------------

typedef struct { ENUMLOGFONTEXW elf; NEWTEXTMETRICEXW ntme; DWORD ft; } EXENTRY;
typedef struct { LOGFONTW lf; TEXTMETRICW tm; DWORD ft; } PLENTRY;

static CRITICAL_SECTION g_cs;
static EXENTRY *g_ex; static int g_exN, g_exCap;
static PLENTRY *g_pl; static int g_plN, g_plCap;
static volatile LONG g_exState = 0;   // 0 empty, 1 building, 2 ready
static volatile LONG g_plState = 0;

static void cache_add_ex(const ENUMLOGFONTEXW *elf, const NEWTEXTMETRICEXW *ntme, DWORD ft)
{
    if (g_exN == g_exCap) {
        int nc = g_exCap ? g_exCap * 2 : 512;
        EXENTRY *ne = (EXENTRY *)HeapReAlloc(GetProcessHeap(), 0, g_ex, (SIZE_T)nc * sizeof(EXENTRY));
        if (!ne) return;
        g_ex = ne; g_exCap = nc;
    }
    g_ex[g_exN].elf = *elf;
    g_ex[g_exN].ntme = *ntme;
    g_ex[g_exN].ft = ft;
    g_exN++;
}

static void cache_add_pl(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD ft)
{
    if (g_plN == g_plCap) {
        int nc = g_plCap ? g_plCap * 2 : 512;
        PLENTRY *ne = (PLENTRY *)HeapReAlloc(GetProcessHeap(), 0, g_pl, (SIZE_T)nc * sizeof(PLENTRY));
        if (!ne) return;
        g_pl = ne; g_plCap = nc;
    }
    g_pl[g_plN].lf = *lf;
    g_pl[g_plN].tm = *tm;
    g_pl[g_plN].ft = ft;
    g_plN++;
}

typedef struct { FONTENUMPROCW user; LPARAM userlp; int capture; } CAPCTX;

static int CALLBACK capProcEx(const ENUMLOGFONTEXW *elf, const NEWTEXTMETRICEXW *ntme, DWORD ft, LPARAM lp)
{
    CAPCTX *ctx = (CAPCTX *)lp;
    if (ctx->capture) {
        EnterCriticalSection(&g_cs);
        cache_add_ex(elf, ntme, ft);
        LeaveCriticalSection(&g_cs);
    }
    return ctx->user((const LOGFONTW *)elf, (const TEXTMETRICW *)ntme, ft, ctx->userlp);
}

static int CALLBACK capProcPl(const ENUMLOGFONTW *lf, const NEWTEXTMETRICW *tm, DWORD ft, LPARAM lp)
{
    CAPCTX *ctx = (CAPCTX *)lp;
    if (ctx->capture) {
        EnterCriticalSection(&g_cs);
        cache_add_pl(lf, tm, ft);
        LeaveCriticalSection(&g_cs);
    }
    return ctx->user((const LOGFONTW *)lf, (const TEXTMETRICW *)tm, ft, ctx->userlp);
}

static int CALLBACK sinkProcEx(const ENUMLOGFONTEXW *e, const NEWTEXTMETRICEXW *n, DWORD ft, LPARAM lp)
{
    (void)e; (void)n; (void)ft; (void)lp; return 1;
}

static int CALLBACK sinkProcPl(const LOGFONTW *l, const TEXTMETRICW *t, DWORD ft, LPARAM lp)
{
    (void)l; (void)t; (void)ft; (void)lp; return 1;
}

// ---------------- hooked implementations ----------------

static int shim_enum_ex(HDC hdc, LPLOGFONTW plf, FONTENUMPROCW proc, LPARAM lp, DWORD flags)
{
    if (plf && flags == 0 && plf->lfPitchAndFamily == 0) {
        if (InterlockedCompareExchange(&g_exState, 2, 2) == 2) {
            int ret = 1, i;
            EnterCriticalSection(&g_cs);
            for (i = 0; i < g_exN; i++) {
                EXENTRY *en = &g_ex[i];
                if (plf->lfCharSet != DEFAULT_CHARSET &&
                    plf->lfCharSet != (BYTE)en->elf.elfLogFont.lfCharSet)
                    continue;
                if (plf->lfFaceName[0] != 0 && lstrcmpiW(plf->lfFaceName, en->elf.elfLogFont.lfFaceName) != 0)
                    continue;
                ret = proc((const LOGFONTW *)&en->elf, (const TEXTMETRICW *)&en->ntme, en->ft, lp);
                if (ret == 0) break;
            }
            LeaveCriticalSection(&g_cs);
            return ret;
        }
        if (plf->lfFaceName[0] == 0 && plf->lfCharSet == DEFAULT_CHARSET &&
            InterlockedCompareExchange(&g_exState, 0, 0) == 0 &&
            InterlockedCompareExchange(&g_exState, 1, 0) == 0) {
            CAPCTX ctx; int ret;
            ctx.user = proc; ctx.userlp = lp; ctx.capture = 1;
            ret = p_EnumFontFamiliesExW(hdc, plf, capProcEx, (LPARAM)&ctx, flags);
            EnterCriticalSection(&g_cs);
            InterlockedExchange(&g_exState, g_exN ? 2 : 0);
            LeaveCriticalSection(&g_cs);
            return ret;
        }
    }
    return p_EnumFontFamiliesExW(hdc, plf, proc, lp, flags);
}

static int shim_enum_pl(HDC hdc, LPCWSTR family, FONTENUMPROCW proc, LPARAM lp)
{
    if (InterlockedCompareExchange(&g_plState, 2, 2) == 2) {
        int ret = 1, i;
        EnterCriticalSection(&g_cs);
        for (i = 0; i < g_plN; i++) {
            PLENTRY *en = &g_pl[i];
            if (family && family[0] != 0 && lstrcmpiW(family, en->lf.lfFaceName) != 0)
                continue;
            ret = proc((const LOGFONTW *)&en->lf, (const TEXTMETRICW *)&en->tm, en->ft, lp);
            if (ret == 0) break;
        }
        LeaveCriticalSection(&g_cs);
        return ret;
    }
    if (!family || family[0] == 0) {
        if (InterlockedCompareExchange(&g_plState, 0, 0) == 0 &&
            InterlockedCompareExchange(&g_plState, 1, 0) == 0) {
            CAPCTX ctx; int ret;
            ctx.user = proc; ctx.userlp = lp; ctx.capture = 1;
            ret = p_EnumFontFamiliesW(hdc, family, capProcPl, (LPARAM)&ctx);
            EnterCriticalSection(&g_cs);
            InterlockedExchange(&g_plState, g_plN ? 2 : 0);
            LeaveCriticalSection(&g_cs);
            return ret;
        }
    }
    return p_EnumFontFamiliesW(hdc, family, proc, lp);
}

// ---------------- passthrough wrappers ----------------
""")

for (r, n, prm) in FUNCS:
    args = ", ".join(argnames(prm))
    A("__declspec(dllexport) %s WINAPI w_%s(%s)" % (r, n, prm))
    A("{")
    A("    return p_%s(%s);" % (n, args))
    A("}")
    A("")

A(r"""
// ---------------- warm-up ----------------

static DWORD WINAPI WarmupThread(LPVOID param)
{
    HDC hdc;
    LOGFONTW lf;
    CAPCTX ctx;
    (void)param;
    Sleep(3000);

    hdc = CreateCompatibleDC(NULL);
    if (!hdc) return 0;

    if (InterlockedCompareExchange(&g_exState, 0, 0) == 0 &&
        InterlockedCompareExchange(&g_exState, 1, 0) == 0) {
        ZeroMemory(&lf, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        ctx.user = sinkProcEx; ctx.userlp = 0; ctx.capture = 1;
        p_EnumFontFamiliesExW(hdc, &lf, capProcEx, (LPARAM)&ctx, 0);
        EnterCriticalSection(&g_cs);
        InterlockedExchange(&g_exState, g_exN ? 2 : 0);
        LeaveCriticalSection(&g_cs);
    }

    if (InterlockedCompareExchange(&g_plState, 0, 0) == 0 &&
        InterlockedCompareExchange(&g_plState, 1, 0) == 0) {
        ctx.user = sinkProcPl; ctx.userlp = 0; ctx.capture = 1;
        p_EnumFontFamiliesW(hdc, NULL, capProcPl, (LPARAM)&ctx);
        EnterCriticalSection(&g_cs);
        InterlockedExchange(&g_plState, g_plN ? 2 : 0);
        LeaveCriticalSection(&g_cs);
    }

    DeleteDC(hdc);
    return 0;
}

static void ResolveGdi(void)
{
    g_gdi = LoadLibraryW(L"gdi32.dll");
    if (!g_gdi) return;
""")

for (r, n, prm) in FUNCS:
    A('    p_%s = (PFN_%s)GetProcAddress(g_gdi, "%s");' % (n, n, n))
A('    p_EnumFontFamiliesExW = (PFN_EnumFontFamiliesExW)GetProcAddress(g_gdi, "EnumFontFamiliesExW");')
A('    p_EnumFontFamiliesW = (PFN_EnumFontFamiliesW)GetProcAddress(g_gdi, "EnumFontFamiliesW");')
A("}")
A("")
A(r"""
// exported entry points (real GDI names assigned by linker alias)
__declspec(dllexport) int WINAPI w_EnumFontFamiliesExW(HDC hdc, LPLOGFONTW plf, FONTENUMPROCW proc, LPARAM lp, DWORD flags)
{
    if (!p_EnumFontFamiliesExW) return 0;
    return shim_enum_ex(hdc, plf, proc, lp, flags);
}

__declspec(dllexport) int WINAPI w_EnumFontFamiliesW(HDC hdc, LPCWSTR family, FONTENUMPROCW proc, LPARAM lp)
{
    if (!p_EnumFontFamiliesW) return 0;
    return shim_enum_pl(hdc, family, proc, lp);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    static HANDLE th = NULL;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        InitializeCriticalSection(&g_cs);
        ResolveGdi();
        th = CreateThread(NULL, 0, WarmupThread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
""")

src = "\n".join(lines) + "\n"
open("afFontShim.c", "w", encoding="utf-8").write(src)

# linker export directives: export real GDI names, mapped to our internal symbols
exp = []
for (r, n, prm) in FUNCS:
    exp.append("/EXPORT:%s=w_%s" % (n, n))
exp.append("/EXPORT:EnumFontFamiliesExW=x_EnumFontFamiliesExW")
exp.append("/EXPORT:EnumFontFamiliesW=x_EnumFontFamiliesW")
open("exports.txt", "w").write("\n".join(exp))
print("wrote afFontShim.c (%d lines) and exports.txt (%d exports)" % (len(lines), len(exp)))
