#!/usr/bin/env python3
"""Standalone UF2 CLI regression checks; a recording backend replaces picotool."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
flash = root / "out/bin/flash"
if not flash.is_file():
    raise SystemExit("Build tools/flash on the host lane before running this test")

with tempfile.TemporaryDirectory(prefix="mm-flash-image-") as directory:
    work = Path(directory)
    package = work / "tool path ' quoted"
    package.mkdir()
    backend = package / "picotool"
    backend.write_text("""#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
Path(os.environ['MM_TEST_FLASH_TRACE']).write_text(json.dumps(sys.argv[1:]))
raise SystemExit(int(os.environ.get('MM_TEST_FLASH_EXIT', '0')))
""")
    backend.chmod(0o755)
    image = work / "image; $literal ' quoted.uf2"
    image.write_bytes(b"backend fixture")
    trace = work / "trace.json"
    env = {**os.environ, "picotool_DIR": str(package), "MM_TEST_FLASH_TRACE": str(trace)}

    def run(args, expected, environment=env):
        trace.unlink(missing_ok=True)
        result = subprocess.run([str(flash), *args], cwd=work, env=environment,
                                text=True, capture_output=True)
        assert result.returncode == expected, (args, result.returncode, result.stderr)
        return result

    # No configuration or manifest in this cwd. Shell metacharacters remain literal.
    run(["--image", str(image)], 0)
    assert json.loads(trace.read_text()) == ["load", "-v", "-x", str(image)]
    run(["--auto-flash", "--image", str(image)], 0)
    assert json.loads(trace.read_text()) == ["load", "-f", "-v", "-x", str(image)]
    run(["--image", str(image)], 23, {**env, "MM_TEST_FLASH_EXIT": "23"})
    assert trace.exists()

    empty = work / "empty.uf2"
    empty.touch()
    invalid = [
        (["--image"], 64),
        (["--image", ""], 64),
        (["--image", str(image), "apps/camera-demo"], 64),
        (["--image", str(image), "--image", str(image)], 64),
        (["--image", "wrong.bin"], 64),
        (["--image", str(work / "missing.uf2")], 127),
        (["--image", str(empty)], 127),
    ]
    for arguments, expected in invalid:
        run(arguments, expected)
        assert not trace.exists(), arguments
    run(["--image", str(image)], 127, {k: v for k, v in env.items() if k != "picotool_DIR"})
    assert not trace.exists()
    run(["--image", str(image)], 127, {**env, "picotool_DIR": str(work / "absent")})
    assert not trace.exists()
    run(["--help"], 0)
    assert not trace.exists()
print("PASS: standalone UF2 flashing, literal paths, reboot flag, validation, and exit status")
