import argparse
import sys
from pathlib import Path

import bpy


BLENDER_VERSION = (5, 2, 2)
FILE_HEADER = b"BLENDER17-01v0502"
DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[2]
    / "plugins/usdBlendFileFormat/tests/fixtures/empty.blend"
)


def validate_empty_scene():
    for name in (
        "objects", "meshes", "cameras", "lights", "materials", "collections",
        "libraries", "actions",
    ):
        if len(getattr(bpy.data, name)) != 0:
            raise RuntimeError(f"Empty fixture contains {name}")
    if len(bpy.data.scenes) != 1 or bpy.context.scene.name != "Scene":
        raise RuntimeError("Empty fixture must contain exactly one scene named Scene")
    if bpy.context.scene.unit_settings.scale_length != 1.0:
        raise RuntimeError("Empty fixture must use a unit scale of 1")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != BLENDER_VERSION:
        raise RuntimeError(
            f"Use Blender {'.'.join(map(str, BLENDER_VERSION))}, "
            f"not {bpy.app.version_string}"
        )
    output = options.output.resolve()

    if not options.check:
        output.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.preferences.filepaths.save_version = 0
        validate_empty_scene()
        result = bpy.ops.wm.save_as_mainfile(
            filepath=str(output), check_existing=False, compress=False
        )
        if result != {"FINISHED"}:
            raise RuntimeError(f"Failed to save fixture: {result}")

    with output.open("rb") as fixture:
        if fixture.read(len(FILE_HEADER)) != FILE_HEADER:
            raise RuntimeError("Fixture must be an uncompressed Blender 5.2 format-1 file")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    validate_empty_scene()
    print(f"Verified empty fixture with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()