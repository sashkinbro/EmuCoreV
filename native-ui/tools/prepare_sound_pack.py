"""Prepare local WAV assets offline, preserving original sound IDs/event bindings.

Requires the standalone Android RCO exporter and a local vgmstream CLI supporting
Sony HE-VAG. Neither converter nor firmware containers are application dependencies.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
import wave


def sha(data): return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("sources", type=Path, nargs="+")
    args = parser.parse_args()
    target = args.pack.resolve() / "audio"
    if target.exists():
        raise ValueError("Sound pack output must be new")
    sources = sorted(args.sources, key=lambda p: p.name)
    if len({p.stem for p in sources}) != len(sources):
        raise ValueError("Sound source names must be unique")
    adb = [args.adb, "-s", args.serial]
    remote = "/data/local/tmp/paf-sound-pack-" + uuid.uuid4().hex

    def run(command):
        try:
            return subprocess.check_output(list(map(str, command)), stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(error.output.decode(errors="replace")) from error

    manifest = {"version": 1, "runtimeSource": "bundled PCM16 WAV assets", "sources": [], "sounds": []}
    with tempfile.TemporaryDirectory(prefix="native-sound-pack-") as temporary:
        temporary = Path(temporary)
        ready = temporary / "ready"
        ready.mkdir()
        run([*adb, "shell", "mkdir", remote])
        try:
            run([*adb, "push", args.exporter, remote + "/exporter"])
            run([*adb, "shell", "chmod", "755", remote + "/exporter"])
            for index, source in enumerate(sources):
                run([*adb, "push", source, remote + "/input.rco"])
                metadata = json.loads(run([*adb, "shell", remote + "/exporter", remote + "/input.rco", remote + f"/group{index}"]))
                local = temporary / str(index)
                run([*adb, "pull", remote + f"/group{index}", local])
                manifest["sources"].append({"name": source.name, "sha256": sha(source.read_bytes()),
                    "sounds": len(metadata["sounds"]), "bindings": metadata["bindings"]})
                for sound in metadata["sounds"]:
                    channels = sound["channels"]
                    if set(channels) == {"src"}: order = ["src"]
                    elif set(channels) == {"left", "right"}: order = ["left", "right"]
                    else: raise ValueError("Unsupported sound channel arrangement")
                    decoded = []
                    references = []
                    for channel in order:
                        original = local / channels[channel]
                        info = json.loads(run([args.decoder, "-m", "-I", original]))
                        if info["channels"] != 1 or info["sampleRate"] != sound["rate"]:
                            raise ValueError("Sound rate/channel metadata differs from RCO")
                        destination = original.with_suffix(".wav")
                        run([args.decoder, "-i", "-o", destination, original])
                        with wave.open(str(destination), "rb") as wav:
                            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (
                                1, 2, sound["rate"], info["numberOfSamples"]):
                                raise ValueError("Converted sound is incomplete or resampled")
                            pcm = wav.readframes(wav.getnframes())
                        decoded.append(pcm)
                        references.append({"channel": channel, "sha256": sha(original.read_bytes()),
                            "pcmSha256": sha(pcm), "metadata": info})
                    if any(len(pcm) != len(decoded[0]) for pcm in decoded):
                        raise ValueError("Stereo channels have different sample counts")
                    if len(order) == 1:
                        pcm = decoded[0]
                    else:
                        pcm = bytearray(len(decoded[0]) * 2)
                        for channel, mono in enumerate(decoded):
                            pcm[channel * 2::4] = mono[0::2]
                            pcm[channel * 2 + 1::4] = mono[1::2]
                    path = ready / source.stem / (sound["id"] + ".wav")
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with wave.open(str(path), "wb") as wav:
                        wav.setparams((len(order), 2, sound["rate"], 0, "NONE", "not compressed"))
                        wav.writeframes(pcm)
                    with wave.open(str(path), "rb") as wav:
                        restored = wav.readframes(wav.getnframes())
                        if restored != pcm:
                            raise ValueError("Final WAV differs from decoded channels")
                        if len(order) == 2 and any(restored[channel * 2::4] != decoded[channel][0::2]
                            or restored[channel * 2 + 1::4] != decoded[channel][1::2] for channel in range(2)):
                            raise ValueError("Stereo channel order changed")
                    manifest["sounds"].append({"path": path.relative_to(ready).as_posix(),
                        "resourceId": sound["id"], "rate": sound["rate"], "channels": len(order),
                        "samples": len(decoded[0]) // 2, "pcmSha256": sha(pcm),
                        "wavSha256": sha(path.read_bytes()), "originalChannels": references})
                print(source.name, len(metadata["sounds"]), "sounds", flush=True)
        finally:
            run([*adb, "shell", "rm", "-rf", remote])
        (ready / "sounds.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(ready, target)
        print("Prepared", len(manifest["sounds"]), "WAV assets; runtime firmware decoding is not needed")


if __name__ == "__main__": main()
