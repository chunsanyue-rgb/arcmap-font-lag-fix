# -*- coding: utf-8 -*-
"""组装可携带到其它电脑的 ArcMap 字体列表补丁安装包。

产物：
dist/ArcMap字体列表卡顿补丁_v1.0/
    01-安装_自动备份.bat
    02-仅备份原文件.bat
    03-回退还原.bat
    使用说明.txt
    tool/afpatch.exe
    tool/afFontShim.dll
    tool/afFontShim.ini
"""
import os
import shutil

SRC = os.path.dirname(os.path.abspath(__file__))
PKGNAME = "ArcMap字体列表卡顿补丁_v1.0"
OUT = os.path.join(SRC, "dist", PKGNAME)
TOOL = os.path.join(OUT, "tool")

HEADER = r"""@echo off
chcp 936 >nul
setlocal EnableExtensions EnableDelayedExpansion
title %TITLE%
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
set "TOOL=%HERE%\tool"
set "OUT=%TOOL%\afpatch.out"
set "BKROOT=%ProgramData%\ArcMapFontFix\backup"
set /a RUNNING=0
set "AUTO="
if /i "%~1"=="/auto" set "AUTO=1"
echo ============================================================
echo   ArcMap 字体列表 / 标注页签 卡顿补丁  v1.0
echo   %SUBTITLE%
echo ============================================================
echo.

net session >nul 2>&1
if errorlevel 1 (
  echo 需要管理员权限，正在请求提升，请在弹窗中点"是"……
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList '%~1' -Verb RunAs" >nul 2>&1
  exit /b
)

if not exist "%TOOL%\afpatch.exe"       goto :nofile
if not exist "%TOOL%\afFontShim.dll"    goto :nofile
if not exist "%TOOL%\afFontShim.ini"    goto :nofile

call :killcheck
if errorlevel 1 goto :end
"""

TAIL = r"""
goto :end

:nofile
echo [x] 安装包不完整：tool 目录下缺少 afpatch.exe / afFontShim.dll / afFontShim.ini
echo     请把压缩包完整解压后再运行（不要在压缩包里直接双击）。
goto :end

:noversion
echo [x] 没有在本机找到 ArcGIS Desktop 10.x 的安装。
echo     如果装在非默认位置，可手工指定 bin 目录：
echo         afpatch.exe patch "D:\某目录\ArcGIS\Desktop10.2\bin"
goto :end

:killcheck
for %%P in (ArcMap.exe ArcCatalog.exe ArcScene.exe ArcGlobe.exe ArcReader.exe) do (
  tasklist /FI "IMAGENAME eq %%P" 2>nul | find /i "%%P" >nul
  if not errorlevel 1 (
    echo [x] 检测到 %%P 正在运行。
    set /a RUNNING=1
  )
)
if %RUNNING%==1 (
  echo.
  echo     请先保存并关闭所有 ArcGIS 程序，再重新运行本程序。
  exit /b 1
)
exit /b 0

:end
echo.
if not defined AUTO if not defined NOPAUSE pause
"""

INSTALL_BODY = r"""
set /a ANY=0
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" discover
if not exist "%OUT%" goto :noversion

for /f "usebackq tokens=1,2,3 delims=|" %%A in ("%OUT%") do (
  set /a ANY=1
  call :dover "%%A" "%%B" "%%C"
)

if %ANY%==0 goto :noversion

echo.
echo ============================================================
echo  处理完毕。
echo  请打开 ArcMap，右键图层 - 属性 - 点"标注"页签体验。
echo  若字体列表出现异常，运行 03-回退还原.bat 完全恢复。
echo ============================================================
goto :end

:dover
set "VK=%~1"
set "BIN=%~2"
set "VER=%~3"
echo.
echo ------------------------------------------------------------
echo   版本: %VK%    %VER%
echo   目录: %BIN%

del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" verify "%BIN%" "%TOOL%\afFontShim.dll"
if errorlevel 1 (
  echo   [跳过] 该版本用到的 gdi32 函数超出本补丁支持范围，未做任何改动。
  type "%OUT%" 2>nul
  exit /b 0
)

del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" check "%BIN%"
if errorlevel 3 (
  echo   [已安装] 该版本之前已经打过补丁，跳过。
  exit /b 0
)
if errorlevel 1 (
  echo   [跳过] 无法识别该目录下的 afUILib.dll。
  exit /b 0
)

echo   [1/3] 备份原始文件 …
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" backup "%BIN%" "%BKROOT%"
if errorlevel 1 (
  echo   [失败] 备份没有成功，已中止，不会改动任何文件。
  type "%OUT%" 2>nul
  exit /b 0
)
set "BAK="
for /f "usebackq tokens=1,* delims= " %%X in ("%OUT%") do if /i "%%X"=="OK" set "BAK=%%Y"
if not defined BAK (
  echo   [失败] 备份路径读取异常，已中止。
  exit /b 0
)
echo         备份到 !BAK!

echo   [2/3] 安装字体枚举组件 …
copy /y "%TOOL%\afFontShim.dll" "%BIN%\" >nul
if errorlevel 1 (
  echo   [失败] 复制 afFontShim.dll 失败（文件被占用？）
  exit /b 0
)
copy /y "%TOOL%\afFontShim.ini" "%BIN%\" >nul

echo   [3/3] 改写 afUILib.dll 导入表 …
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" patch "%BIN%"
if errorlevel 1 (
  echo   [失败] 打补丁失败，原文件未被修改。
  type "%OUT%" 2>nul
  exit /b 0
)
echo   [OK] 该版本安装完成。
exit /b 0
"""

BACKUP_BODY = r"""
set /a ANY=0
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" discover
if not exist "%OUT%" goto :noversion

for /f "usebackq tokens=1,2,3 delims=|" %%A in ("%OUT%") do (
  set /a ANY=1
  call :dobak "%%A" "%%B" "%%C"
)
if %ANY%==0 goto :noversion
echo.
echo ============================================================
echo  备份完毕，可以直接把备份目录整个拷走保存。
echo ============================================================
goto :end

:dobak
set "VK=%~1"
set "BIN=%~2"
set "VER=%~3"
echo.
echo ------------------------------------------------------------
echo   版本: %VK%    %VER%
echo   目录: %BIN%

del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" check "%BIN%"
if errorlevel 3 (
  echo   [跳过] 该版本已打过补丁，当前 afUILib.dll 不是原始文件。
  echo          如需原始备份，请先运行 03-回退还原.bat。
  exit /b 0
)

del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" backup "%BIN%" "%BKROOT%"
if errorlevel 1 (
  echo   [失败] 备份失败：
  type "%OUT%" 2>nul
  exit /b 0
)
set "BAK="
for /f "usebackq tokens=1,* delims= " %%X in ("%OUT%") do if /i "%%X"=="OK" set "BAK=%%Y"
echo   [OK] 已备份到 !BAK!
exit /b 0
"""

ROLLBACK_BODY = r"""
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" list "%BKROOT%"
if not exist "%OUT%" goto :nobak
findstr /b /c:"NONE" "%OUT%" >nul && goto :nobak

echo 检测到以下备份（每个 ArcGIS 版本取最新一份）：
echo.
for /f "usebackq tokens=1,2,3 delims=|" %%A in ("%OUT%") do (
  echo   [%%A] %%B
  echo        目标: %%C
)
echo.
set "SEL="
if not defined AUTO set /p "SEL=输入序号只回退某一个，直接回车 = 全部回退: "
echo.
if not defined SEL goto :all

set /a HIT=0
for /f "usebackq tokens=1,2 delims=|" %%A in ("%OUT%") do (
  if "%%A"=="!SEL!" (
    set /a HIT=1
    call :dorest "%%B"
  )
)
if %HIT%==0 (
  echo [x] 没有序号 !SEL! 对应的备份。
)
goto :done

:all
for /f "usebackq tokens=1,2 delims=|" %%A in ("%OUT%") do call :dorest "%%B"

:done
echo.
echo ============================================================
echo  回退完成。可以打开 ArcMap 确认。
echo  补丁组件（afFontShim.dll / .ini / .log）已从 bin 目录删除。
echo ============================================================
goto :end

:dorest
echo  回退 %~1 …
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" restore "%BKROOT%\%~1"
if errorlevel 1 (
  echo    [失败]
  type "%OUT%" 2>nul
) else (
  echo    [OK]
)
exit /b 0

:nobak
echo [i] %BKROOT% 下没有找到任何备份，可能从没装过补丁。
goto :end
"""

STATUS_BODY = r"""
set /a ANY=0
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" discover
if not exist "%OUT%" goto :noversion
for /f "usebackq tokens=1,2,3 delims=|" %%A in ("%OUT%") do (
  set /a ANY=1
  call :showstate "%%A" "%%B" "%%C"
)
if %ANY%==0 goto :noversion
echo.
echo 备份目录: %BKROOT%
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" list "%BKROOT%"
if exist "%OUT%" (
  findstr /b /c:"NONE" "%OUT%" >nul || (
    echo.
    for /f "usebackq tokens=1,2,3 delims=|" %%A in ("%OUT%") do echo   [%%A] %%B
  )
)
goto :end

:showstate
set "VK=%~1"
set "BIN=%~2"
set "VER=%~3"
del "%OUT%" 2>nul
"%TOOL%\afpatch.exe" check "%BIN%"
if errorlevel 3 (
  echo   %VK%  %VER%
  echo      状态: 已安装补丁        %BIN%
  exit /b 0
)
echo   %VK%  %VER%
echo      状态: 未安装（原始文件）  %BIN%
exit /b 0
"""

README = """ArcMap 字体列表 / 标注页签 卡顿补丁  v1.0
================================================

适用对象
--------
ArcGIS Desktop 10.2.x（ArcMap）在以下对话框里打开极慢、要等十几秒的问题：
  · 图层属性 → 标注 页签
  · 标注标题下的「符号」按钮、符号选择器里的「编辑符号」
  · 其它带字体下拉框的对话框
机器上装的字体越多越慢（本机 1111 个字体时，一次约 11 秒）。

原因（已定位到具体代码）
------------------------
afUILib.dll 的字体下拉框初始化时，每个字体都要向组合框发消息、逐条取回已有
条目做比对去重，是 O(n^2)。3087 个字体项累计约 160 万次窗口消息，
这就是十几秒停顿的来源。Esri 官方缺陷编号 BUG-000113336，10.7 才修复，
10.2.2 从来没有补丁。

本补丁做了什么
--------------
1. 用一个内存 DC 把字体列表枚举一次并缓存，后续直接复用（原逻辑每次都重来）；
2. 字体名去重放到枚举层，回调次数 3087 → 779；
3. 回填列表时跳过那段纯做无用功的逐条比对循环。

效果（本机实测，同一份 mxd）
  修复前：11.4 秒
  修复后：0.27 ~ 0.36 秒
字体列表内容与原逻辑完全一致（条目数相同，没有少也没有重复）。
不改动任何业务逻辑，出图、符号、标注内容都不受影响。

怎么用
------
1. 把整个文件夹解压到本机任意位置（不要在压缩包里直接双击）；
2. 保存并关闭所有 ArcGIS 程序（ArcMap / ArcCatalog / ArcScene / ArcGlobe）；
3. 双击  01-安装_自动备份.bat ，在 UAC 弹窗点"是"；
   它会自动：找到本机所有 ArcGIS 10.x → 备份原始 afUILib.dll → 安装 → 校验。
4. 打开 ArcMap 验证。

备份在哪
--------
  %ProgramData%\\ArcMapFontFix\\backup\\<计算机名>_<时间>_<版本>\\
里面有 info.txt（记录了原文件路径、大小、版本、时间）和 afUILib.dll.orig。

出问题怎么办
------------
双击  03-回退还原.bat ，选序号或直接回车全部回退，1 秒钟恢复原状。
也可以手工把备份目录里的 afUILib.dll.orig 改名回 afUILib.dll 覆盖回去。

其它说明
--------
· 只想先备份、不安装：双击 02-仅备份原文件.bat
· 想知道当前装没装、备份在哪：双击 04-查看状态.bat
· 静默运行（不询问、不暂停，适合批量部署）：给 bat 加参数 /auto，
  例如  01-安装_自动备份.bat /auto
· 重装 ArcGIS、或运行了修复安装，会把 afUILib.dll 还原成原版，补丁随之失效，
  重新运行一次 01-安装_自动备份.bat 即可。
· 补丁是"就地改写一个导入表项 + 放两个文件到 bin 目录"，不写注册表、不装服务、
  不常驻进程；卸载 = 回退，或者删掉 bin 下的 afFontShim.* 再还原 afUILib.dll。
· 安装前会先校验目标 afUILib.dll 需要的 gdi32 函数是否都在补丁组件里，
  不在就跳过该版本、绝不动文件，因此换到别的机器上是安全的。
· 补丁组件在同目录的 tool 文件夹，可单独使用：
      afpatch.exe discover                 列出本机 ArcGIS 版本与 bin 目录
      afpatch.exe check "<bin目录>"        判断是否已打补丁（返回码 0=原始 3=已打）
      afpatch.exe patch "<bin目录>"        打补丁（会自行校验）
      afpatch.exe restore "<备份目录>"      用指定备份还原
"""


def w(path, text, crlf=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if crlf:
        text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    with open(path, "wb") as f:
        f.write(text.encode("gbk", errors="replace"))


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(TOOL, exist_ok=True)

    for junk in ("afpatch.out",):
        jp = os.path.join(TOOL, junk)
        if os.path.exists(jp):
            os.remove(jp)

    for fn in ("afpatch.exe", "afFontShim.dll", "afFontShim.ini"):
        shutil.copy2(os.path.join(SRC, fn), os.path.join(TOOL, fn))

    def bat(name, title, subtitle, body):
        head = HEADER.replace("%TITLE%", title).replace("%SUBTITLE%", subtitle)
        w(os.path.join(OUT, name), head + body + TAIL)

    bat("01-安装_自动备份.bat", "ArcMap 卡顿补丁 - 安装", "先备份，再安装；随时可回退", INSTALL_BODY)
    bat("02-仅备份原文件.bat", "ArcMap 卡顿补丁 - 仅备份", "只备份原始文件，不做任何修改", BACKUP_BODY)
    bat("04-查看状态.bat", "ArcMap 卡顿补丁 - 查看状态", "查看各版本是否已安装补丁", STATUS_BODY)
    bat("03-回退还原.bat", "ArcMap 卡顿补丁 - 回退", "还原成原始文件，并清理补丁组件", ROLLBACK_BODY)

    w(os.path.join(OUT, "使用说明.txt"), README)
    print("已生成:", OUT)
    for root, _, files in os.walk(OUT):
        for f in files:
            p = os.path.join(root, f)
            print("   %-46s %8d" % (os.path.relpath(p, OUT), os.path.getsize(p)))


if __name__ == "__main__":
    main()
