import json
from pathlib import Path

try:
	Import("env")  # type: ignore[name-defined]
except NameError:
	env = None


def _to_cpp_string_array(var_name: str, values):
	lines = [f"constexpr std::array<const char *, {len(values)}> {var_name} = {{"]
	for idx, value in enumerate(values):
		suffix = "," if idx < len(values) - 1 else ""
		lines.append(f"    \"{value}\"{suffix}")
	lines.append("};")
	return "\n".join(lines)


def _to_cpp_raw_json(var_name: str, value):
	json_text = json.dumps(value, indent=2, ensure_ascii=True)
	return f"constexpr char {var_name}[] = R\"json(\\n{json_text}\\n)json\";"


def generate_nightmare_header():
	if env is not None:
		project_dir = Path(env.subst("$PROJECT_DIR"))
		include_dir = Path(env.subst("$PROJECT_INCLUDE_DIR"))
	else:
		project_dir = Path(__file__).resolve().parents[2]
		include_dir = project_dir / "include"
	description_path = project_dir / "src" / "description.json"
	header_path = include_dir / "Nightmare.h"

	if not description_path.exists():
		print(f"[Nightmare] description.json not found: {description_path}")
		return

	include_dir.mkdir(parents=True, exist_ok=True)

	with description_path.open("r", encoding="utf-8") as file:
		description = json.load(file)

	actions = description.get("actions", [])
	sensors = description.get("sensors", [])
	action_names = [item.get("name", "") for item in actions]
	sensor_names = [item.get("name", "") for item in sensors]

	header_lines = [
		"#pragma once",
		"",
		"#include <array>",
		"",
		"namespace Nightmare",
		"{",
		_to_cpp_string_array("kActions", action_names),
		"",
		_to_cpp_string_array("kSensors", sensor_names),
		"",
		_to_cpp_raw_json("kActionsJson", actions),
		"",
		_to_cpp_raw_json("kSensorsJson", sensors),
		"",
		_to_cpp_raw_json("kDescriptionJson", description),
		"} // namespace Nightmare",
		"",
	]

	header_path.write_text("\n".join(header_lines), encoding="utf-8")
	print(f"[Nightmare] Generated header: {header_path}")


generate_nightmare_header()
