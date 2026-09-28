# 自訂搜尋外掛

PCMan 會載入本資料夾中副檔名為 `.xml` 的搜尋外掛。若要新增自訂搜尋，請將下方模板複製成新的 `.xml` 檔案，例如 `my-search.xml`，修改名稱與搜尋網址後重新啟動 PCMan。

請將 XML 檔案儲存為 UTF-8，並保留 `<InputEncoding>UTF-8</InputEncoding>`。目前只支援以 HTTP `GET` 方法開啟搜尋結果，`{searchTerms}` 會被替換成使用者選取並經 UTF-8 URL 編碼的文字。

```xml
<SearchPlugin xmlns="http://www.mozilla.org/2006/browser/search/">
<ShortName>清單名稱</ShortName>
<Description>自訂搜尋網站</Description>
<InputEncoding>UTF-8</InputEncoding>
<Url type="text/html" method="GET" template="https://example.com/search">
  <Param name="q" value="{searchTerms}"/>
</Url>
</SearchPlugin>
```

請依搜尋網站的規格修改：

- `ShortName`：顯示在右鍵搜尋選單中的名稱。
- `Description`：外掛的說明文字。
- `template`：搜尋網站的網址。
- `Param name`：搜尋網站接收關鍵字的參數名稱，常見值為 `q` 或 `query`。

`InputEncoding` 是必要欄位。請使用不含前後空白的 `UTF-8`；不保證其他輸入編碼能正確運作。
