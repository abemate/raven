# PDF-XChange Editor on Raven (personal setup)

Scripts for running the portable PDF-XChange Editor 11 (x64) through a Raven built from the `pdfx-work` branch.

## Setup

On the machine that runs it, from a checkout of this branch:

```sh
git switch --detach pdfx-work
contrib/pdfx/setup-host.sh            # or: contrib/pdfx/setup-host.sh path/to/PXCEditor11_Portable_x64.zip
```

It builds what is missing of the components (using `components-pdfx/` when it is there), builds Raven in `build-pdfx`
(about 10 GB), unpacks a fresh portable PDF-XChange into `~/.local/share/raven/pdfx-program`, and installs `pdfx`,
`pdfx-print` and the "PDF-XChange Editor (Raven)" menu entry. `JOBS` and `BUILD_DIR` change how and where it builds.

## Use

- `pdfx [file.pdf ...]` starts the editor, opening the files given. Linux paths reach it as `Z:\...`.
- Printing: Raven has no printing yet, so PDF-XChange's Print dialog shows "No printer installed". Save the document and run
  `pdfx-print FILE`, which takes a Linux path or the `C:\...` path PDF-XChange shows, and opens Okular's print dialog (or
  sends it to the default printer with `lp` when Okular is not installed).

## Known gaps

- PDFs whose fonts are not embedded (plain Helvetica, Arial, Times) show no text yet; embedded fonts draw correctly.
- A Raven run ends after 24 hours, its longest timeout.
