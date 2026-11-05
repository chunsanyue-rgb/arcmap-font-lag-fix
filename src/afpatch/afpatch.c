/* afpatch.exe -- ArcMap 字体列表卡顿补丁的便携补丁/备份/回退工具
 *
 * 32 位 Windows 控制台程序，单文件、无外部依赖（tcc 编译，只用 kernel32/advapi32）。
 *
 * 命令：
 *   discover                     列出本机所有 ArcGIS Desktop10.x 的 bin 目录
 *   check <bin>                  判断该目录下 afUILib.dll 是否已打补丁
 *                                ORIGINAL=0 / PATCHED=3 / 异常=1
 *   verify <bin> <shim.dll>      校验 shim 是否导出了 afUILib 需要的全部 gdi32 函数
 *   patch <bin>                  就地打补丁（原子替换，失败不动原文件）
 *   patchfile <in> <out>         把 <in> 打补丁后写到 <out>（供比对/离线制作）
 *   backup <bin> <backupRoot>    备份原始 afUILib.dll 到 <backupRoot>\<主机>_<时间>_<版本>\
 *   restore <backupDir>          用该备份目录还原
 *   list <backupRoot>            列出可用备份（新 -> 旧）
 *   stamp                        输出时间戳 yyyyMMdd_HHmmss
 *   version                      显示版本
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define TOOLVER "1.0"

/* 输出：写 AFREPORT 环境变量指定的报告文件（控制台输出在部分环境下不可靠，
   批处理一律读报告文件）。若未设置变量则只尽力写控制台。 */
static HANDLE g_rep = NULL;

static void open_report(void)
{
    char dir[MAX_PATH], path[MAX_PATH * 2], tmp[MAX_PATH];
    DWORD n;

    /* 1) 先试程序自身所在目录（便携包一般可写） */
    n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (n && n < MAX_PATH) {
        int i = (int)n;
        while (i > 0 && dir[i - 1] != '\\' && dir[i - 1] != '/') i--;
        dir[i] = 0;
        wsprintfA(path, "%safpatch.out", dir);
        g_rep = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_rep != INVALID_HANDLE_VALUE) { SetFilePointer(g_rep, 0, NULL, FILE_END); return; }
        g_rep = NULL;
    }
    /* 2) 退到临时目录 */
    if (GetTempPathA(MAX_PATH, tmp)) {
        wsprintfA(path, "%safpatch.out", tmp);
        g_rep = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_rep != INVALID_HANDLE_VALUE) SetFilePointer(g_rep, 0, NULL, FILE_END);
        else g_rep = NULL;
    }
}

static void sayf(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    DWORD w;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    if (g_rep) {
        WriteFile(g_rep, b, lstrlenA(b), &w, NULL);
        FlushFileBuffers(g_rep);
    } else {
        HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
        if (ho && ho != INVALID_HANDLE_VALUE) WriteFile(ho, b, lstrlenA(b), &w, NULL);
    }
}

#define SHIM_NAME "afFontShim.dll"
#define SHIM_NAME_LEN 15            /* strlen("afFontShim.dll") + 1 */
#define FILE_ALIGN 0x200u
#define SECT_ALIGN 0x1000u
#define SEC_CHAR 0x40000040u        /* INITIALIZED_DATA | READ */

#define IMG_MACHINE_I386 0x14cu
#define PE32_MAGIC 0x10bu

static int g_verbose = 0;

/* ---------------------------------------------------------------- 基础 */
static DWORD r32(const BYTE *p) { DWORD v; memcpy(&v, p, 4); return v; }
static WORD  r16(const BYTE *p) { WORD v;  memcpy(&v, p, 2); return v; }
static void  w32(BYTE *p, DWORD v) { memcpy(p, &v, 4); }
static void  w16(BYTE *p, WORD v)  { memcpy(p, &v, 2); }
static DWORD alignup(DWORD v, DWORD a) { return (v + a - 1u) & ~(a - 1u); }

static int ci_eq(const char *a, const char *b) { return lstrcmpiA(a, b) == 0; }

static int ci_starts(const char *s, const char *pfx)
{
    while (*pfx) {
        char x = *s, y = *pfx;
        if (!x) return 0;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        s++; pfx++;
    }
    return 1;
}

typedef struct {
    BYTE *p;
    DWORD n;
    DWORD pe, oph, nsec, ophsz, sectab;
    DWORD offSizeImg, offSizeInit, offCksum, offNsec, hdrSize;
    DWORD impRva, expRva;
} PE;

static int read_file(const char *path, BYTE **out, DWORD *outlen)
{
    HANDLE h;
    DWORD sz, got = 0;
    BYTE *buf;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return 0; }
    buf = (BYTE *)malloc(sz);
    if (!buf) { CloseHandle(h); return 0; }
    if (!ReadFile(h, buf, sz, &got, NULL) || got != sz) {
        free(buf); CloseHandle(h); return 0;
    }
    CloseHandle(h);
    *out = buf; *outlen = sz;
    return 1;
}

static int write_file_atomic(const char *path, const BYTE *data, DWORD len)
{
    char tmp[MAX_PATH + 8];
    HANDLE h;
    DWORD wr = 0;
    DWORD attr;

    lstrcpynA(tmp, path, MAX_PATH - 8);
    lstrcatA(tmp, ".tmp");

    DeleteFileA(tmp);
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { sayf("ERR create %s (%lu)\r\n", tmp, GetLastError()); return 0; }
    if (!WriteFile(h, data, len, &wr, NULL) || wr != len) {
        sayf("ERR write %s (%lu)\r\n", tmp, GetLastError());
        CloseHandle(h); DeleteFileA(tmp); return 0;
    }
    FlushFileBuffers(h);
    CloseHandle(h);

    attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesA(path, attr & ~FILE_ATTRIBUTE_READONLY);

    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        sayf("ERR replace %s (%lu) -- file locked, is ArcMap running?\r\n", path, GetLastError());
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

static DWORD rva2off(PE *pe, DWORD rva)
{
    DWORD i;
    for (i = 0; i < pe->nsec; i++) {
        BYTE *s = pe->p + pe->sectab + i * 40;
        DWORD va = r32(s + 12), vs = r32(s + 8), rs = r32(s + 16), pr = r32(s + 20);
        DWORD span = vs > rs ? vs : rs;
        if (rva >= va && rva < va + span) return pr + (rva - va);
    }
    return 0xFFFFFFFFu;
}

static int pe_load(const char *path, PE *pe)
{
    ZeroMemory(pe, sizeof(*pe));
    if (!read_file(path, &pe->p, &pe->n)) return 0;
    if (pe->p[0] != 'M' || pe->p[1] != 'Z') return 0;
    pe->pe = r32(pe->p + 0x3C);
    if (pe->pe + 24 > pe->n || r32(pe->p + pe->pe) != 0x00004550u) return 0;
    if (r16(pe->p + pe->pe + 4) != IMG_MACHINE_I386) return 0;
    pe->ophsz = r16(pe->p + pe->pe + 20);
    pe->oph = pe->pe + 24;
    if (pe->oph + pe->ophsz > pe->n) return 0;
    if (r16(pe->p + pe->oph) != PE32_MAGIC) return 0;
    pe->nsec = r16(pe->p + pe->pe + 6);
    pe->sectab = pe->oph + pe->ophsz;
    pe->offNsec = pe->pe + 6;
    pe->hdrSize = r32(pe->p + pe->oph + 60);
    pe->offSizeImg = pe->oph + 56;
    pe->offSizeInit = pe->oph + 8;
    pe->offCksum = pe->oph + 64;
    pe->impRva = r32(pe->p + pe->oph + 104);   /* DataDirectory[1].VirtualAddress */
    pe->expRva = r32(pe->p + pe->oph + 96);    /* DataDirectory[0].VirtualAddress */
    return 1;
}

static void pe_free(PE *pe) { if (pe->p) free(pe->p); pe->p = NULL; }

/* 遍历导入描述符；*out_off = 描述符在文件中的偏移 */
static BYTE *imp_desc(PE *pe, DWORD idx, DWORD *out_off)
{
    DWORD base = rva2off(pe, pe->impRva);
    DWORD off;
    if (base == 0xFFFFFFFFu) return NULL;
    off = base + idx * 20;
    if (off + 20 > pe->n) return NULL;
    if (out_off) *out_off = off;
    return pe->p + off;
}

static const char *imp_dll_name(PE *pe, BYTE *desc)
{
    DWORD rva = r32(desc + 12), off;
    if (!rva) return "";
    off = rva2off(pe, rva);
    if (off == 0xFFFFFFFFu || off >= pe->n) return "";
    return (const char *)(pe->p + off);
}

/* 0=原始 1=已打补丁 -1=异常 */
static int detect_state(const char *bin, char *dllpath, DWORD dllpathsz)
{
    char path[MAX_PATH * 2];
    PE pe;
    DWORD i, seenGdi = 0, seenShim = 0;

    wsprintfA(path, "%s\\afUILib.dll", bin);
    if (dllpath) lstrcpynA(dllpath, path, (int)dllpathsz);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return -1;
    if (!pe_load(path, &pe)) return -1;
    if (!pe.impRva) { pe_free(&pe); return -1; }

    for (i = 0; ; i++) {
        DWORD doff;
        BYTE *d = imp_desc(&pe, i, &doff);
        const char *nm;
        if (!d) break;
        if (!r32(d) && !r32(d + 12) && !r32(d + 16)) break;
        nm = imp_dll_name(&pe, d);
        if (ci_eq(nm, "gdi32.dll")) seenGdi++;
        if (ci_eq(nm, SHIM_NAME)) seenShim++;
    }
    pe_free(&pe);
    if (seenShim && !seenGdi) return 1;
    if (seenGdi && !seenShim) return 0;
    return -1;
}

/* ---------------------------------------------------------------- 导出表 */
typedef struct { char *names[512]; int n; } EXPLIST;

static void exports_load(PE *pe, EXPLIST *ex)
{
    DWORD off = rva2off(pe, pe->expRva), n = 0, addrn, i, arr;
    ex->n = 0;
    if (pe->expRva == 0 || off == 0xFFFFFFFFu || off + 40 > pe->n) return;
    n = r32(pe->p + off + 24);          /* NumberOfNames */
    addrn = rva2off(pe, r32(pe->p + off + 32));
    if (addrn == 0xFFFFFFFFu) return;
    if (n > 500) n = 500;
    for (i = 0; i < n; i++) {
        DWORD nmoff = rva2off(pe, r32(pe->p + addrn + i * 4));
        if (nmoff == 0xFFFFFFFFu || nmoff >= pe->n) continue;
        ex->names[ex->n++] = (char *)(pe->p + nmoff);
    }
}

static int exports_has(EXPLIST *ex, const char *name)
{
    int i;
    for (i = 0; i < ex->n; i++) if (ci_eq(ex->names[i], name)) return 1;
    return 0;
}

/* ---------------------------------------------------------------- verify */
static int cmd_verify(const char *bin, const char *shimfile)
{
    char ul[MAX_PATH * 2];
    PE pu, ps;
    EXPLIST ex;
    DWORD i, doff, cnt = 0;
    int missing = 0;

    wsprintfA(ul, "%s\\afUILib.dll", bin);
    if (GetFileAttributesA(ul) == INVALID_FILE_ATTRIBUTES) { sayf("ERR not found: %s\r\n", ul); return 1; }
    if (!pe_load(ul, &pu)) { sayf("ERR cannot parse %s\r\n", ul); return 1; }
    if (!pe_load(shimfile, &ps)) { sayf("ERR cannot parse %s\r\n", shimfile); pe_free(&pu); return 1; }

    exports_load(&ps, &ex);
    if (ex.n == 0) { sayf("ERR shim has no export table\r\n"); pe_free(&pu); pe_free(&ps); return 1; }

    for (i = 0; ; i++) {
        BYTE *d = imp_desc(&pu, i, &doff);
        const char *dll;
        DWORD thunkRva, thrOff, k;
        if (!d) break;
        if (!r32(d) && !r32(d + 12) && !r32(d + 16)) break;
        dll = imp_dll_name(&pu, d);
        if (!ci_eq(dll, "gdi32.dll")) continue;

        thunkRva = r32(d);                       /* OriginalFirstThunk */
        if (!thunkRva) thunkRva = r32(d + 16);   /* 退回 FirstThunk */
        thrOff = rva2off(&pu, thunkRva);
        if (thrOff == 0xFFFFFFFFu) { sayf("ERR import thunk not resolvable\r\n"); missing++; break; }

        for (k = 0; ; k++) {
            DWORD t = r32(pu.p + thrOff + k * 4), noff;
            const char *fn;
            if (!t) break;
            cnt++;
            if (t & 0x80000000u) {
                sayf("ERR gdi32 ordinal import #%lu cannot be forwarded\r\n", t & 0xFFFFu);
                missing++;
                continue;
            }
            noff = rva2off(&pu, t);
            if (noff == 0xFFFFFFFFu) continue;
            fn = (const char *)(pu.p + noff + 2);   /* 跳过 Hint */
            if (!exports_has(&ex, fn)) {
                if (missing < 25) sayf("MISS %s\r\n", fn);
                missing++;
            }
        }
    }
    pe_free(&pu);
    pe_free(&ps);

    if (missing) {
        sayf("RESULT FAIL gdi32_imports=%lu missing=%d\r\n", cnt, missing);
        return 1;
    }
    sayf("RESULT OK gdi32_imports=%lu shim_exports=%d\r\n", cnt, ex.n);
    return 0;
}

/* ---------------------------------------------------------------- patch 核心 */
static int do_patch_buf(PE *pe, int *was_patched)
{
    DWORD i, gdiIdx = (DWORD)-1, descOff = 0, lastIdx = 0, lastVA = 0;
    DWORD newVA, newRaw, newLen, hdrRoom, hdrOff, oldInit;

    *was_patched = 0;
    for (i = 0; ; i++) {
        DWORD doff;
        BYTE *d = imp_desc(pe, i, &doff);
        const char *nm;
        if (!d) break;
        if (!r32(d) && !r32(d + 12) && !r32(d + 16)) break;
        nm = imp_dll_name(pe, d);
        if (ci_eq(nm, SHIM_NAME)) { *was_patched = 1; return 1; }
        if (ci_eq(nm, "gdi32.dll") && gdiIdx == (DWORD)-1) { gdiIdx = i; descOff = doff; }
    }
    if (gdiIdx == (DWORD)-1) { sayf("ERR gdi32 import descriptor not found\r\n"); return 0; }

    /* 最后一个节的表项（文件顺序）与最大虚拟地址 */
    for (i = 0; i < pe->nsec; i++) {
        DWORD va = r32(pe->p + pe->sectab + i * 40 + 12);
        if (i == 0 || va > lastVA) { lastVA = va; if (i != 0 || 1) lastIdx = i; }
        if (va == lastVA) lastIdx = i;
    }
    /* lastIdx 为最大 VA 的节；表项位置 */
    {
        DWORD tblLast = pe->nsec - 1;
        BYTE *s = pe->p + pe->sectab + tblLast * 40;
        DWORD vs = r32(s + 8), rs = r32(s + 16);
        DWORD span = vs > rs ? vs : rs;
        hdrRoom = pe->hdrSize - (pe->sectab + pe->nsec * 40 + 40);
        if ((int)hdrRoom <= 0) { sayf("ERR no header room (%ld bytes) for a new section\r\n", (long)hdrRoom); return 0; }
        newVA = alignup(lastVA + span, SECT_ALIGN);
        hdrOff = pe->sectab + pe->nsec * 40;
        (void)lastIdx;
    }

    newRaw = alignup(pe->n, FILE_ALIGN);
    newLen = newRaw + FILE_ALIGN;

    pe->p = (BYTE *)realloc(pe->p, newLen);
    if (!pe->p) { sayf("ERR out of memory\r\n"); return 0; }
    if (newLen > pe->n) ZeroMemory(pe->p + pe->n, newLen - pe->n);
    pe->n = newLen;

    /* 新节头 */
    {
        BYTE *h = pe->p + hdrOff;
        ZeroMemory(h, 40);
        memcpy(h, ".shim", 5);
        w32(h + 8, SHIM_NAME_LEN);
        w32(h + 12, newVA);
        w32(h + 16, FILE_ALIGN);
        w32(h + 20, newRaw);
        w32(h + 36, SEC_CHAR);
    }
    w16(pe->p + pe->offNsec, (WORD)(pe->nsec + 1));
    w32(pe->p + pe->offSizeImg, newVA + SECT_ALIGN);
    oldInit = r32(pe->p + pe->offSizeInit);
    w32(pe->p + pe->offSizeInit, alignup(oldInit + FILE_ALIGN, FILE_ALIGN));

    memcpy(pe->p + newRaw, SHIM_NAME, SHIM_NAME_LEN);

    {
        DWORD oldName = r32(pe->p + descOff + 12);
        w32(pe->p + descOff + 12, newVA);
        if (g_verbose) sayf("INFO descriptor #%lu name 0x%lX -> 0x%lX\r\n", gdiIdx, oldName, newVA);
    }
    w32(pe->p + pe->offCksum, 0);
    return 1;
}

/* ---------------------------------------------------------------- 命令 */
static int cmd_check(const char *bin)
{
    char dll[MAX_PATH * 2];
    int st = detect_state(bin, dll, sizeof(dll));
    if (st == -1) { sayf("STATE UNKNOWN\r\n"); return 1; }
    sayf("STATE %s\r\n", st ? "PATCHED" : "ORIGINAL");
    return st ? 3 : 0;
}

static int cmd_patch(const char *bin, const char *outOverride)
{
    char dll[MAX_PATH * 2];
    PE pe;
    int was = 0;

    detect_state(bin, dll, sizeof(dll));
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) { sayf("ERR not found: %s\r\n", dll); return 1; }
    if (!pe_load(dll, &pe)) { sayf("ERR cannot parse %s\r\n", dll); return 1; }
    if (!do_patch_buf(&pe, &was)) { pe_free(&pe); return 1; }
    if (was) { sayf("INFO already patched, nothing to do\r\n"); pe_free(&pe); return 0; }

    if (!write_file_atomic(outOverride ? outOverride : dll, pe.p, pe.n)) { pe_free(&pe); return 1; }
    sayf("OK patched %lu bytes\r\n", pe.n);
    pe_free(&pe);

    if (!outOverride) {
        int st = detect_state(bin, dll, sizeof(dll));
        if (st != 1) { sayf("ERR readback check failed state=%d\r\n", st); return 1; }
        sayf("OK verify PATCHED\r\n");
    }
    return 0;
}

static int cmd_patchfile(const char *in, const char *out)
{
    PE pe;
    int was = 0;
    if (!pe_load(in, &pe)) { sayf("ERR cannot parse %s\r\n", in); return 1; }
    if (!do_patch_buf(&pe, &was)) { pe_free(&pe); return 1; }
    if (was) { sayf("INFO input is already patched\r\n"); pe_free(&pe); return 0; }
    if (!write_file_atomic(out, pe.p, pe.n)) { pe_free(&pe); return 1; }
    sayf("OK patched %lu bytes -> %s\r\n", pe.n, out);
    pe_free(&pe);
    return 0;
}

static void get_stamp(char *out, int n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfA(out, "%04d%02d%02d_%02d%02d%02d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static int cmd_stamp(void)
{
    char s[32];
    get_stamp(s, sizeof(s));
    sayf("%s\r\n", s);
    return 0;
}

static void query_ver(const char *versionKey, char *out, int n)
{
    char sub[MAX_PATH], val[256];
    HKEY hk;
    DWORD sz = sizeof(val);
    out[0] = 0;
    wsprintfA(sub, "SOFTWARE\\ESRI\\%s", versionKey);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | KEY_WOW64_32KEY, &hk) == ERROR_SUCCESS) {
        sz = sizeof(val);
        if (RegQueryValueExA(hk, "RealVersion", NULL, NULL, (LPBYTE)val, &sz) == ERROR_SUCCESS) {
            lstrcpynA(out, val, n);
            lstrcatA(out, " build ");
            {
                char b[64];
                DWORD sz2 = sizeof(b);
                if (RegQueryValueExA(hk, "BuildNumber", NULL, NULL, (LPBYTE)b, &sz2) == ERROR_SUCCESS) {
                    b[sz2] = 0;
                    lstrcatA(out, b);
                }
            }
        }
        RegCloseKey(hk);
    }
    if (!out[0]) {
        /* 10.8 之类没有 RealVersion，用 Version */
        sz = sizeof(val);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | KEY_WOW64_32KEY, &hk) == ERROR_SUCCESS) {
            if (RegQueryValueExA(hk, "Version", NULL, NULL, (LPBYTE)val, &sz) == ERROR_SUCCESS) {
                val[sz] = 0;
                lstrcpynA(out, val, n);
            }
            RegCloseKey(hk);
        }
    }
    if (!out[0]) lstrcpynA(out, versionKey, n);
}

/* 由一个 bin 目录反推版本键名（Desktop10.2 等） */
static void verkey_from_bin(const char *bin, char *out, int n)
{
    const char *p = bin, *best = NULL;
    while ((p = strstr(p, "Desktop10")) != NULL) { best = p; p++; }
    out[0] = 0;
    if (!best) return;
    {
        int i = 0;
        while (best[i] && best[i] != '\\' && i < n - 1) { out[i] = best[i]; i++; }
        out[i] = 0;
    }
}

static int dir_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void emit_version(const char *vkey, const char *installDir)
{
    char bin[MAX_PATH * 2], dll[MAX_PATH * 2], ver[128];
    lstrcpynA(bin, installDir, MAX_PATH * 2);
    {
        int L = lstrlenA(bin);
        if (L && bin[L - 1] == '\\') bin[L - 1] = 0;
    }
    if (!ci_starts(bin, "\\bin") && !(lstrlenA(bin) > 4 && ci_eq(bin + lstrlenA(bin) - 4, "\\bin"))) {
        lstrcatA(bin, "\\bin");
    }
    wsprintfA(dll, "%s\\afUILib.dll", bin);
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) return;
    query_ver(vkey, ver, sizeof(ver));
    sayf("%s|%s|%s\r\n", vkey, bin, ver);
}

static int cmd_discover(void)
{
    HKEY hk;
    DWORD idx = 0;
    char name[256];
    int found = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ESRI", 0,
                      KEY_READ | KEY_WOW64_32KEY, &hk) == ERROR_SUCCESS) {
        for (;;) {
            DWORD n = sizeof(name);
            DWORD rc;
            FILETIME ft;
            rc = RegEnumKeyExA(hk, idx++, name, &n, NULL, NULL, NULL, &ft);
            if (rc != ERROR_SUCCESS) break;
            if (ci_starts(name, "Desktop10")) {
                char sub[MAX_PATH], dir[MAX_PATH * 2];
                HKEY hv;
                DWORD sz = sizeof(dir);
                wsprintfA(sub, "SOFTWARE\\ESRI\\%s", name);
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | KEY_WOW64_32KEY, &hv) == ERROR_SUCCESS) {
                    if (RegQueryValueExA(hv, "InstallDir", NULL, NULL, (LPBYTE)dir, &sz) == ERROR_SUCCESS) {
                        dir[sz] = 0;
                        emit_version(name, dir);
                        found++;
                    } else if (RegQueryValueExA(hv, "TargetPath", NULL, NULL, (LPBYTE)dir, &sz) == ERROR_SUCCESS) {
                        dir[sz] = 0;
                        emit_version(name, dir);
                        found++;
                    }
                    RegCloseKey(hv);
                }
            }
        }
        RegCloseKey(hk);
    }

    if (!found) {
        /* 兜底：扫 Program Files 下的 ArcGIS\Desktop10* */
        const char *envs[2] = { "ProgramFiles(x86)", "ProgramFiles" };
        int e;
        for (e = 0; e < 2; e++) {
            char base[MAX_PATH], pat[MAX_PATH * 2];
            WIN32_FIND_DATAA fd;
            HANDLE hf;
            if (!GetEnvironmentVariableA(envs[e], base, MAX_PATH)) continue;
            wsprintfA(pat, "%s\\ArcGIS\\Desktop10*", base);
            hf = FindFirstFileA(pat, &fd);
            if (hf == INVALID_HANDLE_VALUE) continue;
            do {
                char install[MAX_PATH * 2], vk[64];
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                wsprintfA(install, "%s\\ArcGIS\\%s", base, fd.cFileName);
                verkey_from_bin(install, vk, sizeof(vk));
                if (vk[0]) emit_version(vk, install);
            } while (FindNextFileA(hf, &fd));
            FindClose(hf);
        }
    }
    return 0;
}

static int mkdir_p(const char *path)
{
    char tmp[MAX_PATH * 2];
    int i, L;
    lstrcpynA(tmp, path, MAX_PATH * 2);
    L = lstrlenA(tmp);
    for (i = 3; i < L; i++) {
        if (tmp[i] == '\\') {
            tmp[i] = 0;
            if (!dir_exists(tmp)) CreateDirectoryA(tmp, NULL);
            tmp[i] = '\\';
        }
    }
    if (!dir_exists(tmp)) CreateDirectoryA(tmp, NULL);
    return dir_exists(tmp);
}

static int copy_file(const char *src, const char *dst)
{
    BYTE *d;
    DWORD n;
    if (!read_file(src, &d, &n)) return 0;
    if (!write_file_atomic(dst, d, n)) { free(d); return 0; }
    free(d);
    return 1;
}

static int cmd_backup(const char *bin, const char *root)
{
    char dll[MAX_PATH * 2], host[128], stamp[32], dir[MAX_PATH * 2], info[MAX_PATH * 2];
    char vk[64], ver[128], line[512];
    DWORD hsz = sizeof(host);
    HANDLE h;
    DWORD wr = 0;

    wsprintfA(dll, "%s\\afUILib.dll", bin);
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) { sayf("ERR not found: %s\r\n", dll); return 1; }

    host[0] = 0;
    if (!GetComputerNameA(host, &hsz)) lstrcpynA(host, "PC", sizeof(host));
    get_stamp(stamp, sizeof(stamp));
    verkey_from_bin(bin, vk, sizeof(vk));
    query_ver(vk[0] ? vk : "Desktop10.2", ver, sizeof(ver));

    wsprintfA(dir, "%s\\%s_%s_%s", root, host, stamp, vk[0] ? vk : "ArcGIS");
    if (!mkdir_p(dir)) { sayf("ERR cannot create backup dir %s\r\n", dir); return 1; }

    wsprintfA(info, "%s", dir);
    lstrcatA(info, "\\afUILib.dll.orig");
    if (!copy_file(dll, info)) { sayf("ERR backup copy failed\r\n"); return 1; }

    {
        char metapath[MAX_PATH * 2];
        WIN32_FILE_ATTRIBUTE_DATA fad;
        DWORD sz = 0;
        if (GetFileAttributesExA(dll, GetFileExInfoStandard, &fad))
            sz = fad.nFileSizeLow;
        wsprintfA(metapath, "%s\\info.txt", dir);
        h = CreateFileA(metapath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wsprintfA(line, "HOST=%s\r\nTIME=%04d-%02d-%02d %02d:%02d:%02d\r\nVER=%s\r\nBIN=%s\r\nSIZE=%lu\r\nAFUILIB=afUILib.dll.orig\r\n",
                      host, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                      ver, bin, sz);
            WriteFile(h, line, lstrlenA(line), &wr, NULL);
            CloseHandle(h);
        }
    }
    sayf("OK %s\r\n", dir);
    return 0;
}

static int read_info_bin(const char *bakdir, char *bin, int n)
{
    char path[MAX_PATH * 2];
    BYTE *d;
    DWORD len, i;
    bin[0] = 0;
    wsprintfA(path, "%s\\info.txt", bakdir);
    if (!read_file(path, &d, &len)) return 0;
    for (i = 0; i + 4 < len; i++) {
        if (d[i] == 'B' && d[i + 1] == 'I' && d[i + 2] == 'N' && d[i + 3] == '=') {
            DWORD j = i + 4;
            int k = 0;
            while (j < len && d[j] != '\r' && d[j] != '\n' && k < n - 1) bin[k++] = (char)d[j++];
            bin[k] = 0;
            break;
        }
    }
    free(d);
    return bin[0] != 0;
}

static int cmd_restore(const char *bakdir)
{
    char bin[MAX_PATH * 2], src[MAX_PATH * 2], dst[MAX_PATH * 2], shim[MAX_PATH * 2];
    int st;

    if (!read_info_bin(bakdir, bin, sizeof(bin))) { sayf("ERR info.txt missing in backup dir\r\n"); return 1; }
    wsprintfA(src, "%s\\afUILib.dll.orig", bakdir);
    wsprintfA(dst, "%s\\afUILib.dll", bin);
    if (GetFileAttributesA(src) == INVALID_FILE_ATTRIBUTES) { sayf("ERR backup file missing\r\n"); return 1; }
    if (!dir_exists(bin)) { sayf("ERR target dir not found: %s\r\n", bin); return 1; }

    if (!copy_file(src, dst)) { sayf("ERR restore failed (file locked?)\r\n"); return 1; }

    wsprintfA(shim, "%s\\" SHIM_NAME, bin);
    DeleteFileA(shim);
    wsprintfA(shim, "%s\\afFontShim.ini", bin);
    DeleteFileA(shim);
    wsprintfA(shim, "%s\\afFontShim.log", bin);
    DeleteFileA(shim);

    st = detect_state(bin, dst, sizeof(dst));
    if (st != 0) { sayf("ERR post-restore check failed state=%d\r\n", st); return 1; }
    sayf("OK restored %s\r\n", bin);
    return 0;
}

static int cmp_desc(const void *a, const void *b)
{
    return -lstrcmpiA(*(const char **)a, *(const char **)b);
}

static int cmd_list(const char *root)
{
    char pat[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE hf;
    char *dirs[128];
    int n = 0, i;

    wsprintfA(pat, "%s\\*", root);
    hf = FindFirstFileA(pat, &fd);
    if (hf == INVALID_HANDLE_VALUE) { sayf("NONE\r\n"); return 0; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (n < 128) {
            int L = lstrlenA(fd.cFileName);
            dirs[n] = (char *)malloc(L + 1);
            if (dirs[n]) { lstrcpynA(dirs[n], fd.cFileName, L + 1); n++; }
        }
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);

    qsort(dirs, n, sizeof(char *), cmp_desc);
    /* 按目标目录去重：每个 bin 只保留最新的一份备份（dirs 已按时间倒序） */
    {
        static char seen[64][MAX_PATH * 2];
        int nseen = 0, out = 0, k;
        for (i = 0; i < n; i++) {
            char full[MAX_PATH * 2], bin[MAX_PATH * 2], dup = 0;
            wsprintfA(full, "%s\\%s", root, dirs[i]);
            if (read_info_bin(full, bin, sizeof(bin))) {
                for (k = 0; k < nseen; k++) if (ci_eq(seen[k], bin)) { dup = 1; break; }
                if (!dup && nseen < 64) {
                    lstrcpynA(seen[nseen++], bin, MAX_PATH * 2);
                    out++;
                    sayf("%d|%s|%s\r\n", out, dirs[i], bin);
                }
            }
            free(dirs[i]);
        }
    }
    if (!n) sayf("NONE\r\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd;

    open_report();
    if (argc < 2) {
        sayf("afpatch " TOOLVER "  ArcMap font-list patch tool\r\n");
        sayf("usage: afpatch discover|check|verify|patch|patchfile|backup|restore|list|stamp|version\r\n");
        return 1;
    }
    cmd = argv[1];
    if (cmd[0] == '-' && cmd[1] == 'v') { g_verbose = 1; argc--; argv++; cmd = argv[1]; }
    if (argc >= 3 && ci_eq(argv[2], "-v")) g_verbose = 1;

    if (ci_eq(cmd, "version")) { sayf("afpatch " TOOLVER "\r\n"); return 0; }
    if (ci_eq(cmd, "stamp")) return cmd_stamp();
    if (ci_eq(cmd, "discover")) return cmd_discover();
    if (ci_eq(cmd, "list")) {
        if (argc < 3) return 1;
        return cmd_list(argv[2]);
    }
    if (ci_eq(cmd, "check")) {
        if (argc < 3) return 1;
        return cmd_check(argv[2]);
    }
    if (ci_eq(cmd, "verify")) {
        if (argc < 4) return 1;
        return cmd_verify(argv[2], argv[3]);
    }
    if (ci_eq(cmd, "patch")) {
        if (argc < 3) return 1;
        return cmd_patch(argv[2], NULL);
    }
    if (ci_eq(cmd, "patchfile")) {
        if (argc < 4) return 1;
        return cmd_patchfile(argv[2], argv[3]);
    }
    if (ci_eq(cmd, "backup")) {
        if (argc < 4) return 1;
        return cmd_backup(argv[2], argv[3]);
    }
    if (ci_eq(cmd, "restore")) {
        if (argc < 3) return 1;
        return cmd_restore(argv[2]);
    }
    sayf("ERR unknown command: %s\r\n", cmd);
    return 1;
}
