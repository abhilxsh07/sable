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

# Writes the four small PDFs the tests open, next to this script:
#   pages.pdf       three pages (portrait, landscape, rotated), labels i, ii, 1
#   password.pdf    one page, needs the password "sable" (owner: "nightpdf")
#   restricted.pdf  the same page, opens without a password, all permissions off
#   broken.pdf      not a PDF at all
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


def main() -> None:
    files = {
        "pages.pdf": build_pdf("pages.pdf", [PAGE_1, PAGE_2, PAGE_3], LABELS),
        "password.pdf": build_pdf(
            "password.pdf", [PAGE_1], security=(USER_PASSWORD, OWNER_PASSWORD, -4)
        ),
        # the empty user password opens it, only the permissions are off
        "restricted.pdf": build_pdf(
            "restricted.pdf", [PAGE_1], security=(b"", OWNER_PASSWORD, -3904)
        ),
        "broken.pdf": b"%PDF-1.7\n" + b"this is not really a pdf\n" * 20,
    }
    for name, data in files.items():
        with open(os.path.join(HERE, name), "wb") as file:
            file.write(data)
        print(f"wrote {name}, {len(data)} bytes")


if __name__ == "__main__":
    main()
