"""Tests for the asset tooling. Synthetic images only: no game data, no original art.

    python -m unittest discover -s tools/assets/tests
"""
from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import baseline  # noqa: E402
import extract  # noqa: E402
import manifest  # noqa: E402
import synthetic  # noqa: E402
import usage  # noqa: E402
from ja2assets import gates, pcx, png, sti  # noqa: E402
from ja2assets.classify import classify  # noqa: E402
from ja2assets.slf import SlfArchive, write_slf  # noqa: E402


def tree_digest(root: Path) -> str:
    """A hash of every file under `root`, by relative path: two runs must agree byte for byte."""
    import hashlib
    h = hashlib.sha256()
    for f in sorted(root.rglob("*")):
        if f.is_file():
            h.update(f.relative_to(root).as_posix().encode("utf-8"))
            h.update(b"\0")
            h.update(f.read_bytes())
            h.update(b"\0")
    return h.hexdigest()

PALETTE = [(i, 255 - i, (i * 7) % 256) for i in range(256)]


def diamond(w: int, h: int, colour: int) -> tuple[bytes, bytes]:
    idx = bytearray(w * h)
    mask = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if abs(x - w // 2) * h + abs(y - h // 2) * w <= w * h // 2:
                idx[y * w + x] = colour
                mask[y * w + x] = 1
    return bytes(idx), bytes(mask)


def make_sti(frames=2) -> bytes:
    fs = []
    for i in range(frames):
        idx, mask = diamond(12, 8, 10 + i)
        fs.append((12, 8, -6 + i, -4, idx, mask))
    aux = b"".join(bytes([1, 2, 3, 0, 0, 0, 0, i, frames, 0, 0, 0, 0, 0, 0, 0]) for i in range(frames))
    return sti.build_indexed(fs, PALETTE, aux)


class Formats(unittest.TestCase):
    def test_png_roundtrip(self):
        img = png.Image.blank(3, 2)
        img.set_pixel(1, 1, (10, 20, 30, 255))
        back = png.decode(png.encode(img))
        self.assertEqual((back.width, back.height), (3, 2))
        self.assertEqual(back.pixel(1, 1), (10, 20, 30, 255))
        self.assertEqual(back.pixel(0, 0), (0, 0, 0, 0))

    def test_sti_etrle(self):
        s = sti.parse(make_sti())
        self.assertTrue(s.indexed)
        self.assertEqual(len(s.frames), 2)
        f = s.frames[1]
        self.assertEqual((f.width, f.height, f.offset_x, f.offset_y), (12, 8, -5, -4))
        img = s.frame_rgba(1)
        self.assertEqual(img.pixel(6, 4), (*PALETTE[11], 255))  # centre of the diamond
        self.assertEqual(img.pixel(0, 0)[3], 0)                 # corner is transparent
        aux = s.aux_objects()
        self.assertEqual(aux[1]["currentFrame"], 1)
        self.assertEqual(aux[0]["numberOfFrames"], 2)

    def test_etrle_long_runs(self):
        w, h = 300, 2
        idx = bytes(i % 256 for i in range(w * h))
        mask = bytes(1 if (i // 150) % 2 else 0 for i in range(w * h))
        enc = sti.encode_etrle(idx, mask, w, h)
        i2, m2 = sti.decode_etrle(enc, w, h)
        self.assertEqual(m2, mask)
        self.assertEqual(bytes(a if m else 0 for a, m in zip(i2, m2)), bytes(a if m else 0 for a, m in zip(idx, mask)))

    def test_pcx(self):
        idx = bytes([0, 1, 200, 255, 3, 3])
        img, pal = pcx.parse(pcx.build(3, 2, idx, PALETTE))
        self.assertEqual((img.width, img.height), (3, 2))
        self.assertEqual(img.pixel(2, 0), (*PALETTE[200], 255))
        self.assertEqual(img.pixel(0, 1), (*PALETTE[255], 255))

    def test_sti_rgb565_whole_picture(self):
        pixels = struct.pack("<4H", 0xF800, 0x07E0, 0x001F, 0x0000)   # red, green, blue, black
        data = sti.build_rgb565(2, 2, pixels)
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], 8, "stored size is the whole picture")
        s = sti.parse(data)
        self.assertFalse(s.indexed, "a whole picture is not an indexed sheet")
        self.assertFalse(s.flags & sti.ETRLE_COMPRESSED)
        self.assertEqual((s.width, s.height, s.depth), (2, 2, 16))
        self.assertEqual([(f.width, f.height) for f in s.frames], [(2, 2)])
        self.assertEqual(s.frames[0].rgb565, [0xF800, 0x07E0, 0x001F, 0x0000])
        self.assertEqual(s.frame_rgba(0).pixel(0, 0), (255, 0, 0, 255))
        with self.assertRaises(ValueError):
            sti.build_rgb565(2, 2, b"\0\0\0")

    def test_slf(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "Test.slf"
            write_slf(f, "TEST.SLF", "Test/", {"A.STI": b"abc", "SUB/B.PCX": b"defg"})
            with SlfArchive(f) as a:
                self.assertEqual(a.path, "Test/")
                names = [e.name for e in a.files()]
                self.assertEqual(names, ["A.STI", "SUB/B.PCX"])
                self.assertEqual(a.read(a.entries[1]), b"defg")
                self.assertEqual(a.full_path(a.entries[1]), "Test/SUB/B.PCX")


class Pipeline(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.data = root / "Data"
        self.data.mkdir()
        write_slf(self.data / "Interface.slf", "INTERFACE.SLF", "Interface/", {"PANEL.STI": make_sti(1)})
        write_slf(self.data / "Faces.slf", "FACES.SLF", "Faces/", {"BIGFACES/01.STI": make_sti(3), "07.STI": make_sti(2)})
        write_slf(self.data / "Radarmaps.slf", "RADARMAPS.SLF", "Radarmaps/", {"A9.STI": make_sti(1)})
        (self.data / "tilesets" / "0").mkdir(parents=True)
        (self.data / "tilesets" / "0" / "grass.sti").write_bytes(make_sti(1))
        self.root = root

    def tearDown(self):
        self.tmp.cleanup()

    def test_extract(self):
        out = self.root / "out"
        self.assertEqual(extract.main(["--data", str(self.data), "--out", str(out)]), 0)
        meta = json.loads((out / "Faces" / "BIGFACES" / "01.STI" / "meta.json").read_text())
        self.assertEqual(len(meta["frames"]), 3)
        self.assertEqual(meta["frames"][2]["offsetX"], -4)
        self.assertEqual(len(meta["palette"]), 256)
        self.assertEqual(len(meta["auxObjects"]), 3)
        self.assertTrue((out / "tilesets" / "0" / "grass.sti" / "frame_000.png").is_file())

    def test_usage_and_manifest(self):
        log = self.root / "usage.jsonl"
        log.write_text('{"screen": "MAP_SCREEN", "file": "interface\\\\panel.sti", "frame": 3}\n'
                       '{"screen": "GAME_SCREEN", "file": "Interface/PANEL.STI", "frame": 9}\n')
        u = self.root / "usage.json"
        self.assertEqual(usage.main([str(log), "--out", str(u)]), 0)
        data = json.loads(u.read_text())
        self.assertEqual(data["files"]["interface/panel.sti"]["screens"], ["MAP_SCREEN", "GAME_SCREEN"])
        m_path, report = self.root / "manifest.json", self.root / "inventory.html"
        self.assertEqual(manifest.main(["--data", str(self.data), "--usage", str(u), "--manifest", str(m_path),
                                        "--report", str(report)]), 0)
        m = json.loads(m_path.read_text())
        by_path = {a["path"]: a for a in m["assets"]}
        self.assertEqual(m["summary"]["assets"], 5)
        self.assertEqual(by_path["Interface/PANEL.STI"]["method"], "design-new")
        self.assertEqual(by_path["Interface/PANEL.STI"]["usage"], ["MAP_SCREEN", "GAME_SCREEN"])
        self.assertEqual(by_path["Faces/BIGFACES/01.STI"]["method"], "repaint")
        self.assertEqual(by_path["Faces/BIGFACES/01.STI"]["frames"], 3)
        self.assertEqual(by_path["Radarmaps/A9.STI"]["method"], "regenerate")
        self.assertEqual(by_path["tilesets/0/grass.sti"]["method"], "upscale")
        self.assertIn("Faces/07.STI", report.read_text())


class SyntheticData(unittest.TestCase):
    """The synthetic Data/ tree the e2e suite boots on (issue #302)."""

    def generate(self, out: Path) -> dict:
        return synthetic.generate(out, quiet=True)

    def test_generator_is_deterministic(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self.generate(a)
            self.generate(b)
            self.assertEqual(tree_digest(a), tree_digest(b), "two generator runs differ")

    def test_images_carry_the_manifest_shape(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            summary = self.generate(out)
            manifest_data = json.loads(synthetic.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
            self.assertEqual(summary["images"], len(synthetic.used_assets(manifest_data)))
            self.assertGreaterEqual(summary["images"], 300, "the 300-image usage set")
            by_path = {a["path"].lower(): a for a in manifest_data["assets"]}
            with SlfArchive(out / "Data" / "Interface.slf") as a:
                entry = next(e for e in a.files() if e.name.upper() == "TACTPOPUP.STI")
                image = sti.parse(a.read(entry))
            real = by_path["interface/tactpopup.sti"]
            self.assertEqual(len(image.frames), real["frames"], "frame count from the manifest")
            self.assertEqual((image.width, image.height), tuple(real["size"]), "header from the manifest")
            self.assertEqual((image.frames[0].width, image.frames[0].height), synthetic.PLACEHOLDER_FRAME)

    def test_whole_picture_images_are_whole(self):
        """The loading screens are read by AddVideoSurfaceFromFile, which copies width*height*depth/8
        bytes of the file's pixel data straight into a surface, so the file has to hold exactly that
        many and not be a compressed sheet: an ETRLE placeholder made the copy read past the end of
        its buffer (issue #302, a heap-buffer-overflow under ASan in CI)."""
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            self.generate(out)
            manifest_data = json.loads(synthetic.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
            by_path = {a["path"].lower(): a for a in manifest_data["assets"]}
            with SlfArchive(out / "Data" / "Loadscreens.slf") as a:
                entries = {e.name.lower(): e for e in a.files()}
                for path in sorted(synthetic.WHOLE_PICTURES):
                    raw = a.read(entries[path.rsplit("/", 1)[1]])
                    image = sti.parse(raw)
                    size = tuple(by_path[path]["size"])
                    self.assertFalse(image.flags & sti.ETRLE_COMPRESSED, path)
                    self.assertEqual((image.width, image.height), size, path)
                    self.assertEqual([(f.width, f.height) for f in image.frames], [size], path)
                    self.assertEqual(struct.unpack_from("<I", raw, 8)[0], size[0] * size[1] * 2, path)

    def test_fonts_have_a_glyph_per_translation_entry(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            self.generate(out)
            with SlfArchive(out / "Data" / "Fonts.slf") as a:
                self.assertEqual(sorted(e.name for e in a.files()), sorted(synthetic.FONT_FILES))
                font = sti.parse(a.read(a.entries[0]))
            self.assertGreater(len(font.frames), 187, "every codepoint the table names has a glyph")
            self.assertGreater(font.frames[0].width + font.frames[0].offset_x, 0, "text advances")

    def test_prof_dat_decodes_to_named_profiles(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            self.generate(out)
            with SlfArchive(out / "Data" / "Binarydata.slf") as a:
                entry = next(e for e in a.files() if e.name.lower() == "prof.dat")
                raw = a.read(entry)
            self.assertEqual(len(raw), synthetic.PROFILES * synthetic.PROFILE_SIZE)
            names = []
            for pid in range(2):
                block = raw[pid * synthetic.PROFILE_SIZE:(pid + 1) * synthetic.PROFILE_SIZE]
                plain = bytearray(len(block))
                last = 0
                for i, b in enumerate(block):     # JA2EncryptedFileRead
                    plain[i] = (b - last - synthetic.ROTATION[i % len(synthetic.ROTATION)]) & 0xFF
                    last = b
                names.append(plain[:synthetic.NAME_LENGTH * 2].decode("utf-16-le").split("\0", 1)[0])
            self.assertEqual(names, ["Barry", "Blood"])

    def test_ja2set_dat_passes_the_loader_checks(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            self.generate(out)
            with SlfArchive(out / "Data" / "Binarydata.slf") as a:
                entry = next(e for e in a.files() if e.name.lower() == "ja2set.dat")
                raw = a.read(entry)
            self.assertEqual(raw[0], 50)                       # VANILLA_NUM_TILESETS
            self.assertEqual(struct.unpack_from("<I", raw, 1)[0], 151)   # NUMBEROFTILETYPES

    def test_marker_and_non_image_list(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "data"
            self.generate(out)
            data = out / "Data"
            self.assertTrue((data / synthetic.SYNTHETIC_MARKER).is_file())
            present = {f.relative_to(data).as_posix().lower() for f in data.rglob("*") if f.is_file()}
            for slf in sorted(data.glob("*.slf")):
                with SlfArchive(slf) as a:
                    present.update(a.full_path(e).lower() for e in a.files())
            for path in synthetic.all_non_image_files():
                self.assertIn(path.lower(), present, path)


class Classify(unittest.TestCase):
    def test_rules(self):
        cases = {
            "Fonts/LARGEFONT1.STI": "replace", "Interface/COMPFONT.STI": "replace",
            "Cursors/CURSOR.STI": "design-new", "Interface/BUTTONS.STI": "design-new",
            "Laptop/EMAILVIEWER.STI": "design-new", "Laptop/FLOWER_3.STI": "upscale",
            "Radarmaps/B13.STI": "regenerate", "Interface/B_MAP.PCX": "regenerate",
            "Faces/BIGFACES/12.STI": "repaint", "Faces/33FACE/12.STI": "upscale",
            "Interface/MDGUNS.STI": "upscale", "Bigitems/GUN01.STI": "upscale",
            "Anims/S_MERC/S_WALK.STI": "upscale", "Tilesets/0/GRASS.STI": "upscale",
            "Loadscreens/LS_DAYGENERIC.STI": "upscale", "Intro/INTRO.SMK": "upscale",
        }
        for path, method in cases.items():
            self.assertEqual(classify(path)["method"], method, path)


class Gates(unittest.TestCase):
    def sprite(self) -> png.Image:
        s = sti.parse(make_sti(1))
        return s.frame_rgba(0)

    def test_baseline_passes(self):
        o = self.sprite()
        c = baseline.nearest(o, 4)
        results = gates.run_all(o, c)
        self.assertTrue(all(r.passed for r in results), [r.as_dict() for r in results])

    def test_wrong_scale_and_shifted_silhouette_fail(self):
        o = self.sprite()
        c = baseline.nearest(o, 4)
        odd = png.Image.blank(c.width + 1, c.height)
        self.assertFalse(gates.scale_multiple(o, odd).passed)
        shifted = png.Image.blank(c.width, c.height)
        shift = 12
        for y in range(c.height):
            for x in range(c.width - shift):
                shifted.set_pixel(x + shift, y, c.pixel(x, y))
        self.assertFalse(gates.silhouette_match(o, shifted).passed)

    def test_colour_drift(self):
        o = self.sprite()
        c = baseline.nearest(o, 2)
        for i in range(0, len(c.rgba), 4):
            c.rgba[i] = min(255, c.rgba[i] + 60)
        self.assertFalse(gates.colour_drift(o, c).passed)

    def test_frame_stability(self):
        s = sti.parse(make_sti(3))
        frames = [s.frame_rgba(i) for i in range(3)]
        good = [baseline.nearest(f, 2) for f in frames]
        self.assertTrue(gates.frame_stability(frames, good).passed)
        wobbly = [good[0], good[1], png.Image.blank(good[2].width, good[2].height)]
        wobbly[2].set_pixel(0, 0, (255, 255, 255, 255))
        self.assertFalse(gates.frame_stability(frames, wobbly).passed)

    def test_tile_seam(self):
        a = png.Image.blank(4, 4)
        b = png.Image.blank(4, 4)
        for y in range(4):
            a.set_pixel(3, y, (100, 100, 100, 255))
            b.set_pixel(0, y, (104, 100, 100, 255))
        self.assertTrue(gates.tile_seams(a, b).passed)
        for y in range(4):
            b.set_pixel(0, y, (250, 10, 10, 255))
        self.assertFalse(gates.tile_seams(a, b).passed)


if __name__ == "__main__":
    unittest.main()
