# -*- coding: utf-8 -*-
"""
Generate afFontShim.c (v4)

v3 的实测结论（见 afFontShim.log）：
  enum_ms 已经是 0；全部耗时 100% 落在 replay_ms（4.7s / 6.5s）——
  即 afUILib 自己的字体回调里那段 O(n^2) 循环：
      n = SendMessageW(combo, CB_GETCOUNT)
      for i in 0..n-1: SendMessageW(combo, CB_GETITEMDATA, i) -> 取串比对 -> 重复则跳过
      SendMessageW(combo, CB_ADDSTRING, 0, name)
  该循环唯一作用是"跳重复"，而 Shim 侧已经去过重，它每次都是白跑。

v4 在这一层下手（不重做 afUILib 补丁，只替换 Shim）：
  1) 运行时把 afUILib 的 SendMessageW IAT 槽改指本 DLL 的钩子（只动 1 个槽位，
     不解剖 user32 的 167 个导入，避免 wsprintfW 等 cdecl 函数被误换）；
  2) 钩子在"本线程正在回放字体条目"期间，让 CB_GETCOUNT 返回 0 —— 循环自然不进入，
     CB_ADDSTRING / CB_SETITEMDATA 照常执行，最终列表与原来完全一致；
  3) 其余消息、其他线程一律原样透传给真正的 user32!SendMessageW。
"""
import ast

src = open("gen_shim.py", encoding="utf-8").read()
FUNCS = None
for node in ast.parse(src).body:
    if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "FUNCS":
        FUNCS = ast.literal_eval(node.value)
assert FUNCS and len(FUNCS) == 47, len(FUNCS or [])

NAMES = [n for (_, n, _) in FUNCS]
for need in ("CreateCompatibleDC", "DeleteDC", "GetDeviceCaps"):
    assert need in NAMES, need


def argnames(params):
    out = []
    for p in params.split(","):
        p = p.strip()
        if p:
            out.append(p.split()[-1].lstrip("*"))
    return out


L = []
A = L.append
A('#define WIN32_LEAN_AND_MEAN')
A('#include <windows.h>')
A('')
A('// afFontShim.dll v4 - 字体列表：单遍枚举 + 结果缓存 + 回放期抑制 combo 去重循环')
A('// tcc 编译，导出 afUILib 需要的全部 gdi32 函数名')
A('')
A('static HMODULE g_gdi = NULL;')
A('static HMODULE g_u32 = NULL;')
A('static CRITICAL_SECTION g_cs;')
A('static int g_enabled = 1, g_dedupe = 1, g_log = 1, g_useMem = 1, g_fastCombo = 1;')
A('static BYTE g_charset_ok[256];')
A('static char g_inipath[MAX_PATH];')
A('static char g_logpath[MAX_PATH];')
A('')

for (r, n, prm) in FUNCS:
    A('typedef %s (WINAPI *PFN_%s)(%s);' % (r, n, prm))
    A('static PFN_%s p_%s;' % (n, n))
A('typedef int (WINAPI *PFN_EnumFontFamiliesExW)(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD);')
A('static PFN_EnumFontFamiliesExW p_EnumFontFamiliesExW;')
A('typedef int (WINAPI *PFN_EnumFontFamiliesW)(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);')
A('static PFN_EnumFontFamiliesW p_EnumFontFamiliesW;')
A('typedef LRESULT (WINAPI *PFN_SendMessageW)(HWND, UINT, WPARAM, LPARAM);')
A('static PFN_SendMessageW p_SendMessageW;')
A('')

A(r'''
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
''')

A('/* ---------------- 其余 47 个函数原样透传 ---------------- */')
A('')
for (r, n, prm) in FUNCS:
    args = ", ".join(argnames(prm))
    A('__declspec(dllexport) %s WINAPI w_%s(%s)' % (r, n, prm))
    A('{')
    A('    return p_%s(%s);' % (n, args))
    A('}')
    A('')

A(r'''
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
''')

for (r, n, prm) in FUNCS:
    A('    p_%s = (PFN_%s)GetProcAddress(g_gdi, "%s");' % (n, n, n))
A('    p_EnumFontFamiliesExW = (PFN_EnumFontFamiliesExW)GetProcAddress(g_gdi, "EnumFontFamiliesExW");')
A('    p_EnumFontFamiliesW = (PFN_EnumFontFamiliesW)GetProcAddress(g_gdi, "EnumFontFamiliesW");')
A('    if (!p_EnumFontFamiliesExW || !p_CreateCompatibleDC || !p_DeleteDC || !p_GetDeviceCaps)')
A('        g_enabled = 0;')
A('')
A('    g_u32 = GetModuleHandleA("user32.dll");')
A('    if (!g_u32) g_u32 = LoadLibraryA("user32.dll");')
A('    if (g_u32) p_SendMessageW = (PFN_SendMessageW)GetProcAddress(g_u32, "SendMessageW");')
A('    if (!p_SendMessageW) g_enabled = 0;')
A('}')
A('')
A(r'''
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
''')

open("afFontShim.c", "w", encoding="utf-8").write("\n".join(L) + "\n")
print("wrote afFontShim.c (%d lines)" % len(L))
