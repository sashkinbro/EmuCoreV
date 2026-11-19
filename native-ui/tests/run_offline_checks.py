"""Run the offline PageTurn reconstruction checks that need no Android device.

    python native-ui/tests/run_offline_checks.py [OWNED_DECRYPTED_LIBPAF]

The material-order reference check always runs. When a decrypted libpaf image is
given, the draw-trace slot cross-check also runs; it compares the traced material
setter blocks with the bit-exact shape/pose/light oracles for five fixtures.
Exits nonzero on the first failure.
"""
import subprocess
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent


def run(script, *arguments):
    command = [sys.executable, str(BASE / script), *arguments]
    print("$", " ".join(command))
    result = subprocess.run(command)
    if result.returncode != 0:
        raise SystemExit("%s failed with %d" % (script, result.returncode))


def main():
    if len(sys.argv) > 2:
        raise SystemExit(__doc__)
    run("verify_page_material_reference.py")
    if len(sys.argv) == 2:
        run("verify_page_material_slots.py", sys.argv[1])
    else:
        print("SKIP draw-trace slot cross-check: no decrypted libpaf supplied")
    print("OK offline PageTurn checks passed")


if __name__ == "__main__":
    main()
