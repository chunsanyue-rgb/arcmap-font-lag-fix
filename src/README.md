# Source layout / 源码结构

```text
src/
├─ shim/
│  ├─ gen_shim4.py      生成器：从函数清单产出 afFontShim.c（入口，先跑它）
│  ├─ afFontShim.c      生成产物：47 个 gdi32 导出（45 透传 + 2 钩子）
│  ├─ afFontShim.ini    运行期配置（也随发布包分发到 ArcGIS bin 目录）
│  ├─ patch_exports.py  等长字节改写导出名 _w_Xxx@N -> Xxx
│  └─ patch_iat.py      给 afUILib.dll 增加 .shim 节并把 gdi32 描述符指过去
├─ afpatch/
│  └─ afpatch.c         补丁/备份/还原/校验工具（原生，无依赖）
└─ package/
   └─ build_pkg.py      生成发布目录与 zip（GBK + CRLF 的 bat）
```

## 构建顺序 / build order

```bat
python src\shim\gen_shim4.py
tcc -shared -o afFontShim.dll src\shim\afFontShim.c afpatch_gdi32.def
python src\shim\patch_exports.py afFontShim.dll
tcc -o afpatch.exe src\afpatch\afpatch.c
python src\package\build_pkg.py
```

`afpatch_gdi32.def` 是 `tcc -impdef` 从系统 `gdi32.dll` 生成的导入库，不入库（每台机器的 system32 都能重新生成）：

```bat
tcc -impdef gdi32.dll -o afpatch_gdi32.def
```

## 说明 / notes

- `gen_shim4.py` 的函数清单来自 `gen_shim.py` 中的 `FUNCS` 常量（47 项），运行时会校验数量，防止手改漂移。
- `patch_iat.py` 默认**干跑**（dry-run），只打印将要做的改动；加 `--apply` 才写出 `*.patched.dll`。
- 源码注释为中文，属于开发过程记录，保留原样便于追溯每一层优化为什么这么写。
