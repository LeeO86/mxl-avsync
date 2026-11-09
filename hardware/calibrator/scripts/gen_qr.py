"""Writes qr_matrix.txt (1 = dark module) for the bottom-side QR code.

Needs the 'segno' package (pip install segno); the matrix is committed, so the
board build does not need it. URL: the project's GitHub Pages site.
"""
import os

import segno

URL = "https://leeo86.github.io/mxl-avsync/"
q = segno.make(URL, error="m", micro=False)
rows = ["".join("1" if c else "0" for c in row) for row in q.matrix]
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "qr_matrix.txt"), "w") as f:
    f.write(f"# {URL}  version {q.version}-{q.error}, {len(rows)}x{len(rows)}, no quiet zone\n")
    f.write("\n".join(rows) + "\n")
print(q.version, q.error, len(rows))
