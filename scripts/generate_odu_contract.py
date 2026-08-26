#!/usr/bin/env python3
"""Generate the ODU C++ contract and documentation from data/odu/*.yaml."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import yaml


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "odu"
COMPONENT = ROOT / "components" / "quatt_odu_simulator"
DOC = ROOT / "docs" / "quatt-odu-register-contract.md"
PROFILE_ORDER = ["disabled", "v1", "v1_5", "v2_old", "v2_new"]
PROFILE_BITS = {name: 1 << index for index, name in enumerate(PROFILE_ORDER)}
CPP_PROFILE = {
    "disabled": "DISABLED",
    "v1": "V1",
    "v1_5": "V1_5",
    "v2_old": "V2_OLD",
    "v2_new": "V2_NEW",
}


def load(name: str):
    with (DATA / name).open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def profile_mask(names: list[str]) -> int:
    return sum(PROFILE_BITS[name] for name in names)


def cpp_string(value: object) -> str:
    return str(value).replace("\\", "\\\\").replace('"', '\\"')


def fmt_number(value: float | int) -> str:
    if isinstance(value, int):
        return str(value)
    return f"{value:.6g}f"


def fmt_float(value: float | int) -> str:
    rendered = f"{float(value):.8g}"
    if "." not in rendered and "e" not in rendered.lower():
        rendered += ".0"
    return rendered + "f"


def padded(values: list[int], size: int = 21) -> list[int]:
    if len(values) > size:
        raise ValueError(f"table contains {len(values)} entries; maximum is {size}")
    return values + [0] * (size - len(values))


def validate(registers: dict, profiles: dict, performance: dict) -> None:
    ids = [profile["id"] for profile in profiles["profiles"]]
    if ids != PROFILE_ORDER:
        raise ValueError(f"profile order must be {PROFILE_ORDER}, got {ids}")

    exact_addresses = [item["address"] for item in registers["registers"]]
    if len(exact_addresses) != len(set(exact_addresses)):
        raise ValueError("duplicate exact register address")
    missing_runtime = sorted(set(range(2099, 2139)) - set(exact_addresses))
    if missing_runtime:
        raise ValueError(f"runtime registers are not explicit: {missing_runtime}")

    writable = {1999, 2006, 2010, 2015, 3999}
    for item in registers["registers"]:
        if "write" in item["access"] and item["address"] not in writable:
            raise ValueError(f"unexpected writable exact register {item['address']}")

    for profile in profiles["profiles"]:
        if profile["id"] == "disabled":
            continue
        expected = profile["max_physical_level"] + 1
        if len(profile["factory_cooling"]) != expected or len(profile["factory_heating"]) != expected:
            raise ValueError(f"{profile['id']} frequency table length does not match capability")
        for mode in ("factory_cooling", "factory_heating"):
            values = profile[mode]
            if values[0] != 0 or any(value < 1 or value > 120 for value in values[1:]):
                raise ValueError(f"invalid {profile['id']} {mode}")
            if any(left > right for left, right in zip(values, values[1:])):
                raise ValueError(f"non-monotone {profile['id']} {mode}")

    profile_by_id = {profile["id"]: profile for profile in profiles["profiles"]}
    for preset in profiles["presets"]:
        if "cooling" not in preset:
            continue
        if len(preset["applies_to"]) != 1:
            raise ValueError(f"runtime preset {preset['id']} must apply to exactly one profile")
        profile = profile_by_id[preset["applies_to"][0]]
        expected = profile["max_physical_level"] + 1
        if len(preset["cooling"]) != expected or len(preset["heating"]) != expected:
            raise ValueError(f"runtime preset {preset['id']} frequency table length does not match capability")

    for model_name, model in performance["models"].items():
        if len(model["frequency_hz"]) != 10:
            raise ValueError(f"{model_name} must have ten performance anchors")
        for field in ("thermal_power_w", "cop"):
            if len(model[field]) != 2 or any(len(row) != 2 for row in model[field]):
                raise ValueError(f"{model_name} {field} must be 2x2x10")
            if any(len(values) != 10 for row in model[field] for values in row):
                raise ValueError(f"{model_name} {field} must be 2x2x10")


def generate_profiles(profiles: dict) -> str:
    rows = []
    for profile in profiles["profiles"]:
        cooling = ", ".join(str(value) for value in padded(profile["factory_cooling"]))
        heating = ", ".join(str(value) for value in padded(profile["factory_heating"]))
        customer = ", ".join(f"0x{value:04X}U" for value in profile["customer_model_words"])
        rows.append(
            "    {Profile::%s, \"%s\", %s, %dU, %dU, 0x%04XU, 0x%04XU, {%s}, {%s}, {%s}},"
            % (
                CPP_PROFILE[profile["id"]],
                cpp_string(profile["label"]),
                "true" if profile["enabled"] else "false",
                profile["max_physical_level"],
                profile["compressor_code"],
                profile["pcb_program"],
                profile["control_board_item"],
                customer,
                cooling,
                heating,
            )
        )

    preset_rows = []
    for preset in profiles["presets"]:
        if "cooling" not in preset:
            continue
        cooling = ", ".join(str(value) for value in padded(preset["cooling"]))
        heating = ", ".join(str(value) for value in padded(preset["heating"]))
        preset_rows.append(
            '    {Profile::%s, "%s", "%s", {%s}, {%s}},'
            % (
                CPP_PROFILE[preset["applies_to"][0]],
                cpp_string(preset["id"]),
                cpp_string(preset["label"]),
                cooling,
                heating,
            )
        )

    legacy_heating = ", ".join(f"0x{value:04X}U" for value in profiles["legacy_extension_fixture"]["heating"])
    legacy_cooling = ", ".join(f"0x{value:04X}U" for value in profiles["legacy_extension_fixture"]["cooling"])
    return f"""// Generated by scripts/generate_odu_contract.py. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::quatt_odu_simulator {{

enum class Profile : uint8_t {{ DISABLED = 0, V1, V1_5, V2_OLD, V2_NEW }};

struct ProfileDefinition {{
  Profile profile;
  const char *label;
  bool enabled;
  uint8_t max_physical_level;
  uint16_t compressor_code;
  uint16_t pcb_program;
  uint16_t control_board_item;
  std::array<uint16_t, 2> customer_model_words;
  std::array<uint8_t, 21> factory_cooling;
  std::array<uint8_t, 21> factory_heating;
}};

struct RuntimePresetDefinition {{
  Profile profile;
  const char *id;
  const char *label;
  std::array<uint8_t, 21> cooling;
  std::array<uint8_t, 21> heating;
}};

inline constexpr std::array<ProfileDefinition, {len(rows)}> PROFILE_DEFINITIONS = {{{{
{chr(10).join(rows)}
}}}};

inline constexpr std::array<RuntimePresetDefinition, {len(preset_rows)}> RUNTIME_PRESETS = {{{{
{chr(10).join(preset_rows)}
}}}};

inline constexpr std::array<uint16_t, 10> LEGACY_HEATING_EXTENSION = {{{legacy_heating}}};
inline constexpr std::array<uint16_t, 10> LEGACY_COOLING_EXTENSION = {{{legacy_cooling}}};

inline constexpr const ProfileDefinition &profile_definition(Profile profile) {{
  const size_t index = static_cast<size_t>(profile);
  return PROFILE_DEFINITIONS[index < PROFILE_DEFINITIONS.size() ? index : 0U];
}}

inline constexpr Profile detect_profile(uint16_t compressor_code, uint16_t pcb_program, uint16_t control_board_item) {{
  for (const auto &definition : PROFILE_DEFINITIONS)
    if (definition.enabled && definition.compressor_code == compressor_code &&
        definition.pcb_program == pcb_program && definition.control_board_item == control_board_item)
      return definition.profile;
  return Profile::DISABLED;
}}

inline constexpr const RuntimePresetDefinition *runtime_modified_preset(Profile profile) {{
  for (const auto &preset : RUNTIME_PRESETS)
    if (preset.profile == profile) return &preset;
  return nullptr;
}}

}}  // namespace esphome::quatt_odu_simulator
"""


def generate_registers(registers: dict) -> str:
    defaults = registers["defaults"]
    access_map = {"read": "READ", "write": "WRITE", "read_write": "READ_WRITE"}
    confidence_map = {
        "observed": "OBSERVED",
        "documented": "DOCUMENTED",
        "controller_contract": "CONTROLLER_CONTRACT",
        "synthetic": "SYNTHETIC",
        "unknown": "UNKNOWN",
    }
    rows = []
    for item in registers["registers"]:
        profiles = item.get("profiles", defaults["profiles"])
        rows.append(
            "    {%dU, %dU, \"%s\", Access::%s, %s, %s, 0x%02XU, Confidence::%s, %s},"
            % (
                item["address"],
                item.get("width_words", defaults["width_words"]),
                cpp_string(item["name"]),
                access_map[item["access"]],
                fmt_float(item.get("scale", 1)),
                fmt_float(item.get("offset", 0)),
                profile_mask(profiles),
                confidence_map[item["confidence"]],
                "true" if item.get("reserved", False) else "false",
            )
        )
    range_rows = []
    for item in registers["ranges"]:
        range_rows.append(
            "    {%dU, %dU, \"%s\", Access::%s, 0x%02XU, Confidence::%s},"
            % (
                item["start"],
                item["end"],
                cpp_string(item["name"]),
                access_map[item["access"]],
                profile_mask(item["profiles"]),
                confidence_map[item["confidence"]],
            )
        )
    return f"""// Generated by scripts/generate_odu_contract.py. Do not edit.
#pragma once

#include <array>
#include <cstdint>

namespace esphome::quatt_odu_simulator {{

enum class Access : uint8_t {{ READ = 1, WRITE = 2, READ_WRITE = 3 }};
enum class Confidence : uint8_t {{ OBSERVED, DOCUMENTED, CONTROLLER_CONTRACT, SYNTHETIC, UNKNOWN }};

struct RegisterDescriptor {{
  uint16_t address;
  uint8_t width_words;
  const char *name;
  Access access;
  float scale;
  float offset;
  uint8_t profile_mask;
  Confidence confidence;
  bool reserved;
}};

struct RegisterRangeDescriptor {{
  uint16_t start;
  uint16_t end;
  const char *name;
  Access access;
  uint8_t profile_mask;
  Confidence confidence;
}};

inline constexpr std::array<RegisterDescriptor, {len(rows)}> REGISTER_DESCRIPTORS = {{{{
{chr(10).join(rows)}
}}}};

inline constexpr std::array<RegisterRangeDescriptor, {len(range_rows)}> REGISTER_RANGE_DESCRIPTORS = {{{{
{chr(10).join(range_rows)}
}}}};

inline constexpr bool access_readable(Access access) {{
  return access == Access::READ || access == Access::READ_WRITE;
}}
inline constexpr bool access_writable(Access access) {{
  return access == Access::WRITE || access == Access::READ_WRITE;
}}

}}  // namespace esphome::quatt_odu_simulator
"""


def generate_performance(performance: dict) -> str:
    blocks = []
    for model_name in ("v1", "v2"):
        model = performance["models"][model_name]
        values = []
        for field in ("thermal_power_w", "cop"):
            flattened = [value for amb in model[field] for supply in amb for value in supply]
            values.append(
                "inline constexpr std::array<float, 40> %s_%s = {%s};"
                % (model_name.upper(), field.upper(), ", ".join(fmt_float(value) for value in flattened))
            )
        prefix = model_name.upper()
        grid = (
            f"inline constexpr PerformanceGrid {prefix}_GRID = "
            f"{{{{{', '.join(fmt_number(value) for value in model['frequency_hz'])}}}, "
            f"{{{', '.join(fmt_number(value) for value in model['ambient_c'])}}}, "
            f"{{{', '.join(fmt_number(value) for value in model['supply_c'])}}}, "
            f"{prefix}_THERMAL_POWER_W.data(), {prefix}_COP.data()}};"
        )
        blocks.append("\n".join(values + [grid]))
    return f"""// Generated by scripts/generate_odu_contract.py. Do not edit.
#pragma once

#include <array>

namespace esphome::quatt_odu_simulator {{

struct PerformanceGrid {{
  std::array<float, 10> frequency_hz;
  std::array<float, 2> ambient_c;
  std::array<float, 2> supply_c;
  const float *thermal_power_w;
  const float *cop;
}};

{chr(10).join(blocks)}

}}  // namespace esphome::quatt_odu_simulator
"""


def generate_docs(registers: dict, profiles: dict, performance: dict) -> str:
    lines = [
        "# Quatt ODU register contract",
        "",
        "Generated from `data/odu/registers.yaml`, `data/odu/profiles.yaml` and `data/odu/performance.yaml`.",
        "Do not edit this file by hand.",
        "",
        "Modbus addresses below are zero-based. A spreadsheet address is therefore Modbus address + 1.",
        "",
        "## Exact registers",
        "",
        "| Modbus | Sheet | Name | Access | Unit | Scale | Confidence | Reserved |",
        "|---:|---:|---|---|---|---:|---|---|",
    ]
    for item in registers["registers"]:
        lines.append(
            f"| {item['address']} | {item['address'] + 1} | `{item['name']}` | {item['access']} | "
            f"{item.get('unit', '')} | {item.get('scale', 1)} | {item['confidence']} | "
            f"{'yes' if item.get('reserved') else 'no'} |"
        )
    lines.extend(["", "## Register ranges", "", "| Modbus | Sheet | Name | Access | Profiles | Confidence |", "|---|---|---|---|---|---|"])
    for item in registers["ranges"]:
        lines.append(
            f"| {item['start']}..{item['end']} | {item['start'] + 1}..{item['end'] + 1} | "
            f"`{item['name']}` | {item['access']} | {', '.join(item['profiles'])} | {item['confidence']} |"
        )
    lines.extend(["", "## Identity and frequency profiles", "", "| Profile | 2114 | 2122 | 2127 | Customer | Levels |", "|---|---:|---:|---:|---|---:|"])
    for profile in profiles["profiles"]:
        customer = "".join(chr(word >> 8) + chr(word & 0xFF) for word in profile["customer_model_words"]).strip("\x00") or "empty"
        lines.append(
            f"| {profile['label']} | `{profile['compressor_code']}` | `0x{profile['pcb_program']:04X}` | "
            f"`0x{profile['control_board_item']:04X}` | {customer} | F0..F{profile['max_physical_level']} |"
        )
    lines.extend(
        [
            "",
            "## Runtime frequency-table presets",
            "",
            "| Preset | Profiles | Cooling | Heating |",
            "|---|---|---|---|",
        ]
    )
    for preset in profiles["presets"]:
        if "cooling" not in preset:
            continue
        lines.append(
            f"| `{preset['id']}` | {', '.join(preset['applies_to'])} | "
            f"`{','.join(str(value) for value in preset['cooling'])}` | "
            f"`{','.join(str(value) for value in preset['heating'])}` |"
        )
    lines.extend(
        [
            "",
            "## Confidence",
            "",
            "- `observed`: confirmed by hardware data or a dump.",
            "- `documented`: from known ODU documentation.",
            "- `controller_contract`: required by the OpenQuatt consumer implementation.",
            "- `synthetic`: internally consistent simulator behaviour, not a hardware claim.",
            "- `unknown`: exposed only as an explicit fixture/reserved value.",
            "",
            "## Performance snapshot",
            "",
            f"The numerical subset comes from OpenQuatt revision `{performance['provenance']['revision']}`.",
            "Interpolation and dynamics are simulator-owned. Values above 90 Hz are clamped by default and marked synthetic.",
            "",
        ]
    )
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> bool:
    content = content.rstrip() + "\n"
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            print(f"out of date: {path.relative_to(ROOT)}", file=sys.stderr)
            return False
        return True
    path.write_text(content, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    registers = load("registers.yaml")
    profiles = load("profiles.yaml")
    performance = load("performance.yaml")
    validate(registers, profiles, performance)

    outputs = {
        COMPONENT / "quatt_odu_registers.generated.h": generate_registers(registers),
        COMPONENT / "quatt_odu_profiles.generated.h": generate_profiles(profiles),
        COMPONENT / "quatt_odu_performance.generated.h": generate_performance(performance),
        DOC: generate_docs(registers, profiles, performance),
    }
    ok = all(write_or_check(path, content, args.check) for path, content in outputs.items())
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
