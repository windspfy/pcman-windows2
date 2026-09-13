PCMan
---
這是 PCMan BBS 用戶端的程式碼。PCMan Combo 已停止維護並自主要開發分支移除；
最後版本封存於 Git tag `combo-final`。

除 Lite 目錄下的 Rijndael.cpp 和 Rijndael.h 為 George Anescu 撰寫，
不是使用 GPL 授權以外，其餘程式碼皆是使用 GPL 授權。

因為授權問題，本專案無法包含圖形檔，各種圖示暫時以單色的 bmp 取代，
請下載舊版 PCMan 的 zip 檔，從中取出各 bmp 圖檔，並放到正確位置。

首次釋出時間 2007.01.01

Getting started
---
   * [PCMan程式專案建置和執行：逐步解說](../../wiki/Building_PCMan)

使用 Visual Studio 2026 建置
---

請安裝「使用 C++ 的桌面開發」工作負載，並包含下列元件：

* MSVC v143 x86/x64 建置工具
* 適用於 x86 的 MFC
* Windows 11 SDK 
* vcpkg

開啟 `PCMan.sln` 後可直接建置 `Debug|Win32` 或 `Release|Win32`。專案已啟用
vcpkg manifest，首次建置會自動下載及編譯相依套件。

也可以在 Developer PowerShell for Visual Studio 中執行：

```powershell
msbuild PCMan.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

`cpprestsdk` 已停止維護，且已從新版 vcpkg ports 中移除。本專案透過
`vcpkg-overlay-ports/cpprestsdk` 保留 WebSocket 功能並加入新版 MSVC 所需的相容修補；
這是維持既有程式可建置的封存方案，若要長期維護，仍應規劃替換該程式庫。
