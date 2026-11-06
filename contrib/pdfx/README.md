# PDF-XChange Editor on Raven (personal setup)

Scripts for running the portable PDF-XChange Editor 11 (x64) through a Raven built from the `pdfx-work` branch.

## Setup

1. Build Raven as in the top-level README, from this branch, with the provisioned components (controls, installer, text,
   graphics, roots, rtl), and build every target (`cmake --build build -j2`), not only `raven`: the component DLLs are staged
   by the other targets.
2. Unpack the portable edition into `~/.local/share/raven/pdfx-program` (the zip uses `\` separators; afterwards run
   `chmod -R u+rwX` on the folder).
3. Install the scripts and the menu entry:

   ```sh
   install -m755 pdfx pdfx-print ~/.local/bin/
   install -m644 pdfx.desktop ~/.local/share/applications/
   ```

`RAVEN`, `PDFX_DIR` and `RAVEN_PREFIX` override where the scripts look (see the top of `pdfx`).

## Use

- `pdfx [file.pdf ...]` starts the editor, opening the files given. Linux paths reach it as `Z:\...`.
- Printing: Raven has no printing yet, so PDF-XChange's Print dialog shows "No printer installed". Save the document and run
  `pdfx-print FILE`, which takes a Linux path or the `C:\...` path PDF-XChange shows, and opens Okular's print dialog (or
  sends it to the default printer with `lp` when Okular is not installed).

## Known gaps

- PDFs whose fonts are not embedded (plain Helvetica, Arial, Times) show no text yet; embedded fonts draw correctly.
- A Raven run ends after 24 hours, its longest timeout.
