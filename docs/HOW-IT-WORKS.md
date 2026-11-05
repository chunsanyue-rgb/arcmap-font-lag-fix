# 实现原理与踩坑记录 / How It Works

[中文](#中文) · [English](#english)

---

## 中文

### 1. 定位过程

问题表现为 UI 线程假死，但"假死"只是表象，需要先确定时间到底花在哪个函数里。做法是**对一个 32 位进程做栈采样**：

1. 找到 ArcMap 进程里正在打开标注页签的 UI 线程；
2. `SuspendThread` + `Wow64GetThreadContext` 拿到 32 位上下文；
3. 从 `Eip`/`Esp` 沿栈扫描，把返回地址映射回模块 + 导出符号；
4. 连续采样几百次，按帧聚类。

聚类结果非常干净：绝大多数样本落在 `afUILib.dll` 与 `user32!SendMessageW` 之间来回跳，且调用方固定是字体枚举回调。把 `SendMessageW` 的第二个参数打出来，看到的是 `CB_GETCOUNT` / `CB_GETITEMDATA` 的高频循环 —— 到这里根因就清楚了。

> 采样器的一个坑：不显式声明 `argtypes`，64 位 Python 下 `HWND` 会被截断成 32 位，`OpenThread`/`GetWindowThreadProcessId` 全部拿不到正确句柄。四个 user32 函数签名必须补齐。

### 2. 三层修复的取舍

| 层 | 备选方案 | 为什么选它 |
|---|---------|-----------|
| ① 枚举 | 改 afUILib 的代码段，跳过枚举 / 每次新建 DC / **自己建内存 DC 枚举一次并缓存** | 改代码段要处理指令长度与重定位；内存 DC 天然快（`GetDeviceCaps(LOGPIXELSY)` 直接给 96 之类的小值，避免按大分辨率 DC 枚举）且可缓存，0 风险 |
| ② 预去重 | 按"字体名"去重 / 按"名字+类型"去重 / 不去重 | 必须与 afUILib 自己的判重口径一致。实测"名字+类型"得 783 条、"仅名字"得 779 条，而应用侧最终就是 779 条 —— 口径对齐才能保证列表**逐条相同** |
| ③ 判重循环 | 改代码段 NOP 掉循环 / Hook `SendMessageW` 改写消息 / **Hook 后让 `CB_GETCOUNT` 返回 0** | NOP 需要精确定位指令边界；改写消息会污染行为；让 `CB_GETCOUNT` 返回 0 只影响"进入循环的条件"，`CB_ADDSTRING`/`CB_SETITEMDATA` 完全照常，副作用最小 |

### 3. 为什么是 shim，而不是重打包 MSI

官方的 MSP 补丁是直接替换 `afUILib.dll`。我们要达到同样效果，有三条路：

| 路线 | 代价 |
|------|------|
| 反编译 → 改代码 → 重编译 | 不现实（无源码） |
| 打代码段补丁（Inline Hook / NOP） | 需处理指令长度、重定位、校验和；一处错就是启动崩溃；不可校验 |
| **改导入表，把一组 gdi32 导入整体重定向到自己的 DLL** | 只改一个字符串指针 + 加一个只读节；thunk 数组完全不动；可用 `check` 精确校验；不匹配直接拒绝 |

选第三条。原理：

```text
IMAGE_IMPORT_DESCRIPTOR[i].Name  ──原──>  "gdi32.dll\0"
                                  └─改─>  "afFontShim.dll\0"   (放在新增的 .shim 节里)
```

因为 loader 是按**描述符**解析的，`OriginalFirstThunk`/`FirstThunk` 里的函数名表一条都不用动 —— 只要 shim **导出了同名同序的全部函数**，加载就成功。afUILib 从 gdi32 导入 47 个函数，shim 就导出 47 个（45 个纯透传 + `EnumFontFamiliesExW` 与 `SendMessageW` 两个钩子，加上 `EnumFontFamiliesExA/W` 的别名处理，最终导出 49 项）。

### 4. 编译链上的三个坑

**坑 1：TinyCC 不支持 `.def` 文件驱动命名。** 只能用 `__declspec(dllexport)` 导出，但那样导出名会带上源码里的标识符。解决办法：源码里用 `_w_` 前缀导出（如 `_w_SendMessageW`），编译完用 `patch_exports.py` 做**等长字节改名**，把导出表里的名字改成真正的 GDI 名（`_w_SendMessageW` → `SendMessageW`，长度相同，只需原地覆写字符串）。

**坑 2：tcc 的 win32 导入库缺 gdi32 条目。** 直接调用 `CreateCompatibleDC` 链接不过。解决办法：这些函数在运行时 `GetProcAddress(GetModuleHandleA("gdi32.dll"), ...)` 取，绕开链接期依赖。

**坑 3：tcc 生成的控制台程序在某些代码布局下 `printf` 完全静默。** 这是一个代码生成层面的 bug，改代码结构能让它"碰巧好"，但不可靠。最终决定**彻底不用标准输出**：所有结果写进程序同目录的 `afpatch.out`（失败退 `%TEMP%`），中文提示交给外层 bat 用 `echo` 输出。这也顺带解决了控制台代码页与中文乱码问题。

### 5. 其他的坑

- **`read_file` 的最小长度限制**：早期实现要求文件 ≥ 512 字节，导致读取 200 字节的 `info.txt` 直接失败、被判定为"备份不存在"。改为 `sz == 0` 才失败。
- **`GetFileSize` 参数用错**：把一个路径字符串当文件句柄传进去，`info.txt` 里记录的 SIZE 是垃圾值。改用 `GetFileAttributesExA` 取 `nFileSizeLow`。
- **管理员权限下 OpenThread 被拒**：ArcMap 以管理员身份运行时，普通权限的采样器 `OpenThread(THREAD_SUSPEND_RESUME)` 直接 `ERROR_ACCESS_DENIED`。解决办法是把整个采样器也用 `Start-Process -Verb RunAs` 提起来 —— 注意必须加 `-Wait`，否则父 PowerShell 立刻退出，子进程被回收。
- **两遍枚举翻倍成本**：v2 试过"先枚举一遍计数，再枚举一遍填充"，结果成本翻倍。改为单遍枚举 + `HeapAlloc` 扩容（一开始误用 `HeapReAlloc(NULL, ...)` 失败，因为它只能 realloc 已存在的块）。

### 6. 校验与安全闸

`afpatch.exe verify` 做的事，就是安装前的守门人：

1. 解析目标 `afUILib.dll` 的导入目录；
2. 找到 gdi32 描述符，枚举其 `OriginalFirstThunk` 里的 47 个函数名；
3. 与 shim 的导出表逐条比对（名字必须全在）；
4. 检查是否已存在 `.shim` 节（已打过则幂等跳过）；
5. 检查 PE 头剩余空间是否够放新的 section header（不够则拒绝，而不是越界写）。

任何一步不满足 → 退出码非 0，bat 显示"该版本不支持/结构不符"，**不改任何文件**。

### 7. 回放期抑制的线程安全

`CB_GETCOUNT` 返回 0 只在**调用线程正处于"回放字体条目"这段代码内**时生效，用 TLS 深度计数控制：

```c
__declspec(thread) static int g_replayDepth = 0;
```

- 回放开始时 `g_replayDepth++`，结束时 `--`；
- 钩子里判断 `g_replayDepth > 0 && msg == CB_GETCOUNT` 才返回 0；
- 其他线程、其他消息、其他窗口类一律 `return RealSendMessageW(...)`。

因为深度是 TLS 的，多开 ArcMap 窗口/多线程互不干扰。

---

## English

### 1. Locating the hot spot

The symptom is a frozen UI thread, which tells you nothing about *where*. We sampled stacks of the 32-bit ArcMap process while the Labels tab was opening: `SuspendThread` → `Wow64GetThreadContext` → scan the stack from `Eip`/`Esp`, map return addresses back to module + export → cluster a few hundred samples.

The clusters were unambiguous: samples bounced between `afUILib.dll` and `user32!SendMessageW`, always from the font-enumeration callback. Logging `SendMessageW`'s message id exposed a tight `CB_GETCOUNT` / `CB_GETITEMDATA` loop — root cause found.

> Sampler gotcha: without explicit `argtypes`, 64-bit Python truncates `HWND` to 32 bits and every handle lookup silently fails.

### 2. Why these three layers

| Layer | Alternatives | Choice rationale |
|-------|--------------|------------------|
| 1. Enumeration | patch code to skip it / new DC per call / **own memory DC, enumerate once, cache** | Code patching needs instruction-length and relocation handling; a memory DC is inherently fast (`GetDeviceCaps(LOGPIXELSY)` gives a small value instead of enumerating against a high-DPI DC) and cacheable — zero risk |
| 2. Pre-dedupe | by name / by name+type / none | Must match `afUILib`'s own key. name+type yields 783 items, name-only yields 779 — and 779 is what the app actually shows, so matching the key is what guarantees an identical list |
| 3. Kill the O(n²) loop | NOP the loop / rewrite the message / **hook and answer `CB_GETCOUNT` with 0** | NOP requires exact instruction boundaries; rewriting messages pollutes behaviour; answering 0 only affects the loop *entry condition* while `CB_ADDSTRING`/`CB_SETITEMDATA` proceed untouched — minimal blast radius |

### 3. Shim instead of a repackaged MSI

Esri's MSP replaces `afUILib.dll` outright. Without source, our options were:

| Route | Cost |
|-------|------|
| Decompile → edit → rebuild | Not viable (no source) |
| Inline patch / NOP the code section | Instruction length, relocations, checksum; one wrong byte = crash on startup; not verifiable |
| **Retarget the gdi32 import descriptor to our own DLL** | One string pointer + one read-only section; thunk arrays untouched; exactly verifiable via `check`; refuse on mismatch |

We took the third. The loader resolves by descriptor, so not a single entry in `OriginalFirstThunk`/`FirstThunk` has to change — as long as the shim exports every function name `afUILib` imports, loading succeeds. `afUILib` imports 47 gdi32 functions; the shim exports all of them (45 pass-throughs plus `EnumFontFamiliesExW` and `SendMessageW` hooks, 49 exported names in total after alias handling).

### 4. Toolchain traps

1. **TinyCC ignores `.def`-driven naming.** `__declspec(dllexport)` names come from the source identifiers, so we export `_w_SendMessageW` and later rewrite the export table in place to `SendMessageW` with an equal-length byte patch (`patch_exports.py`).
2. **tcc's win32 import lib lacks gdi32 entries.** Resolve those at runtime via `GetProcAddress(GetModuleHandleA("gdi32.dll"), ...)` to bypass link-time dependencies.
3. **tcc console output can go silently missing** under certain code layouts. Rather than hunt a codegen bug, the tool writes results to `afpatch.out` next to the exe (falling back to `%TEMP%`), and lets the wrapping `.bat` print human-readable Chinese text — which also sidesteps console code page issues.

### 5. Other war stories

- `read_file` rejected anything under 512 bytes, so a ~200-byte `info.txt` looked "missing" — relaxed to `sz == 0`.
- `GetFileSize` was fed a path string instead of a handle, producing a garbage SIZE field — switched to `GetFileAttributesExA`.
- **`OpenThread` denied under elevation**: when ArcMap runs as administrator, an unelevated sampler gets `ERROR_ACCESS_DENIED`. Elevate the sampler too via `Start-Process -Verb RunAs` — and pass `-Wait`, otherwise the parent PowerShell exits immediately and the child is reaped.
- **Two-pass enumeration doubled the cost**; switched to a single pass with `HeapAlloc` growth (the first attempt used `HeapReAlloc(NULL, ...)`, which cannot create a block).

### 6. Verification gate

Before touching anything, `afpatch.exe verify` parses the target's import directory, enumerates the 47 gdi32 function names in the descriptor, compares them against the shim's export table, checks for an existing `.shim` section (idempotency), and confirms there is room in the PE headers for one more section header (refusing instead of writing out of bounds). Any failure → non-zero exit code, the bat reports "unsupported build", **nothing is modified**.

### 7. Thread safety of the suppression

`CB_GETCOUNT → 0` applies only while the *calling thread* is inside the replay section, tracked by thread-local depth (`__declspec(thread) static int g_replayDepth`). Incremented on replay entry, decremented on exit; the hook returns 0 only when `g_replayDepth > 0`. Everything else — other threads, other messages, other window classes — is forwarded to the real `user32!SendMessageW`.
