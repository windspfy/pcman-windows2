# CSI 解析測試

本次僅補強 7-bit `ESC [`（CSI）解析，保留現有基本指令處理。
DEC2026、SGR Mouse 仍忽略；CPR 仍由既有空函式處理，不回覆。
這不是完整的 ECMA-48 終端機實作，OSC／DCS 字串協定未納入本次範圍。
不將 Big5／UAO／UTF-8 中的高位元組當成 8-bit C1 控制碼。

## 自動測試

使用 Visual Studio Developer PowerShell，在專案根目錄執行：

```powershell
MSBuild Tests/AnsiSequenceParserTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/AnsiSequenceParserTests.exe
MSBuild Tests/AnsiSequenceParserTests.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
./Tests/bin/Release/AnsiSequenceParserTests.exe
```

測試直接包含正式程式使用的 `Lite/AnsiSequenceParser.h`，不是另外模擬一份解析器。
檢查既有指令分派、所有私有前綴與 final byte 組合、中間字元、未知指令、
63-byte 容量界線、數字超出 32767、ESC 重啟、CAN／SUB 取消、內嵌 C0、
中文編碼位元組、每個可能的兩段切割位置及逐位元組輸入，以及不同連線的狀態隔離。
過長、超出數字上限或不支援的序列會整段忽略，不執行截斷後的內容。

此測試驗證解析結果，**不等於驗證 MFC 畫面繪製或實際 Telnet／WSS 連線**。
完整程式另以 `PCMan.sln` 的 Debug／Release Win32 組態編譯。

## 人工離線測試

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/New-CsiVisualFixture.ps1
```

1. 開啟剛編譯的 PCMan。
2. 使用「ANSI編輯 → 開啟舊檔」，開啟 `Tests/fixtures/CsiCompatibility.ans`。
3. 第 3 行 `RED BEFORE` 與 `RED AFTER` 應同樣為紅色。
4. 第 4 行的綠色 LEFT／RIGHT 應在同一行，第 5 行青色亦同。
5. DEC2026、MOUSE、LONG CSI、CPR 各行不應出現多餘控制碼文字。
6. RESTART 行應顯示綠色 `GREEN OK`，最後可見 `END`。

其中第 3 行可重現舊版把 `ESC[>4;2m` 誤當 SGR 重設顏色的問題；
第 4／5 行檢查未知指令不能誤移游標。畫面測試不驗證網路分段，
分段處理由上述自動測試涵蓋。不要將測試檔貼上／傳送至 BBS。

## 人工連線回歸測試

Debug 與 Release 分別登入 PTT／PTT2（不要同時執行兩個版本）：

- 登入前後無控制碼殘字、閃退或停滯。
- 看板清單、文章、上下頁切換及返回選單的顏色、游標位置正常。
- 中文／日文字顯示與選取複製沒有新增異常。
- 既有滾輪與右鍵功能保持原狀。

正常瀏覽預期與原先相同；不能只因畫面正常，就宣稱所有未知指令均已相容。
整篇文章複製的既有底列邊界問題不在這次修改範圍內。
