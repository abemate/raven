#!/usr/bin/env python3
"""Write a one-page PDF with an AcroForm: a text field "name" and a check box "agree", to check filling forms in PDF-XChange Editor.

usage: make-form-pdf.py OUTPUT.pdf
"""

import sys


def build():
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)

    catalog = add(None)
    pages = add(None)
    page = add(None)
    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    content_stream = b"BT /F1 24 Tf 72 720 Td (Raven form test) Tj ET\nBT /F1 14 Tf 72 655 Td (Name:) Tj ET\nBT /F1 14 Tf 72 605 Td (I agree:) Tj ET"
    content = add(b"<< /Length %d >>\nstream\n" % len(content_stream) + content_stream + b"\nendstream")
    check_on = b"0 0 0 rg 4 4 12 12 re f"
    on_appearance = add(b"<< /Type /XObject /Subtype /Form /BBox [0 0 20 20] /Length %d >>\nstream\n" % len(check_on) + check_on + b"\nendstream")
    off_appearance = add(b"<< /Type /XObject /Subtype /Form /BBox [0 0 20 20] /Length 0 >>\nstream\n\nendstream")
    name_field = add(b"<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [140 645 400 670] /P %d 0 R /F 4 "
                     b"/DA (/Helv 12 Tf 0 g) /MK << /BC [0 0 0] /BG [1 1 1] >> /V () >>" % page)
    agree_field = add(b"<< /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /Rect [140 600 160 620] /P %d 0 R /F 4 "
                      b"/V /Off /AS /Off /MK << /BC [0 0 0] /BG [1 1 1] >> /AP << /N << /Yes %d 0 R /Off %d 0 R >> >> >>"
                      % (page, on_appearance, off_appearance))
    objects[catalog - 1] = (b"<< /Type /Catalog /Pages %d 0 R /AcroForm << /Fields [%d 0 R %d 0 R] /NeedAppearances true "
                            b"/DR << /Font << /Helv %d 0 R >> >> /DA (/Helv 12 Tf 0 g) >> >>" % (pages, name_field, agree_field, font))
    objects[pages - 1] = b"<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page
    objects[page - 1] = (b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R >> >> "
                         b"/Annots [%d 0 R %d 0 R] >>" % (pages, content, font, name_field, agree_field))
    out = bytearray(b"%PDF-1.6\n%\xe2\xe3\xcf\xd3\n")
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
        output.write(build())
