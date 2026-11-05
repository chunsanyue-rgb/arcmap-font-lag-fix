# 常见问题 / FAQ

[中文](#中文) · [English](#english)

---

## 中文

**Q1. 装完了还是慢，怎么办？**

按顺序排查：

1. `04-查看状态.bat` 确认目标 `bin` 目录显示"已打补丁"、校验 OK；
2. 打开 `afFontShim.log`，看 `enum_ms` 和 `replay_ms` 各自多大 —— 如果日志没生成，说明 `bin` 里的 `afFontShim.ini` 被改成了 `log=0`，或者 shim 根本没被加载（回到第 1 步）；
3. 如果你装了**多个** ArcGIS 版本，`discover` 会列出全部，确认你实际启动的那个已被处理；
4. 换一个空白的 .mxd 试 —— 如果空白工程很快、只有某个工程慢，那慢的是工程里的其他东西（大图层渲染、符号库解析），不是本题。

**Q2. 会不会把 ArcMap 弄坏？**

改动面是三个文件（两处新增 + 一处导入表重定向），全部在 `bin` 目录，全部可逆；安装前有校验闸，结构不符直接拒绝而不是硬改；备份是字节级完整的原始文件。风险不为零，但可控，且一秒钟能退回去。

**Q3. 卸载要重装 ArcGIS 吗？**

不需要。`03-回退还原.bat` 或手动覆盖 `afUILib.dll.orig` 即可，删掉 `afFontShim.*` 两个文件就彻底干净（不残留服务、驱动、注册表项）。

**Q4. ArcGIS 打了 Service Pack / 修复安装之后失效了？**

正常。安装程序会把 `afUILib.dll` 换回原版。重跑 `01-安装_自动备份.bat` 即可（会自动生成新的备份，旧备份保留）。如果升到了 10.7+，就不需要了。

**Q5. 字体下拉框出现空项 / 顺序和以前不一样？**

先把 `afFontShim.ini` 的 `fast_combo` 置 0 试：恢复 → 问题在第三层；仍异常 → 再把 `use_memory_dc` 置 0 试：恢复 → 问题在第一层。把现象和日志发 issue，通常一条就能定位。

**Q6. 能不能只保留几种常用字体，让它更快？**

可以，但对已装补丁的环境意义不大（已经是毫秒级）。真需要的话用 `charset_whitelist=1,2,134,136,255` 只保留常用字符集，能进一步减少条目数。注意这会**改变**下拉框里可选的字体范围，属于功能变更，不是纯粹的性能优化。

**Q7. 支持 64 位 ArcGIS Pro 吗？**

不支持，也不需要 —— ArcGIS Pro 没有这个缺陷，本项目只针对 32 位的 ArcGIS Desktop 10.x。

**Q8. 支持 10.8 / 10.8.2 吗？**

不需要。10.8 之后官方已彻底解决（见参考里的帖子）。安装器在 10.7+ 上会因为导入表结构不同而拒绝安装，这是刻意设计。

**Q9. 这是不是破解 / 违反许可？**

不是。它不绕过任何授权、不修改许可校验、不包含任何 Esri 代码。它只是在你**自己已授权安装**的副本上，把一组 GDI 导入指向一个本地 DLL。不过，若你所在单位有"禁止修改已安装程序文件"的 IT 政策，请先取得授权。

**Q10. 为什么用 TinyCC 而不是 MSVC？**

为了把整个构建链压到最小：TCC 约 483 KB，解压即用，无需装几个 GB 的 VS；产物也就十几 KB。代价是要自己处理导出命名和导入库缺失（见 `docs/HOW-IT-WORKS.md`）。

**Q11. `/auto` 静默模式怎么用？**

```bat
01-安装_自动备份.bat /auto
```

无交互、无 UAC 二次询问（仍需进程本身有管理员权限）、适合 SCCM/GPO 推送。返回值：0 成功，非 0 见 `tool\afpatch.out`。

**Q12. 会不会拖慢 ArcMap 其他功能？**

不会。shim 只在字体枚举/回放这条路径上有逻辑，其余全部直接透传给真正的 `user32`/`gdi32`，每调用一次只是多一层跳转（纳秒级）。

---

## English

**Q1. I installed it and it is still slow. What now?**

1. Run `04-查看状态.bat` and confirm the target `bin` reports "patched" with a passing verification.
2. Open `afFontShim.log` and check `enum_ms` vs `replay_ms`. If there is no log, either `log=0` in `afFontShim.ini` or the shim was never loaded (back to step 1).
3. If you have **multiple** ArcGIS versions installed, `discover` lists them all — make sure the one you actually launch was patched.
4. Try a blank .mxd. If a blank map is fast and only one project is slow, you are looking at rendering or symbol-library cost, not this bug.

**Q2. Can it break ArcMap?**

The blast radius is three files in the `bin` folder (two additions, one import-descriptor retarget), all reversible. Verification refuses to patch on a structural mismatch instead of forcing a rewrite, and the backup is byte-exact. Risk is not zero, but it is bounded — and rollback takes one second.

**Q3. Do I need to reinstall ArcGIS to remove it?**

No. Run `03-回退还原.bat` or copy `afUILib.dll.orig` back over `afUILib.dll`, then delete the two `afFontShim.*` files. No services, drivers or registry keys are left behind.

**Q4. It stopped working after a service pack / repair install.**

Expected — those restore the original `afUILib.dll`. Re-run `01-安装_自动备份.bat` (a new backup is created; old ones are kept). On 10.7+ you no longer need it.

**Q5. Blank entries or a different order in the font dropdown.**

Set `fast_combo=0` first: recovered → layer 3 is implicated. Still wrong → set `use_memory_dc=0`: recovered → layer 1. Attach the log to an issue; it usually pins the cause in one step.

**Q6. Can I keep only a few common fonts to make it even faster?**

You can (`charset_whitelist=1,2,134,136,255`), but on a patched install it is already millisecond-scale. Note this **changes** which fonts are offered — a functional change, not a pure optimisation.

**Q7. Does it support 64-bit ArcGIS Pro?**

No, and it does not need to — Pro does not have this defect. This project targets 32-bit ArcGIS Desktop 10.x only.

**Q8. What about 10.8 / 10.8.2?**

Not needed; fixed upstream after 10.8. On 10.7+ the installer refuses because the import table no longer matches — by design.

**Q9. Is this cracking or a licence violation?**

No. It bypasses no licensing, validates no licence differently, and contains no Esri code. It redirects a group of GDI imports to a local DLL on your own licensed installation. That said, if your organisation forbids modifying installed program files, get authorisation first.

**Q10. Why TinyCC instead of MSVC?**

To keep the toolchain minimal: ~483 KB, unzip and go, no multi-GB VS install; binaries are ~14–19 KB. The price is handling export naming and missing import-lib entries yourself (see `docs/HOW-IT-WORKS.md`).

**Q11. How does `/auto` work?**

`01-安装_自动备份.bat /auto` — no interaction, no second UAC prompt (the process still needs admin rights), SCCM/GPO friendly. Exit code 0 on success; otherwise inspect `tool\afpatch.out`.

**Q12. Does it slow anything else down?**

No. The shim only carries logic on the font enumeration/replay path; everything else is forwarded straight to the real `user32`/`gdi32` at the cost of one extra jump (nanoseconds).
