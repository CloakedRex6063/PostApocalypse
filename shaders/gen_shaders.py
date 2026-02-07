import subprocess
from pathlib import Path
import re

SLANG_BINARY = (
        Path(__file__).resolve().parent.parent
        / "engine"
        / "extern"
        / "slangc"
        / "slangc"
)

ROOT_DIR = Path(__file__).parent
OUT_FILE = Path(__file__).parent.parent / "engine" / "inc" / "shader_data.hpp"

SLANG_TARGET = "dxil"

SHADER_STAGE_MAP = {
    "compute": "cs",
    "vertex": "vs",
    "pixel": "ps",
    "geometry": "gs",
    "hull": "hs",
    "domain": "ds",
    "mesh": "ms",
    "amplification": "as",
}

def detect_shader_stages(shader_path: Path) -> list[tuple[str, str]]:
    text = shader_path.read_text(encoding="utf-8", errors="ignore")

    pattern = r'\[shader\s*\(\s*"(\w+)"\s*\)\]\s*\n\s*(?:export\s+)?[\w\d_]+\s+(\w+)\s*\('
    matches = re.finditer(pattern, text)

    stages = []
    for match in matches:
        stage_name = match.group(1).lower()
        entry_point = match.group(2)

        if stage_name not in SHADER_STAGE_MAP:
            print(f"Warning: Unsupported shader stage '{stage_name}' in {shader_path}, skipping")
            continue

        stages.append((entry_point, SHADER_STAGE_MAP[stage_name]))

    if not stages:
        raise RuntimeError(
            f"No [shader(\"...\")] attributes found in {shader_path}"
        )

    return stages

def compile_to_dxil(shader_path: Path, entry_point: str, stage: str) -> bytes:
    profile = f"{stage}_6_6"
    dxil_path = shader_path.with_suffix(f".{entry_point}.dxil.tmp")

    cmd = [
        str(SLANG_BINARY),
        str(shader_path),
        "-entry", entry_point,
        "-target", "dxil",
        "-profile", profile,
        "-o", str(dxil_path),
    ]

    print(f"Compiling: {shader_path} [{entry_point} - {stage}]")
    subprocess.run(cmd, check=True)

    data = dxil_path.read_bytes()
    dxil_path.unlink(missing_ok=True)
    return data

def main():
    arrays = []

    for file in ROOT_DIR.rglob("*"):
        if not file.is_file():
            continue

        if file.suffix == ".py":
            continue

        base_name = file.stem.replace("-", "_")

        try:
            stages = detect_shader_stages(file)
        except RuntimeError as e:
            print(f"Skipping {file}: {e}")
            continue

        for entry_point, stage in stages:
            try:
                bytecode = compile_to_dxil(file, entry_point, stage)
            except subprocess.CalledProcessError as e:
                print(f"Failed: {file} [{entry_point}]: {e}")
                continue

            size = len(bytecode)
            bytes_cpp = ", ".join(f"0x{b:02x}" for b in bytecode)

            var_name = f"{base_name}_{entry_point}_code"

            arrays.append(
                f"inline constexpr std::array<uint8_t, {size}> {var_name} = {{\n"
                f"  {bytes_cpp}\n"
                f"}};"
            )

    OUT_FILE.write_text(
        "#pragma once\n"
        + "\n\n".join(arrays)
    )

    print(f"Written: {OUT_FILE}")

if __name__ == "__main__":
    main()