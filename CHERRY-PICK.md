# What to cherry-pick

This is a quick guide to which commits help Raven in general and which only exist to get PDF-XChange (PXC) running. A ✅ in the Test column means the commit adds or extends a regression test. The other commits were checked only by running PXC. All of the code is AI-generated (see the [README](README.md)), so please review before taking anything.

| Grade | Meaning |
|---|---|
| 🟢 **Pick first** | Raven behaved differently from Windows in a way that affects any GUI program. Small, self-contained, and mostly tested. |
| 🟡 **Worth a look** | Generally useful, but bigger, a design choice you may want to make differently, or untested. |
| ⚪ **API gap fillers** | Small additions of missing Windows APIs. They are low risk, and worth having when another program needs them. |
| 🔴 **Skip** | PXC-specific, personal, or debugging aids. |

## 🟢 Pick first: general Windows behaviour

| Commit | What it fixes | Test |
|---|---|---|
| `7034474` | WM_MOVE and WM_SIZE come from DefWindowProc(WM_WINDOWPOSCHANGED), as on Windows. Programs had been laying out with a stale size. | |
| `80fe3fa` | The update region is clipped to the client area, so rcPaint is no longer wider than the window after a resize. | ✅ |
| `60d3d7c` | Siblings beneath a child that hides, moves or is destroyed repaint the uncovered area. | ✅ |
| `eb72e1b` | Host input goes to the thread that owns the window. This fixes a cross-thread WM_NCHITTEST deadlock. | ✅ |
| `61312eb` | Owned dialogs keep the keyboard and hand focus back to their owner when they close. | ✅ |
| `7535965` | Ctrl+Alt+key arrives as WM_KEYDOWN, not WM_SYSKEYDOWN, as on Windows. | ✅ |
| `7bd3acc` | VK_L/R SHIFT, CONTROL and MENU keep their own state. Apps that check VK_RMENU for AltGr had been dropping Ctrl+Alt shortcuts. | ✅ |
| `9ab09f8` | The mouse wheel goes to the window under the pointer. | |
| `7cccfca` | Maximized windows don't offer sizing borders. | ✅ |
| `9e72494` | Non-client mouse messages (WM_NC*) reach the program. | |
| `c1fef69` | CheckTokenMembership answers from the token's interactive logon groups. PXC never saved its settings without this. | ✅ |
| `0c3d053` | Every writable view of a file section shares the same pages. | |
| `1dd14f2` | StretchDIBits honours the DC's mapping mode and world transform. | |
| `8e98a01` | Enumerated italic faces report lfItalic and tmItalic as 255, as Windows does. | |
| `42c301e` | SetLayeredWindowAttributes alpha and colour key are applied when the window is shown. | ✅ |
| `c31e922` | GetTempFileName works with an empty prefix. | |

## 🟡 Worth a look: bigger or opinionated

| Commit | What it adds | Test | Note |
|---|---|---|---|
| `daa4306` `ae62aae` `6438243` `8adea6f` `03210ef` | Follows the compositor's maximize and restore, including KWin's size-less restore when a maximized window is dragged by its title bar. A frame press already released no longer starts a move or resize. | ✅ (last) | Best taken as a set. |
| `40d7163` `82e4be4` | Wayland: owned windows stay above their owners, and unowned captionless tool popups show on the active window where the program placed them. Without this, KWin cascaded PXC's Alt key tips and splitter bar. | ✅ (2nd) | `82e4be4` is a heuristic. |
| `e46732f` | Drop-down shadows sit beside their list, and clicks reach popups. | | |
| `ce6ccf8` | Uniscribe and text measuring are served without stopping the guest: 3.1 s → 1.5 s of CPU on a heavy layout. | ✅ | perf |
| `cd2f38a` | FlsSetValue is stored in the guest without a stop. This was 60% of all stops while PXC scrolled. | | perf |
| `c2e5ed5` | Wayland frame changes are found by comparing rows directly instead of hashing them. | | perf |
| `d740d43` | The desktop's own Open/Save dialogs, through the XDG portal FileChooser. | ✅ | New feature |
| `0d79045` `02cbf63` `3955926` `614b7a2` | Printing through CUPS: printer DCs, PageSetupDlgW, text under mapping modes. | ✅ (1st) | Large (~1,900 lines) |
| `49bb75e` `4b354ca` | OLE drag and drop inside a program, and Wayland drag and drop to and from the desktop. | | Large (~1,200 lines) |
| `893146d` | SHILCreateFromPath and SHOpenFolderAndSelectItems (opens the desktop file manager), and xdg-open processes are reaped. | | |
| `f436954` | Whole font files handed out (GetFontData), with glyphs named by the face's own indices. | | |
| `9c6993c` `887c803` `238b9f0` | Crash reports say which mapping or bitmap a faulting address belongs to. | | Debugging |
| `53374dd` | System libraries and exports PXC's core imports (PR #140, by Raymond Bennett). | ✅ | Already a PR upstream |

## ⚪ API gap fillers

These are small, mostly untested additions, worth having when another program needs them:

`4287b19` GetSystemTimeAdjustment · `7cfa4fc` GetDllDirectory · `d2c85e8` GetColorDirectory · `56a2b56` GetCharacterPlacement, GetFontLanguageInfo, ScriptGetLogicalWidths · `9743ec3` OleRun · `fbae34d` MapVirtualKeyEx · `a32b6f6` StrRStrI · `7816aed` GetRandomRgn · `a02512e` GetGUIThreadInfo · `9a53c9b` GetScaleFactorForMonitor · `964d523` CryptSetProvParam · `7e15285` ScrollDC · `254554e` ValidateRgn · `266f512` resuming COM class objects · `6b8030b` InternetAttemptConnect · `b9b8f19` shlwapi string and path helpers · `833fe58` StrRetToBufW, OleLockRunning, OleSetContainedObject · `854fa85` title bar info, process layout, animated rects · `f8cc222` TransparentBlt, DC brush and pen colours · `97ae83b` printer calls on a system with no printer · `36bde07` LICENSES.json entries for the new shell files

## 🔴 Skip: PXC-specific or personal

- `2569395` `07598b8` `478422a` `d5e2262`: a personal launcher, setup script and logging (`contrib/pdfx/`).
- `c5c7d08` `d28e511` `27f8513` `51a2287` `adc20ad` `1d49fae`: a headless PXC test harness and test PDFs (`scripts/native-pdfx.py`, `testdata/pdfx/`). This is useful only if you want PXC as a regression target.
- `aab41a2` `fd32d03` `0188d08`: extra trace output (`RAVEN_TRACE_DRAG` and others). `0188d08` also makes SC_MOVE work on a maximized window, which is general.
- The commits that add `PDFX-NOTES.md`, `README.md` and this file: they only describe this repository.
