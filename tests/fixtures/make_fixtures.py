# Sable, a native port of NightPDF
# Copyright (C) 2021  Advaith Madhukar
# Copyright (C) 2026  Abhilash Kar
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Writes the PDFs the tests open, next to this script:
#   annotations.pdf Unicode comments, unknown types, damaged fields and limits
#   comments.pdf    three pages of small comments for native sidebar checks
#   pages.pdf       three pages (portrait, landscape, rotated), labels i, ii, 1
#   links.pdf       cropped/rotated pages, destinations and URI/action links
#   text.pdf        cropped pages at four rotations and Unicode text mappings
#   outline*.pdf    hierarchy, Unicode titles, actions, cycles and deep nesting
#   print.pdf       original colors, print-visible and screen-only annotations
#   password.pdf    one page, needs the password "sable" (owner: "nightpdf")
#   restricted.pdf  the same page, opens without a password, all permissions off
#   broken.pdf      not a PDF at all
#   empty.pdf       an empty file
#   not_pdf.pdf     plain text with a PDF extension
#   no_pages.pdf    a PDF with an empty page tree
# The bytes are the same on every run (no dates, fixed ids), so a changed file
# in git means the script changed. Only the standard library is used, so I wrote
# RC4 and the key derivation out by hand. They follow ISO 32000-1 7.6.3 for the
# standard security handler, revision 3 (128 bit RC4).
#
# Usage, from rework/:
#   python tests/fixtures/make_fixtures.py
import hashlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))

TITLE = "Sable test file"
USER_PASSWORD = b"sable"
OWNER_PASSWORD = b"nightpdf"

# every password is cut or filled up to 32 bytes with this (algorithm 2, step a)
PAD = bytes.fromhex(
    "28bf4e5e4e758a41 64004e56fffa0108 2e2e00b6d0683e80 2f0ca9fe6453697a"
)


def md5(data: bytes) -> bytes:
    # the format asks for md5, it isn't guarding anything here
    return hashlib.md5(data, usedforsecurity=False).digest()


def rc4(key: bytes, data: bytes) -> bytes:
    # key schedule
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) % 256
        state[i], state[j] = state[j], state[i]
    # key stream, xored into the data (encrypting and decrypting are the same)
    out = bytearray()
    i = j = 0
    for byte in data:
        i = (i + 1) % 256
        j = (j + state[i]) % 256
        state[i], state[j] = state[j], state[i]
        out.append(byte ^ state[(state[i] + state[j]) % 256])
    return bytes(out)


def pad_password(password: bytes) -> bytes:
    return (password + PAD)[:32]


def with_counter(key: bytes, i: int) -> bytes:
    # the key with every byte xored with the round number (algorithms 3 and 5)
    return bytes(b ^ i for b in key)


def owner_entry(owner: bytes, user: bytes) -> bytes:
    """Algorithm 3: the /O value, revision 3."""
    digest = md5(pad_password(owner))
    for _ in range(50):
        digest = md5(digest[:16])
    key = digest[:16]
    value = rc4(key, pad_password(user))
    for i in range(1, 20):
        value = rc4(with_counter(key, i), value)
    return value


def file_key(user: bytes, o: bytes, permissions: int, id0: bytes) -> bytes:
    """Algorithm 2: the key every object key is made from."""
    flags = (permissions & 0xFFFFFFFF).to_bytes(4, "little")
    digest = md5(pad_password(user) + o + flags + id0)
    for _ in range(50):
        digest = md5(digest[:16])
    return digest[:16]


def user_entry(key: bytes, id0: bytes) -> bytes:
    """Algorithm 5: the /U value, revision 3 (the last 16 bytes are free, zeros)."""
    value = rc4(key, md5(PAD + id0))
    for i in range(1, 20):
        value = rc4(with_counter(key, i), value)
    return value + bytes(16)


def object_key(key: bytes, num: int, gen: int = 0) -> bytes:
    """Algorithm 1: every object gets an RC4 key of its own."""
    return md5(key + num.to_bytes(3, "little") + gen.to_bytes(2, "little"))[:16]


def standard_handler(
    user: bytes, owner: bytes, permissions: int, id0: bytes
) -> tuple[bytes, str]:
    """The file key and the text of the /Encrypt dictionary."""
    o = owner_entry(owner, user)
    key = file_key(user, o, permissions, id0)
    u = user_entry(key, id0)
    text = (
        "<< /Filter /Standard /V 2 /R 3 /Length 128 "
        f"/P {permissions} /O <{o.hex()}> /U <{u.hex()}> >>"
    )
    return key, text


class Writer:
    """Numbered objects one after the other, then xref, trailer and %%EOF."""

    def __init__(self, id0: bytes, key: bytes | None = None):
        self.id0 = id0
        # the file key, None for a file that isn't encrypted
        self.key = key
        self.out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        self.offsets: list[int] = []

    def seal(self, num: int, data: bytes) -> bytes:
        if self.key is None:
            return data
        return rc4(object_key(self.key, num), data)

    def string(self, num: int, text: str) -> str:
        # plain text only, no parentheses or backslashes in it. In an
        # encrypted file the string is the hex of what RC4 makes of it
        if self.key is None:
            return f"({text})"
        return f"<{self.seal(num, text.encode('latin-1')).hex()}>"

    def start(self, num: int) -> None:
        # I add the objects in order, so the xref can be built as I go
        assert num == len(self.offsets) + 1, f"object {num} is out of order"
        self.offsets.append(len(self.out))

    def add(self, num: int, body: str) -> None:
        self.start(num)
        self.out += f"{num} 0 obj\n{body}\nendobj\n".encode("latin-1")

    def add_stream(self, num: int, data: bytes) -> None:
        # rc4 keeps the length, but /Length is taken from what gets written
        self.start(num)
        data = self.seal(num, data)
        head = f"{num} 0 obj\n<< /Length {len(data)} >>\nstream\n"
        self.out += head.encode("latin-1") + data + b"\nendstream\nendobj\n"

    def finish(self, root: int, info: int, encrypt: int | None = None) -> bytes:
        size = len(self.offsets) + 1
        xref = len(self.out)
        # every entry is exactly 20 bytes: 10 digits, 5 digits, n or f, space, LF
        rows = ["0000000000 65535 f "]
        rows += [f"{offset:010d} 00000 n " for offset in self.offsets]
        ident = self.id0.hex()
        trailer = f"/Size {size} /Root {root} 0 R /Info {info} 0 R"
        if encrypt is not None:
            trailer += f" /Encrypt {encrypt} 0 R"
        trailer += f" /ID [<{ident}> <{ident}>]"
        tail = f"xref\n0 {size}\n" + "\n".join(rows) + "\n"
        tail += f"trailer\n<< {trailer} >>\nstartxref\n{xref}\n%%EOF\n"
        return bytes(self.out) + tail.encode("latin-1")


# not embedded, every reader has Helvetica
FONT = (
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
    "/Encoding /WinAnsiEncoding >>"
)

# pages 1 and 2 (index 0 and 1) count as i and ii, the third one starts at 1
LABELS = "/PageLabels << /Nums [0 << /S /r >> 2 << /S /D >>] >>"


def content(*lines: str) -> bytes:
    return "\n".join(lines).encode("latin-1")


# each page is (MediaBox, Rotate, content stream)
PAGE_1 = (
    "0 0 595 842",
    0,
    content(
        "0 0 0 rg 50 592 200 200 re f",  # black, top left
        "1 0 0 rg 345 592 200 200 re f",  # red, top right
        "0 0 1 rg 50 50 200 200 re f",  # blue, bottom left
        "BT /F1 24 Tf 0 0 0 rg 50 400 Td (Sable test page 1) Tj ET",
    ),
)
PAGE_2 = (
    "0 0 792 612",
    0,
    content(
        "0 1 0 rg 100 100 200 200 re f",  # green
        "BT /F1 24 Tf 0 0 0 rg 400 300 Td (Sable test page 2) Tj ET",
    ),
)
PAGE_3 = (
    "0 0 300 200",
    90,
    content(
        # the left half of the page as it is stored, so the top once rotated
        "0 0 0 rg 0 0 150 200 re f",
        "BT /F1 12 Tf 0 0 0 rg 160 90 Td (page 3) Tj ET",
    ),
)


def build_pdf(
    name: str,
    pages: list[tuple[str, int, bytes]],
    catalog: str = "",
    security: tuple[bytes, bytes, int] | None = None,
) -> bytes:
    """Object 1 is the catalog, 2 the page tree and 3 the font. Then every page
    takes two (the page, its content), the info comes next and the /Encrypt
    dictionary last. security is (user password, owner password, /P)."""
    id0 = md5(b"sable " + name.encode("ascii"))
    key = encrypt = None
    if security is not None:
        key, encrypt = standard_handler(*security, id0)
    w = Writer(id0, key)

    first = 4
    info = first + 2 * len(pages)
    kids = " ".join(f"{first + 2 * i} 0 R" for i in range(len(pages)))
    extra = f" {catalog}" if catalog else ""
    w.add(1, f"<< /Type /Catalog /Pages 2 0 R{extra} >>")
    w.add(2, f"<< /Type /Pages /Kids [{kids}] /Count {len(pages)} >>")
    w.add(3, FONT)
    for i, (box, rotate, stream) in enumerate(pages):
        num = first + 2 * i
        turn = f" /Rotate {rotate}" if rotate else ""
        w.add(
            num,
            f"<< /Type /Page /Parent 2 0 R /MediaBox [{box}]{turn} "
            f"/Resources << /Font << /F1 3 0 R >> >> /Contents {num + 1} 0 R >>",
        )
        w.add_stream(num + 1, stream)
    w.add(info, f"<< /Title {w.string(info, TITLE)} >>")
    if encrypt is not None:
        w.add(info + 1, encrypt)
    return w.finish(1, info, info + 1 if encrypt is not None else None)


def link_pdf() -> bytes:
    w = Writer(md5(b"sable links.pdf"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R "
          "/Dests << /chapter [6 0 R /FitH 450] >> >>")
    w.add(2, "<< /Type /Pages /Kids [4 0 R 6 0 R] /Count 2 >>")
    w.add(3, FONT)
    actions = [
        "/A << /S /URI /URI (https://example.com/sable?x=1#part) >>",
        "/Dest [6 0 R /XYZ 60 400 2]",
        "/A << /S /GoTo /D [6 0 R /Fit] >>",
        "/Dest /chapter",
        "/A << /S /URI /URI (javascript:alert) >>",
        "/A << /S /Launch /F (calc.exe) >>",
        "/A << /S /GoToR /F (other.pdf) /D [0 /Fit] >>",
    ]
    annots = []
    drawing = []
    for i, action in enumerate(actions):
        top = 540 - i * 80
        annots.append(f"<< /Type /Annot /Subtype /Link "
                      f"/Rect [40 {top - 40} 160 {top}] {action} >>")
        drawing.append(f"0 0 1 RG 40 {top - 40} 120 40 re S")
    w.add(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 600] "
          "/CropBox [20 30 380 570] /Annots [" + " ".join(annots) + "] "
          "/Resources << /Font << /F1 3 0 R >> >> /Contents 5 0 R >>")
    w.add_stream(5, content(*drawing))
    w.add(6, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 500] /Rotate 90 "
          "/Annots ["
          "<< /Subtype /Link /Rect [20 100 100 180] "
          "/A << /S /URI /URI (HTTP://example.com/rotated) >> >> "
          "<< /Subtype /Link /Rect [120 100 200 180] "
          "/Dest [4 0 R /XYZ null 200 null] >> "
          "<< /Subtype /Link /Rect [20 220 100 280] "
          "/Dest [4 0 R /FitR 40 100 200 400] >>] "
          "/Resources << /Font << /F1 3 0 R >> >> /Contents 7 0 R >>")
    w.add_stream(7, content("0 0 1 RG 20 100 80 80 re S"))
    w.add(8, "<< /Title (Sable link fixtures) >>")
    return w.finish(1, 8)


def text_pdf() -> bytes:
    w = Writer(md5(b"sable text.pdf"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R >>")
    w.add(2, "<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R 10 0 R 12 0 R] /Count 5 >>")
    w.add(3, FONT)
    for i, rotation in enumerate((0, 90, 180, 270, 0)):
        page = 4 + 2 * i
        font = 14 if i == 4 else 3
        w.add(page, f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
              f"/CropBox [20 30 380 270] /Rotate {rotation} "
              f"/Resources << /Font << /F1 {font} 0 R >> >> /Contents {page + 1} 0 R >>")
        text = "(ABC) Tj" if i == 4 else "(Alpha beta) Tj 0 -40 Td (Second line) Tj"
        w.add_stream(page + 1, content(f"BT /F1 20 Tf 40 230 Td {text} ET"))
    w.add(14, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
          "/Encoding /WinAnsiEncoding /ToUnicode 15 0 R >>")
    w.add_stream(15, content("/CIDInit /ProcSet findresource begin 12 dict begin begincmap",
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def",
        "/CMapName /SableUnicode def /CMapType 2 def",
        "1 begincodespacerange <00> <FF> endcodespacerange",
        "3 beginbfchar <41> <03A9> <42> <D83DDE00> <43> <00E9> endbfchar",
        "endcmap CMapName currentdict /CMap defineresource pop end end"))
    w.add(16, "<< /Title (Sable text fixtures) >>")
    return w.finish(1, 16)


def print_pdf() -> bytes:
    w = Writer(md5(b"sable print.pdf"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    w.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
          "/Resources << >> /Contents 4 0 R /Annots [5 0 R 6 0 R] >>")
    w.add_stream(4, content("1 0 0 rg 10 10 30 30 re f"))
    w.add(5, "<< /Type /Annot /Subtype /Square /Rect [50 10 80 40] "
          "/F 4 /AP << /N 7 0 R >> >>")
    w.add(6, "<< /Type /Annot /Subtype /Square /Rect [10 50 40 80] "
          "/F 0 /AP << /N 8 0 R >> >>")
    for obj, color in ((7, "0 1 0"), (8, "0 0 1")):
        data = content(f"{color} rg 0 0 30 30 re f")
        w.add(obj, f"<< /Type /XObject /Subtype /Form /BBox [0 0 30 30] "
              f"/Resources << >> /Length {len(data)} >>\nstream\n"
              + data.decode("latin-1") + "\nendstream")
    w.add(9, "<< /Title (Sable print fixture) >>")
    return w.finish(1, 9)



def outline_pdf(cycle: bool = False) -> bytes:
    name = b"sable outline_cycle.pdf" if cycle else b"sable outline.pdf"
    w = Writer(md5(name))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R /Outlines 6 0 R "
          "/Names << /Dests << /Names [(chapter-two) [5 0 R /FitH 210]] >> >> >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>")
    for obj, rotation in ((3, 0), (4, 90), (5, 0)):
        w.add(obj, f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
              f"/CropBox [20 30 380 270] /Rotate {rotation} /Resources << >> >>")
    w.add(6, f"<< /Type /Outlines /First 7 0 R /Last {15 if cycle else 14} 0 R "
          f"/Count {8 if cycle else 7} >>")
    title = ("Chapters \u03a9\U0001f600").encode("utf-16-be").hex()
    w.add(7, f"<< /Title <feff{title}> /Parent 6 0 R /First 8 0 R /Last 9 0 R "
          "/Next 11 0 R /Count 2 /Dest [3 0 R /Fit] >>")
    w.add(8, "<< /Title (Rotated XYZ) /Parent 7 0 R /Next 9 0 R "
          "/Dest [4 0 R /XYZ 60 220 1.5] >>")
    w.add(9, "<< /Title (Named chapter) /Parent 7 0 R /Prev 8 0 R "
          "/First 10 0 R /Last 10 0 R /Count -1 /A << /S /GoTo /D (chapter-two) >> >>")
    w.add(10, "<< /Title (Web reference) /Parent 9 0 R "
          "/A << /S /URI /URI (https://example.com/outline) >> >>")
    w.add(11, "<< /Title (Unsupported action) /Parent 6 0 R /Prev 7 0 R /Next 12 0 R "
          "/A << /S /Launch /F (nope.exe) >> >>")
    w.add(12, "<< /Title () /Parent 6 0 R /Prev 11 0 R /Next 13 0 R >>")
    w.add(13, "<< /Title (Broken destination) /Parent 6 0 R /Prev 12 0 R /Next 14 0 R "
          "/Dest [9999 0 R /Fit] >>")
    next_item = "/Next 15 0 R " if cycle else ""
    w.add(14, "<< /Title (Unsafe URI) /Parent 6 0 R /Prev 13 0 R " + next_item +
          "/A << /S /URI /URI (javascript:alert%281%29) >> >>")
    if cycle:
        w.add(15, "<< /Title (Sibling cycle) /Parent 6 0 R /Prev 14 0 R /Next 15 0 R >>")
    info = 16 if cycle else 15
    w.add(info, "<< /Title (Sable outline fixtures) >>")
    return w.finish(1, info)


def deep_outline_pdf() -> bytes:
    w = Writer(md5(b"sable outline_deep.pdf"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    w.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] >>")
    w.add(4, "<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 70 >>")
    for level in range(70):
        obj = 5 + level
        parent = 4 if level == 0 else obj - 1
        child = f"/First {obj+1} 0 R /Last {obj+1} 0 R /Count {69-level}" if level < 69 else ""
        w.add(obj, f"<< /Title (Level {level}) /Parent {parent} 0 R {child} >>")
    w.add(75, "<< /Title (Sable deep outline fixture) >>")
    return w.finish(1, 75)


def attachments_pdf() -> bytes:
    w = Writer(md5(b"sable attachments.pdf"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles "
          "<< /Names [(a) 4 0 R (b) 5 0 R (c) 6 0 R (d) 7 0 R] >> >> >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    w.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] >>")
    unicode_name = "notes \u03a9\U0001f600.txt".encode("utf-16-be").hex()
    w.add(4, f"<< /Type /Filespec /UF <feff{unicode_name}> "
          "/F (notes.txt) /EF << /F 8 0 R >> >>")
    w.add(5, "<< /Type /Filespec /F (../../empty.bin) /EF << /F 9 0 R >> >>")
    w.add(6, "<< /Type /Filespec /F (broken.bin) >>")
    w.add(7, "<< /Type /Filespec /F <00626164> /EF << /F 8 0 R >> >>")
    w.add_stream(8, b"hello\x00\xff\n")
    w.add_stream(9, b"")
    w.add(10, "<< /Title (Sable attachment fixture) >>")
    return w.finish(1, 10)


def annotations_pdf() -> bytes:
    w = Writer(md5(b"sable annotation snapshots"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R 7 0 R] "
          "/Count 5 >>")
    refs = ["9 0 R 10 0 R 11 0 R 12 0 R 13 0 R 14 0 R", "", "15 0 R",
            " ".join(["11 0 R"] * 4097), " ".join(["16 0 R"] * 40)]
    for i, annots in enumerate(refs):
        w.add(i + 3, "<< /Type /Page /Parent 2 0 R "
              "/MediaBox [0 0 300 400] /CropBox [10 20 290 380] "
              f"/Rotate 90 /Annots [{annots}] >>")
    w.add(8, "<< /Title (Sable annotation fixtures) >>")
    note = (b"\xfe\xff" + "Notes \u03a9\U0001f600\nsecond line".encode(
        "utf-16-be")).hex()
    author = (b"\xfe\xff" + "Abhilash \u03a9".encode("utf-16-be")).hex()
    w.add(9, "<< /Type /Annot /Subtype /Text /Rect [40 50 60 70] "
          f"/Contents <{note}> /T <{author}> /F 68 >>")
    w.add(10, "<< /Type /Annot /Subtype /Highlight /Rect [20 30 80 45] "
          "/QuadPoints [20 45 80 45 20 30 80 30] /Contents (Review) >>")
    w.add(11, "<< /Type /Annot /Subtype /Link /Rect [20 80 60 90] >>")
    w.add(12, "<< /Type /Annot /Subtype /Widget /Rect [20 90 60 100] >>")
    w.add(13, "<< /Type /Annot /Subtype /FutureNote /Contents (Keep me) >>")
    w.add(14, "<< /Type /Annot /Subtype /Text /Contents 123 /T [] "
          "/Rect (broken) >>")
    w.add(15, "<< /Type /Annot /Subtype /Text /Contents (" + "A" * 70000 +
          ") /T (Still here) >>")
    w.add(16, "<< /Type /Annot /Subtype /Text /Contents (" + "A" * 16384 +
          ") >>")
    return w.finish(1, 8)


def comments_pdf() -> bytes:
    w = Writer(md5(b"sable comments sidebar"))
    w.add(1, "<< /Type /Catalog /Pages 2 0 R >>")
    w.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>")
    for i, refs in enumerate(["7 0 R 10 0 R 11 0 R 12 0 R", "8 0 R", "9 0 R"]):
        w.add(i + 3, "<< /Type /Page /Parent 2 0 R "
              f"/MediaBox [0 0 300 400] /Annots [{refs}] >>")
    w.add(6, "<< /Title (Sable comments sidebar) >>")
    author = (b"\xfe\xff" + "Abhilash \u03a9".encode("utf-16-be")).hex()
    text = (b"\xfe\xff" + "Notes \u03a9\U0001f600\nsecond line".encode(
        "utf-16-be")).hex()
    w.add(7, "<< /Type /Annot /Subtype /Text /Rect [30 40 50 60] "
          f"/Contents <{text}> /T <{author}> >>")
    w.add(8, "<< /Type /Annot /Subtype /Highlight /Rect [20 30 80 45] "
          "/QuadPoints [20 45 80 45 20 30 80 30] /Contents (Review) >>")
    w.add(9, "<< /Type /Annot /Subtype /Text /Rect [30 40 50 60] "
          "/Contents (Last page) >>")
    for i, kind in enumerate(["Link", "Popup", "Widget"]):
        w.add(10 + i, f"<< /Type /Annot /Subtype /{kind} "
              "/Rect [20 80 60 90] /Contents (Excluded) >>")
    return w.finish(1, 6)


def main() -> None:
    # numbered, mixed sizes, with /Rotate on every third page
    long_pages = []
    for i in range(1000):
        box, rotate, _ = (PAGE_1, PAGE_2, PAGE_3)[i % 3]
        long_pages.append((box, rotate, content(
            "0.8 0.2 0.1 rg 20 20 50 50 re f",
            f"BT /F1 24 Tf 0 0 0 rg 80 100 Td (Page {i + 1}) Tj ET",
        )))
    files = {
        "annotations.pdf": annotations_pdf(),
        "comments.pdf": comments_pdf(),
        "attachments.pdf": attachments_pdf(),
        "outline.pdf": outline_pdf(),
        "outline_cycle.pdf": outline_pdf(True),
        "outline_deep.pdf": deep_outline_pdf(),
        "links.pdf": link_pdf(),
        "print.pdf": print_pdf(),
        "long.pdf": build_pdf("long.pdf", long_pages),
        "pages.pdf": build_pdf("pages.pdf", [PAGE_1, PAGE_2, PAGE_3], LABELS),
        "text.pdf": text_pdf(),
        "password.pdf": build_pdf(
            "password.pdf", [PAGE_1], security=(USER_PASSWORD, OWNER_PASSWORD, -4)
        ),
        # the empty user password opens it, only the permissions are off
        "restricted.pdf": build_pdf(
            "restricted.pdf", [PAGE_1], security=(b"", OWNER_PASSWORD, -3904)
        ),
        "broken.pdf": b"%PDF-1.7\n" + b"this is not really a pdf\n" * 20,
        "empty.pdf": b"",
        "not_pdf.pdf": b"this file contains plain text, not a PDF\n",
        "no_pages.pdf": build_pdf("no_pages.pdf", []),
    }
    for name, data in files.items():
        with open(os.path.join(HERE, name), "wb") as file:
            file.write(data)
        print(f"wrote {name}, {len(data)} bytes")


if __name__ == "__main__":
    main()
