"""Compare actual native draws against externally supplied reference shaders.

This verifies the opaque material on one supplied mesh/vertex-parameter fixture,
not complete firmware rendering or full-screen equivalence. No original inputs
or rendered images are distributed with this test.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--vertex-shader", type=Path, required=True)
    parser.add_argument("--fragment-shader", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fragment-only", action="store_true",
                        help="Keep the reference vertex stage when isolating material changes")
    args = parser.parse_args()
    adb = [args.adb, "-s", args.serial]

    def run(*command):
        try:
            return subprocess.check_output([*adb, *map(str, command)], stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(error.output.decode("utf-8", errors="replace").strip()) from error

    remote = "/data/local/tmp/paf-material-" + uuid.uuid4().hex
    cases = []
    report = {"device": args.serial, "nativeVertex": not args.fragment_only,
              "fullScreenEquivalenceVerified": False, "cases": cases}
    with tempfile.TemporaryDirectory(prefix="paf-material-") as temporary:
        local = Path(temporary)
        capture = local / "capture"
        capture.mkdir()
        for name in ("stream-0.bin", "indices.bin", "vertex-ubo-0.bin",
                     "fragment-texture-0.bin", "fragment-texture-0-palette.bin"):
            shutil.copyfile(args.capture / name, capture / name)
        (local / "environment.rgba").write_bytes(bytes(
            channel for y in range(64) for x in range(64)
            for channel in (x * 4, y * 4, (x * 7 + y * 11) % 256, 255)))
        shutil.copyfile(args.vertex_shader, local / "vertex.vert")
        shutil.copyfile(args.fragment_shader, local / "reference.frag")
        shutil.copyfile(args.executable, local / "preview")
        parameters = [
            ("captured", (args.capture / "fragment-ubo-0.bin").read_bytes()),
            ("mixed-colors", struct.pack("10f", 1, 0, .13, .41, .79, 1, .07, .3, .91, 1)),
            ("translucent", struct.pack("10f", .3125, 0, .83, .22, .06, 1, .61, .13, .38, 1)),
        ]
        run("shell", "mkdir", remote)
        try:
            run("push", str(local) + "/.", remote)
            run("shell", "chmod", "755", remote + "/preview")
            for name, data in parameters:
                (capture / "fragment-ubo-0.bin").write_bytes(data)
                run("push", capture / "fragment-ubo-0.bin", remote + "/capture/")
                frames = []
                for shader in (remote + "/reference.frag", "--native-material"):
                    vertex = "--native-vertex" if shader == "--native-material" and not args.fragment_only else remote + "/vertex.vert"
                    log = run("shell", remote + "/preview", remote + "/capture",
                              vertex, shader, remote + "/environment.rgba",
                              remote + "/frame.rgba").decode().strip()
                    frame = run("exec-out", "cat", remote + "/frame.rgba")
                    if len(frame) != 960 * 544 * 4 or not any(frame[3::4]):
                        raise RuntimeError("Missing, truncated or empty rendered bubble")
                    frames.append(frame)
                    report["renderer"] = log
                reference, native = frames
                differences = [abs(a - b) for a, b in zip(reference, native) if a != b]
                cases.append({"name": name, "differentChannels": len(differences),
                              "maximumDelta": max(differences, default=0),
                              "referenceSha256": hashlib.sha256(reference).hexdigest(),
                              "nativeSha256": hashlib.sha256(native).hexdigest()})
                print(name, "different channels:", len(differences), flush=True)
        finally:
            run("shell", "rm", "-rf", remote)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return int(any(case["differentChannels"] for case in cases))


if __name__ == "__main__":
    raise SystemExit(main())
