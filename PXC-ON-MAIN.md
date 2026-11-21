# PDF-XChange Editor on current main

In [#150](https://github.com/mirkobrombin/raven/issues/150) you asked: *"Could you try PDF-XChange again on current main and tell us what still breaks?"* This is the answer, tested on main at
`e9e45f2d` (2026-11-20). PDF-XChange Editor (PXC) is a large Windows PDF editor; it is the program the earlier fixes came from.

On main alone, PXC doesn't start: it stops before showing a window, at `raven: unimplemented native API GetDllDirectoryW`, the
first of the missing calls below. With the 20 fixes below, it starts confined
in the new sandbox and works through a normal editing session:

- opening and rendering, search, page thumbnails
- rotating pages, moving pages by dragging their thumbnails, editing page text, the Typewriter tool, sticky notes, bookmarks, undo
- the JavaScript console, Save As, the clipboard, the save prompt when closing a changed document
- Alt key tips, maximize, minimize and restore
- settings and recent files kept across a restart, and a clean exit

Each fix is its own branch with one commit on `main`, and each brings its own test. To see one fix's change, compare it with main:
`https://github.com/abemate/raven/compare/e9e45f2d...fix/<branch>`, for example
[fix/dll-directory](https://github.com/abemate/raven/compare/e9e45f2d...fix/dll-directory).

All of the code is AI-generated (Claude Code), and I'm not a programmer, so please review before taking anything. As last time, the
commits are offered as problem descriptions you can also take directly.

## How each fix was checked

- Every branch is one commit on `e9e45f2d`, with a test run as 32 and 64 bit (`testdata/native_loader/*_client.c`, or the existing
  `wayland_presentation_test` and `confine_test`).
- Each test **fails on main** and **passes with its fix**, three runs each.
- Each test's CMake entry is anchored at a different existing line of `native/CMakeLists.txt`, so the branches can be taken in any
  combination without conflicts. All 20 merged together build and pass.
- Every branch passes the repository's own `check_commits.py`, `check_files.py` and `check_licenses.py`.
- The full `ctest` suite was run on main and on all 20 merged (`ctest -j2`, same machine, no GPU). Both have 71 failures, the same
  tests apart from two that time out now and then: `native_input_thread` (failed on main only) and `native_cmd_x32` (failed with the
  fixes only). `native_cmd_x32` hits its 60 s `--timeout-ms` on main too: in five runs each it failed three times on main and twice
  with the fixes. The rest are the GPU tests (D3D, Vulkan, OpenGL) and tests that also fail on main without any change here.

## Fixes that stop PXC

Without these, PXC ends at a missing call (or, with `RAVEN_CONTINUE_UNIMPLEMENTED`, gets a wrong answer and carries on).

| Branch | What PXC hit | Test |
|---|---|---|
| `fix/confined-fonts` | The confined runtime couldn't read `RAVEN_FONT_DIRECTORY`. The README builds with `-DRAVEN_FONT_DIRECTORY=$PWD/components/text/fonts`, a folder in the source tree that `confine.cpp` doesn't allow, so on a build made by the README's steps every program that draws text stops with "cannot load qualified native font file". Packaged installs keep their fonts beside the runtime, which is allowed. | `confine_test` reads a font there |
| `fix/system-time-adjustment` | `GetSystemTimeAdjustment` at startup. | ✅ |
| `fix/dll-directory` | `GetDllDirectoryW/A` at startup. | ✅ |
| `fix/color-directory` | `GetColorDirectoryW/A` at startup (colour profiles). | ✅ |
| `fix/text-placement` | `GetCharacterPlacement`, `GetFontLanguageInfo`, `ScriptGetLogicalWidths` while laying out text. Glyphs are main's real glyph ids. | ✅ |
| `fix/reverse-search` | `StrRStrIW/A`. | ✅ |
| `fix/random-region` | `GetRandomRgn` while painting. | ✅ |
| `fix/gui-thread-info` | `GetGUIThreadInfo` (caret and focus). | ✅ |
| `fix/monitor-scale` | `GetScaleFactorForMonitor` before sizing the toolbars. | ✅ |
| `fix/validate-region` | `ValidateRgn`, tens of thousands of times a session. | ✅ |
| `fix/ole-run` | `OleRun` (ole32, in `raven-shell`). | ✅ |
| `fix/scroll-dc` | `ScrollDC` whenever a page or the thumbnails scroll. | ✅ |
| `fix/crypt-provider-parameter` | `CryptSetProvParam` while saving. | ✅ |

## Fixes for wrong behaviour

| Branch | What went wrong in PXC | Test |
|---|---|---|
| `fix/font-file` | `GetFontData` refused table 0 (the whole font file). PXC embeds the font it types with, so the Typewriter tool made a box that took no text. | ✅ |
| `fix/active-stacking` | Activation never changed Raven's stacking order, which `WindowFromPoint` answers from. With two document windows overlapping, the one clicked into stayed "beneath", so PXC moved the focus to the other and ended a text edit. | ✅ |
| `fix/wayland-minimize` | Minimize did nothing on Wayland: `presentation.minimize` was implemented for X11 only. Restore comes back with the keyboard, as SC_RESTORE, the way X11 does it. | ✅ in `wayland_test.cpp` |
| `fix/app-id` | Every window has the app ID `raven.native`, which no menu entry matches, so KDE showed PXC with the generic Wayland icon. `RAVEN_APP_ID` lets a launcher name its program (Wayland app ID and X11 `WM_CLASS`). | ✅ in `wayland_test.cpp` |
| `fix/lasting-daylight-time` | `America/Vancouver` keeps UTC−7 for good from 2026. `GetTimeZoneInformation` described 2026 with no end to daylight time and said standard time was in force, so its bias disagreed with `GetLocalTime` by an hour. PXC stamped comments an hour off. The test uses a small zone file made with `zic`, so it doesn't depend on the host's zone data. | ✅ |
| `fix/drag-drop` | `DoDragDrop` cancelled at once and `RegisterDragDrop` kept no target, so dragging page thumbnails to move pages did nothing. The shell component now runs OLE drag and drop inside a process, and `WindowFromPoint` passes over disabled windows and layered windows transparent to the pointer (the drag picture is one). Dragging to and from other desktop programs isn't part of it. | ✅ |
| `fix/existing-directory` | `CreateDirectory` of an existing folder the program may only read gave `ERROR_ACCESS_DENIED` instead of `ERROR_ALREADY_EXISTS`. PXC creates each folder above the document it opens, and every one was logged in `sandbox.log` as a refused change. | ✅ |

## Proposals (no code)

These are design questions about the new sandbox, or features, so they're described rather than patched:

- **Opening a second document while PXC runs fails with "Access is denied".** This affects everyday use the most. PXC, like most
  Windows editors, runs once: a second start hands its file to the running copy and exits. The second process is granted its
  command-line file (`main.cpp`, `sandbox().grant`), but that grant lives only in its own memory, and the running PXC, which opens the
  file, is refused (`sandbox.log`: `open`). So double-clicking a second PDF in the file manager while PXC is open shows "Cannot open
  the document. Access is denied." A file picked in the Open dialog works, because the picker records an `app-read` grant in
  `sandbox.conf` that the running process re-reads. Recording command-line grants for the program in the same way (perhaps for the
  session only) would cover it. To reproduce: start PXC with a PDF, then start it again with another PDF in the same prefix.
- **A file chosen in the Open dialog is granted read-only** (`picker.c`: `write = state->save || FOS_PICKFOLDERS`). A file passed on
  the command line is granted for writing (`main.cpp`). For an editor this means: open a PDF through File > Open, change it, press
  Ctrl+S, and you get "Cannot save… Access is denied" (`sandbox.log`: `change`). Save As to a new name works. Windows programs commonly save over the file they opened, so
  an Open grant that allows writing the chosen file (or a way for the program to ask for it) would cover editors.
- **Printing:** PXC's print dialog shows "No printer installed" and nothing breaks. Our earlier branch had printing through CUPS
  ([`0d79045`](https://github.com/abemate/raven/commit/0d790453) and the commits after it on `pdf-xchange`) if a reference helps.
- **`SystemTimeToTzSpecificLocalTime` / `TzSpecificLocalTimeToSystemTime` with an explicit `TIME_ZONE_INFORMATION`** apply only
  `Bias` and ignore the daylight rules, so in summer the result is an hour off. Raven's own guest libraries only pass NULL, which is
  right. We saw nothing in PXC that depends on this.
- **`ValidateRgn`** could join `InvalidateRect`/`ValidateRect` in `desktop_beside_api` (`providers.cpp`), as of `c9aac455`. It's
  correct as it is.

## Not checked

- Dragging files in from the desktop's file manager, and out to it (no file manager in the headless test session).
- Ctrl+wheel zoom.
