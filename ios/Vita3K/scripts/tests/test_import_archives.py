import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile

binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="vita3k-import-") as folder:
    root = pathlib.Path(folder)
    def check(name, payload, success):
        source = root / (name + ".zip")
        source.write_bytes(payload)
        destination = root / name
        subprocess.run([str(binary), str(source), str(destination), "ok" if success else "fail"], check=True)
        return destination

    for method in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED]:
        source = root / "source.zip"
        with zipfile.ZipFile(source, "w", compression=method) as archive:
            archive.writestr("wrapper/sce_sys/", b"")
            archive.writestr("wrapper/eboot.bin", b"game data" * 10000)
            archive.writestr("wrapper/sce_sys/param.sfo", b"metadata")
        valid = source.read_bytes()
        destination = check("valid-" + str(method), valid, True)
        assert (destination / "wrapper/eboot.bin").read_bytes() == b"game data" * 10000
        check("truncated-" + str(method), valid[:-10], False)
        corrupt = bytearray(valid)
        central = corrupt.index(b"PK\x01\x02", corrupt.index(b"PK\x01\x02") + 1)
        struct.pack_into("<I", corrupt, central + 16, 0x12345678)  # bad CRC
        check("crc-" + str(method), corrupt, False)
        corrupt = bytearray(valid)
        struct.pack_into("<I", corrupt, central + 24, 1)  # stored size mismatch / short inflate
        check("size-" + str(method), corrupt, False)

    for index, name in enumerate(["../escape", "/absolute", "a/../../escape", "C:/escape", "a\\..\\escape"]):
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr(name, b"bad")
        check("path-" + str(index), source.read_bytes(), False)
    assert not (root / "escape").exists()
    check("not-zip", b"invalid archive", False)
    with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_BZIP2) as archive:
        archive.writestr("eboot.bin", b"data")
    check("unsupported", source.read_bytes(), False)
    # A large decompressed entry exercises streaming without holding it in RAM.
    with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        with archive.open("large.bin", "w", force_zip64=True) as entry:
            for _ in range(192):
                entry.write(b"x" * (1024 * 1024))
    destination = check("large-zip64", source.read_bytes(), True)
    assert (destination / "large.bin").stat().st_size == 192 * 1024 * 1024
print("Archive + SFO regression tests passed")
