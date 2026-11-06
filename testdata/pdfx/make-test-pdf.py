#!/usr/bin/env python3
"""Write a small, valid multi-page PDF with large page numbers and text, to check PDF-XChange Editor opens, scrolls and zooms.

usage: make-test-pdf.py OUTPUT.pdf [PAGES]
"""

import sys


def page_stream(number, pages):
    # Big page number and a heading; a grid of boxes makes scroll and zoom visible on a screenshot.
    lines = [
        # A filled bar across the top, coloured per page: vector drawing, visible without any font.
        "%.2f 0.3 %.2f rg 36 740 540 30 re f" % (0.2 * number % 1.0, 1.0 - 0.15 * number),
        "0 0 0 rg",
        "BT /F1 48 Tf 72 700 Td (Raven test page %d of %d) Tj ET" % (number, pages),
        "BT /F1 18 Tf 72 650 Td (The quick brown fox jumps over the lazy dog.) Tj ET",
        "BT /F1 220 Tf 200 300 Td (%d) Tj ET" % number,
        "0.2 0.4 0.8 RG 4 w",
    ]
    for row in range(4):
        for column in range(4):
            lines.append("%d %d 100 40 re S" % (72 + column * 120, 80 + row * 50))
    return "\n".join(lines).encode()


def build(pages):
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)

    catalog = add(None)
    tree = add(None)
    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    kids = []
    for number in range(1, pages + 1):
        stream = page_stream(number, pages)
        content = add(b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream")
        kids.append(add(b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] /Contents %d 0 R "
                        b"/Resources << /Font << /F1 %d 0 R >> >> >>" % (tree, content, font)))
    objects[catalog - 1] = b"<< /Type /Catalog /Pages %d 0 R >>" % tree
    objects[tree - 1] = b"<< /Type /Pages /Kids [%s] /Count %d >>" % (b" ".join(b"%d 0 R" % kid for kid in kids), pages)

    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for index, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % index + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    out += b"".join(b"%010d 00000 n \n" % offset for offset in offsets)
    out += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, catalog, xref)
    return bytes(out)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as output:
        output.write(build(int(sys.argv[2]) if len(sys.argv) > 2 else 5))
