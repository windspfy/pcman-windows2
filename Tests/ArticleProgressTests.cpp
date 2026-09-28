#include <afx.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../Lite/ArticleProgress.h"

static unsigned int checks = 0;
static int messages = 0;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static int TestMessageBox(const char*, unsigned int) { ++messages; return 0; }
#define AfxMessageBox TestMessageBox
class CTelnetConn;
struct CDownloadArticleDlg
{
    explicit CDownloadArticleDlg(CTelnetConn*) {}
    int DoModal() { return 0; }
};
static CDownloadArticleDlg* download_article_dlg = nullptr;
class CTelnetConn
{
public:
    struct Settings { int line_count = 4; int cols_per_page = 80; } site_settings;
    struct KeyMap { const char* FindKey(int, int) { return nullptr; } } keys;
    KeyMap* key_map = &keys;
    char lines[4][81] = {};
    char* rows[4] = { lines[0], lines[1], lines[2], lines[3] };
    char** screen = rows;
    int last_line = 3, first_line = 0, scroll_pos = 0;
    int current_download_line = 0;
    bool is_getting_article = false, get_article_with_ansi = false, get_article_in_editor = false;
    CString downloaded_article;
    CString clipboard = "unchanged";
    int sends = 0, completes = 0;
    ~CTelnetConn() { delete download_article_dlg; download_article_dlg = nullptr; }
    void SendString(const char*) { ++sends; }
    CString GetLineWithAnsi(int y) { return CString("ANSI:") + screen[y]; }
    void CopyArticleComplete(bool = false)
    {
        ++completes; is_getting_article = false; clipboard = downloaded_article;
    }
    int IsEndOfArticleReached();
    ArticleProgress::Progress GetArticleProgress();
    void CopyArticle(bool, bool);
    void ContinueCopyArticle();
    void Footer(const std::string& s)
    {
        std::memset(lines[3], ' ', 80);
        std::memcpy(lines[3], s.data(), (s.size() < 80 ? s.size() : 80));
    }
};
#include "ProductionArticleMethods.inc"

static std::string Footer(int percent, int first, int last)
{
    return "page (" + std::to_string(percent) + "%) range " + std::to_string(first) +
        "~" + std::to_string(last) + " \xa6\xe6 (X/%) help";
}
int main()
{
    using namespace ArticleProgress;
    const std::string valid = Footer(50, 1, 3);
    for (const char* bad : { "", "    ", "100%", "(101%) 1~3 \xa6\xe6", "(-1%) 1~3 \xa6\xe6",
        "(50%) 0~3 \xa6\xe6", "(50%) 4~3 \xa6\xe6", "(50%) 1~ \xa6\xe6",
        "(50%) 1~3", "(50%) 1~3 ", "(50%) 1~3 \xa6", "(50%) 1~3x \xa6\xe6",
        "(50%) 1~999999999999999999999 \xa6\xe6", "(999999999999999%) 1~3 \xa6\xe6",
        "(50%) 1~80 cols, 1~3 \xa6\xe6", "(50%) no range", "(50%) (100%) 1~3 \xa6\xe6" })
        Check(Parse(bad, std::strlen(bad)).state == Unavailable, "reject invalid/incomplete footer");
    Check(Parse(nullptr, 80).state == Unavailable, "null safe");
    Check(Parse(valid.data(), 0).state == Unavailable, "zero width");
    Check(Parse(valid.data(), valid.size()).state == More, "valid non-NUL input");
    const std::string hints = valid + " ~ 999% \xa4\x7e";
    Check(Parse(hints.data(), hints.size()).state == More, "right-hand hints ignored");
    const std::string utf8 = "page (100%) range 1~3 \xe8\xa1\x8c";
    Check(Parse(utf8.data(), utf8.size()).state == Complete, "UTF8 footer");
    const std::string spaced = "page (  0%) range 01 ~ 03 \xa6\xe6";
    Check(Parse(spaced.data(), spaced.size()).state == More, "padded fields and zero percent");
    for (int p = 0; p <= 100; ++p)
    {
        const auto s = Footer(p, 1, 3);
        Check(Parse(s.data(), s.size()).state == (p == 100 ? Complete : More), "only 100 completes");
    }
    // Put the last supplied byte directly before an inaccessible memory page.
    SYSTEM_INFO info; GetSystemInfo(&info);
    char* memory = static_cast<char*>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(memory != nullptr, "guard allocation");
    DWORD old = 0;
    Check(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old) != 0,
        "guard protection");
    for (std::size_t n = 0; n <= valid.size(); ++n)
    {
        char* input = memory + info.dwPageSize - n;
        std::memcpy(input, valid.data(), n);
        const auto result = Parse(input, n);
        Check(result.state == Unavailable || result.state == More, "bounded prefixes");
    }
    for (int n = 1; n <= 160; ++n)
    {
        char* input = memory + info.dwPageSize - n;
        std::memset(input, ' ', n);
        Check(Parse(input, n).state == Unavailable, "nonterminated blank row bounded");
        std::memset(input, '9', n);
        Check(Parse(input, n).state == Unavailable, "nonterminated digits bounded");
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    Check(NewLines(Parse(valid.data(), valid.size()), 3, 3) == 0, "duplicate progress");
    for (int delta : { -1, 0, 1, 2, 3, 100 })
    {
        const auto s = Footer(60, 1 + delta, 3 + delta);
        const auto p = Parse(s.data(), s.size());
        Check(NewLines(p, 3, 3) == (delta == 1 || delta == 2 ? delta : 0), "bounded advance");
    }
    {
        CTelnetConn c;
        c.Footer("no footer"); c.CopyArticle(false, false);
        Check(c.sends == 0 && c.completes == 0 && c.clipboard == "unchanged" && messages == 1,
            "invalid start leaves clipboard and network alone");
        Check(c.IsEndOfArticleReached() == Unavailable, "unknown is distinct from complete");
        c.last_line = -1;
        Check(c.GetArticleProgress().state == Unavailable, "negative row safe");
        c.last_line = 4;
        Check(c.GetArticleProgress().state == Unavailable, "row limit safe");
    }
    for (bool ansi : { false, true })
    {
        CTelnetConn c;
        std::strcpy(c.lines[0], "one"); std::strcpy(c.lines[1], "two"); std::strcpy(c.lines[2], "three");
        c.Footer(valid); c.CopyArticle(ansi, false);
        Check(c.sends == 1 && c.is_getting_article && download_article_dlg, "start requests once and allows cancel");
        c.ContinueCopyArticle();
        Check(c.sends == 1, "same footer does not request again");
        c.Footer(Footer(90, 8, 10)); c.ContinueCopyArticle();
        Check(c.sends == 1 && c.current_download_line == 3 && c.completes == 0, "unexpected jump waits");
        c.Footer("(100%)"); c.ContinueCopyArticle();
        Check(c.sends == 1 && c.completes == 0, "missing range cannot complete");
        const auto next = Footer(75, 2, 4);
        const std::size_t unitEnd = next.find("\xa6\xe6") + 2;
        for (std::size_t n = 0; n < unitEnd; ++n)
        {
            c.Footer(next.substr(0, n)); c.ContinueCopyArticle();
            Check(c.sends == 1 && c.current_download_line == 3 && c.completes == 0, "split footer waits");
        }
        std::strcpy(c.lines[2], "four"); c.Footer(next); c.ContinueCopyArticle();
        Check(c.sends == 2 && c.current_download_line == 4, "append one line");
        c.ContinueCopyArticle(); Check(c.sends == 2, "no duplicate request");
        c.Footer(Footer(100, 2, 4)); c.ContinueCopyArticle();
        Check(c.completes == 0, "new percent with stale range cannot complete");
        std::strcpy(c.lines[1], "five"); std::strcpy(c.lines[2], "six");
        c.Footer(Footer(100, 4, 6)); c.ContinueCopyArticle();
        Check(c.completes == 1 && c.sends == 2 && !c.is_getting_article, "two lines and complete without extra Down");
        const CString expected = ansi ? "ANSI:one\r\nANSI:two\r\nANSI:three\r\nANSI:four\r\nANSI:five\r\nANSI:six\r\n" :
            "one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\n";
        Check(c.clipboard == expected, "copy content without duplicate or lost rows");
    }
    {
        CTelnetConn c; c.Footer(Footer(100, 1, 3)); c.CopyArticle(false, false);
        Check(c.completes == 1 && c.sends == 0, "single page needs no Down");
    }
    std::printf("PASS: %u article checks (bounded parser and real copy methods)\n", checks);
}
