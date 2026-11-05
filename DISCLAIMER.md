# 免责声明 / Disclaimer

[中文](#免责声明) · [English](#disclaimer)

---

## 免责声明

本软件（下称"本工具"）由个人开发者**春三月**以学习与技术研究为目的编写并开源发布，**与 Esri 公司无任何关联**，未经 Esri 授权、认可或支持。ArcGIS、ArcMap、ArcCatalog、ArcScene 等名称是 Esri 在美国及其他国家/地区的商标或注册商标，本文中仅为说明兼容性而使用。

**请在完整阅读并理解以下条款后再使用本工具。下载、安装或使用本工具，即视为你已知悉并接受全部条款。**

1. **风险自担。** 本工具会修改你本机上已安装的 ArcGIS Desktop 程序目录中的 `afUILib.dll`，并向同目录写入 `afFontShim.dll` / `afFontShim.ini`。此类二进制修改存在不可预见的风险，包括但不限于：ArcMap 无法启动、功能异常、工程文档（.mxd）打开失败、与其他插件或扩展冲突、ArcGIS 修复安装/升级失败。**使用本工具的全部风险由你自行承担。**

2. **务必先备份。** 安装脚本会自动把原始 `afUILib.dll` 备份到 `%ProgramData%\ArcMapFontFix\backup\<主机名>_<时间戳>_<版本>\`，并提供一键回退脚本。**在未成功备份前，请勿继续安装。** 建议在生产环境使用前，先在非关键机器或虚拟机中验证。仍建议你对重要工程数据另行备份。

3. **无担保。** 本工具按"现状"（AS IS）提供，不附带任何明示或暗示的担保，包括但不限于适销性、特定用途适用性、无侵权及无缺陷的担保。作者不保证本工具适用于你的具体环境，也不保证其无错误或可连续无中断运行。

4. **责任限制。** 在适用法律允许的最大范围内，作者不对因使用或无法使用本工具而导致的任何直接、间接、附带、特殊、惩罚性或后果性损害承担责任，包括但不限于：数据丢失或损坏、工期延误、生产中断、项目损失、设备损坏、第三方索赔、商誉损失及利润损失。即使作者已被告知可能发生此类损害，亦同。

5. **不提供官方支持。** 本工具不是 Esri 官方补丁，不受 Esri 技术支持覆盖。若你的组织受软件许可或 IT 管理政策约束（例如禁止修改已安装程序文件），**请在实施前取得相应授权**，由此产生的合规责任由使用者承担。

6. **合规使用。** 你应确保在所在国家/地区法律及你与 Esri 之间的许可协议允许的范围内使用本工具。本工具不包含、不反编译、不分发任何 Esri 源代码或二进制文件；它只在你自己机器上、对你自己的已授权安装副本做运行时导入表重定向。

7. **无网络行为。** 本工具不联网、不上传、不采集任何数据，不含遥测与自动更新逻辑。

8. **可回退。** 任何时候双击 `03-回退还原.bat`，或手动把备份目录中的 `afUILib.dll.orig` 复制回 ArcGIS `bin` 目录覆盖 `afUILib.dll`，即可完全恢复原始状态（本工具新增的两个 `afFontShim.*` 文件可直接删除，不残留服务、驱动或注册表项）。

9. **条款变更。** 作者保留随时修改本工具及本免责声明的权利，恕不另行通知。条款如有更新，以仓库中的最新版本为准。

10. **第三方组件与商标。** 本工具不含任何 Esri 源代码或二进制文件，也不对其再分发；`afFontShim.dll` / `afpatch.exe` 均为本项目原创代码，用 TinyCC 编译。ArcGIS、ArcMap、ArcCatalog、ArcScene 是 Esri 在美国及其他国家/地区的商标或注册商标，本项目仅出于说明兼容性的目的使用这些名称，不表示任何隶属、赞助或背书关系。若权利人认为本仓库存在不当之处，请通过仓库 Issue 联系，作者将及时处理。

> 一句话总结：**这是非官方补丁，改的是你本机的程序文件，请先备份、先在非关键机器上验证，出问题的后果由使用者自行承担。**

---

## Disclaimer

This software (the "Tool") is written and open-sourced by an individual developer, **Chun San Yue (春三月)**, for learning and technical-research purposes. It is **not affiliated with, authorized by, endorsed by, or supported by Esri**. ArcGIS, ArcMap, ArcCatalog and ArcScene are trademarks or registered trademarks of Esri in the United States and other jurisdictions, used here only to describe compatibility.

**Please read and understand the following before use. By downloading, installing or using the Tool you acknowledge and accept all of the terms below.**

1. **Use at your own risk.** The Tool modifies `afUILib.dll` inside your locally installed ArcGIS Desktop directory and writes `afFontShim.dll` / `afFontShim.ini` next to it. Binary modification carries unforeseeable risk, including but not limited to: ArcMap failing to start, broken functionality, failure to open map documents (.mxd), conflicts with other add-ins or extensions, and failed ArcGIS repair/upgrade. **You assume all risk.**

2. **Back up first.** The installer automatically backs up the original `afUILib.dll` to `%ProgramData%\ArcMapFontFix\backup\<host>_<timestamp>_<version>\` and ships a one-click restore script. **Do not proceed if the backup did not succeed.** Validate on a non-critical machine or a VM before rolling out to production, and keep independent backups of your project data.

3. **No warranty.** The Tool is provided "AS IS", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, non-infringement and freedom from defects. The author does not warrant that the Tool will work in your specific environment, be error-free, or run uninterrupted.

4. **Limitation of liability.** To the maximum extent permitted by applicable law, the author shall not be liable for any direct, indirect, incidental, special, punitive or consequential damages arising from the use of, or inability to use, the Tool — including but not limited to loss or corruption of data, schedule delays, production downtime, loss of project revenue, equipment damage, third-party claims, loss of goodwill and loss of profit — even if advised of the possibility of such damages.

5. **No official support.** The Tool is not an Esri official patch and is not covered by Esri support. If your organization is bound by software-licensing or IT-management policies (e.g. a prohibition on modifying installed program files), **obtain the necessary authorization first**; compliance responsibility rests with the user.

6. **Lawful use.** You must use the Tool within the limits of the laws of your jurisdiction and your license agreement with Esri. The Tool contains, decompiles and redistributes no Esri source code or binaries; it performs a runtime import-table redirection on your own licensed installation, on your own machine.

7. **No network activity.** The Tool does not connect to the internet, does not upload or collect any data, and contains no telemetry or auto-update logic.

8. **Reversible.** Double-click `03-回退还原.bat` at any time, or manually copy `afUILib.dll.orig` from the backup folder back over `afUILib.dll` in the ArcGIS `bin` directory, to fully restore the original state. The two added files `afFontShim.*` can simply be deleted — no services, drivers or registry entries are left behind.

9. **Changes.** The author may modify the Tool and this disclaimer at any time without notice. The latest version in this repository prevails.

10. **Third-party components and trademarks.** The Tool contains and redistributes no Esri source code or binaries. `afFontShim.dll` and `afpatch.exe` are original code from this project, compiled with TinyCC. ArcGIS, ArcMap, ArcCatalog and ArcScene are trademarks or registered trademarks of Esri in the United States and other jurisdictions; these names are used solely to describe compatibility and imply no affiliation, sponsorship or endorsement. If a rights holder believes anything here is improper, please open an Issue and it will be addressed promptly.

> TL;DR — **This is an unofficial patch that modifies program files on your own machine. Back up first, test on a non-critical machine, and accept that you bear the consequences.**
