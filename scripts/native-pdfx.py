#!/usr/bin/env python3
"""Run the unmodified PDF-XChange Editor (portable, x64) through Raven on an isolated headless compositor.

Modelled on native-npp.py. The program directory is copied into the evidence directory, so every run starts from the same
pristine portable install and never touches the source copy. Each stage records window titles and screenshots.

Stages (each includes the earlier ones):
  window  the main window opens and draws something other than black
  open    a test PDF passed on the command line opens; its name reaches the title
  view    the document scrolls (PageDown) and zooms (ctrl+plus); the canvas changes each time

usage: native-pdfx.py RAVEN PDFX_DIRECTORY EVIDENCE_DIR [--stage STAGE] [--x11] [--keep-open SECONDS]
Extra environment for Raven (RAVEN_TRACE_API, RAVEN_SURVEY_MISSING ...) passes through.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))
from x11_session import X11Session  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
RUNTIME = Path(f"/run/user/{os.getuid()}/raven-labwc-runtime")
STAGES = ("window", "open", "view")
x11 = None


def environment():
    if x11:
        return {"PATH": "/usr/bin:/bin"} | x11.environment()
    return {"PATH": "/usr/bin:/bin", "XDG_RUNTIME_DIR": str(RUNTIME), "WAYLAND_DISPLAY": "wayland-0"}


def start_compositor(log):
    """Start a headless labwc that never touches the user's own session."""
    if (RUNTIME / "wayland-0").exists():
        probe = subprocess.run(["wlrctl", "toplevel", "list"], env=environment(), capture_output=True, timeout=10)
        if probe.returncode == 0:
            return None
        (RUNTIME / "wayland-0").unlink(missing_ok=True)
        (RUNTIME / "wayland-0.lock").unlink(missing_ok=True)
    RUNTIME.mkdir(mode=0o700, parents=True, exist_ok=True)
    env = environment() | {"WLR_BACKENDS": "headless", "WLR_RENDERER": "pixman", "WLR_HEADLESS_OUTPUTS": "1"}
    process = subprocess.Popen(["labwc"], env=env, stdout=log, stderr=log)
    for _ in range(50):
        if (RUNTIME / "wayland-0").exists():
            time.sleep(0.5)
            subprocess.run(["wlr-randr", "--output", "HEADLESS-1", "--custom-mode", "1600x1000"], env=environment(),
                           capture_output=True, timeout=10)
            return process
        time.sleep(0.1)
    raise RuntimeError("isolated compositor did not start")


def titles():
    if x11:
        probe = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "."], env=environment(), capture_output=True, text=True, timeout=10)
        names = (subprocess.run(["xdotool", "getwindowname", window], env=environment(), capture_output=True, text=True, timeout=10)
                 for window in probe.stdout.split())
        return [name.stdout.strip() for name in names if name.returncode == 0]
    probe = subprocess.run(["wlrctl", "toplevel", "list"], env=environment(), capture_output=True, text=True, timeout=10)
    return [line.split(": ", 1)[1] for line in probe.stdout.splitlines() if ": " in line]


def screenshot(path):
    if x11:
        return x11.capture(path)
    subprocess.run(["grim", str(path)], env=environment(), check=True, timeout=20)
    return Image.open(path).convert("RGB")


def drawn(image):
    return image.point(lambda value: 255 if value > 8 else 0).convert("L").getbbox()


def same(left, right):
    changed = ImageChops.difference(left, right).convert("L").point(lambda value: 255 if value > 16 else 0).getbbox()
    return changed is None or (changed[2] - changed[0]) * (changed[3] - changed[1]) <= 40 * 40


def key(combination):
    if x11:
        subprocess.run(["xdotool", "key", combination], env=environment(), check=True, timeout=30)
    else:
        *modifiers, name = combination.split("+")
        subprocess.run(["wtype", "-s", "500", *(a for m in modifiers for a in ("-M", m)), "-k", name,
                        *(a for m in modifiers for a in ("-m", m))], env=environment(), check=True, timeout=30)
    time.sleep(1.5)


class Run:
    def __init__(self, raven, evidence, process, log):
        self.raven, self.evidence, self.process, self.log = raven, evidence, process, log
        self.shots = 0
        self.seen = []

    def alive(self):
        return self.process.poll() is None

    def note_titles(self):
        current = titles()
        for title in current:
            if title not in self.seen:
                self.seen.append(title)
                print(f"[{time.strftime('%H:%M:%S')}] window: {title!r}", flush=True)
        return current

    def shot(self, name):
        self.shots += 1
        return screenshot(self.evidence / f"{self.shots:02d}-{name}.png")

    def wait(self, description, condition, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.note_titles()
            value = condition()
            if value:
                return value
            if not self.alive():
                raise RuntimeError(f"PDF-XChange exited with status {self.process.returncode} while waiting for {description}")
            time.sleep(1)
        raise RuntimeError(f"timed out waiting for {description}; windows: {titles()}")

    def settle(self, name, timeout=240):
        """Wait until the screen stops changing and is not black, then return its screenshot."""
        previous, stable = None, 0
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.note_titles()
            if not self.alive():
                raise RuntimeError(f"PDF-XChange exited with status {self.process.returncode} before {name} settled")
            image = screenshot(self.evidence / f"settle-{name}.png")
            stable = stable + 1 if drawn(image) and previous is not None and same(image, previous) else 0
            previous = image
            if stable >= 3:
                self.shots += 1
                final = self.evidence / f"{self.shots:02d}-{name}.png"
                shutil.move(self.evidence / f"settle-{name}.png", final)
                print(f"[{time.strftime('%H:%M:%S')}] settled: {final.name}", flush=True)
                return image
            time.sleep(2)
        raise RuntimeError(f"the screen never settled for {name}")


def main():
    global x11
    parser = argparse.ArgumentParser()
    parser.add_argument("raven", type=Path)
    parser.add_argument("program", type=Path)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--stage", choices=STAGES, default="window")
    parser.add_argument("--x11", action="store_true")
    parser.add_argument("--pdf", type=Path, help="open this file (as raven-test.pdf) instead of the generated test document")
    parser.add_argument("--wait", type=int, default=300, help="seconds to wait for the main window")
    parser.add_argument("--keep-open", type=int, default=0, help="seconds to keep the program running at the end")
    args = parser.parse_args()
    shutil.rmtree(args.evidence, ignore_errors=True)
    args.evidence.mkdir(parents=True)
    if args.x11:
        with X11Session(args.evidence / "x11") as x11:
            return workflow(args)
    return workflow(args)


def workflow(args):
    evidence = args.evidence.resolve()
    program_directory = evidence / "pdfx"
    shutil.copytree(args.program, program_directory)
    prefix = evidence / "prefix"
    documents = prefix / "drive_c" / "users" / "Public" / "Documents"
    documents.mkdir(parents=True)
    pdf = documents / "raven-test.pdf"
    if args.pdf:
        shutil.copyfile(args.pdf, pdf)
    else:
        subprocess.run([sys.executable, str(REPO / "testdata/pdfx/make-test-pdf.py"), str(pdf), "5"], check=True)
    stage = STAGES.index(args.stage)

    compositor = None if x11 else start_compositor((evidence / "compositor.log").open("wb"))
    log = (evidence / "raven.log").open("wb")
    env = environment() | {"RAVEN_PREFIX": str(prefix)}
    env |= {name: value for name, value in os.environ.items() if name.startswith("RAVEN_") and name != "RAVEN_PREFIX"}
    command = [str(args.raven.resolve()), "--run-process", "--execution=native", "--timeout-ms=1800000",
               "--desktop=x11" if x11 else "--desktop=wayland", str(program_directory / "PXCEditor.exe")]
    if stage >= STAGES.index("open"):
        command += ["--", r"C:\users\Public\Documents\raven-test.pdf"]
    started = time.time()
    run = Run(args.raven, evidence, subprocess.Popen(command, env=env, stdout=log, stderr=log), log)
    try:
        run.wait("a PDF-XChange window", lambda: any("PDF-XChange" in title for title in run.note_titles()), 300)
        print(f"[{time.strftime('%H:%M:%S')}] first window after {time.time() - started:.0f}s", flush=True)
        # A message box ("Unable to load the PXCEditorCore.x64.dll module") also carries the title; the main window is far larger.
        run.wait("the main window", lambda: (box := drawn(screenshot(evidence / "probe.png"))) and box[2] - box[0] > 900
                 and box[3] - box[1] > 500, args.wait)
        run.settle("window")
        if stage >= STAGES.index("open"):
            run.wait("the test document in the title", lambda: any("raven-test" in title for title in run.note_titles()), 180)
            before = run.settle("opened")
        if stage >= STAGES.index("view"):
            key("Next")
            scrolled = run.settle("page-down")
            if same(before, scrolled):
                raise RuntimeError("PageDown did not change the view")
            key("ctrl+plus")
            zoomed = run.settle("zoom-in")
            if same(scrolled, zoomed):
                raise RuntimeError("zooming in did not change the view")
        print(f"stage {args.stage} passed", flush=True)
        end = time.time() + args.keep_open
        while time.time() < end and run.alive():
            run.note_titles()
            time.sleep(2)
        if args.keep_open and run.alive():
            run.shot("kept-open")
        return 0
    except Exception as error:
        print(f"FAILED: {error}", flush=True)
        if run.alive():
            try:
                run.shot("failure")
            except Exception:
                pass
        return 1
    finally:
        if run.alive():
            run.process.kill()
            run.process.wait()
        print(f"raven exit status: {run.process.returncode}; log: {evidence / 'raven.log'}", flush=True)
        if compositor:
            compositor.kill()
            compositor.wait()


if __name__ == "__main__":
    sys.exit(main())
