#pragma once

#include <array>

namespace Nightmare
{
constexpr std::array<const char *, 7> kActions = {
    "ir_send",
    "ir_learn",
    "ac_set_power",
    "ac_set_mode",
    "ac_set_temperature",
    "ac_set_target_temperature",
    "ac_set_target"
};

constexpr std::array<const char *, 2> kSensors = {
    "temperature",
    "power"
};

constexpr char kActionsJson[] = R"json(\n[
  {
    "name": "ir_send",
    "description": "Send an IR command.",
    "parameters": [
      {
        "name": "code",
        "type": "string",
        "description": "The IR code to send, in hexadecimal format."
      }
    ]
  },
  {
    "name": "ir_learn",
    "description": "Learn an IR command and return its code.",
    "parameters": []
  },
  {
    "name": "ac_set_power",
    "description": "Turn on/turn off the AC unit.",
    "parameters": [
      {
        "name": "power",
        "type": "int",
        "description": "1 for ON, 0 for OFF. 2 for toggle."
      }
    ]
  },
  {
    "name": "ac_set_mode",
    "description": "Set the AC mode.",
    "parameters": [
      {
        "name": "mode",
        "type": "int",
        "description": "0 for Auto, 1 for Cool, 2 for Dry, 3 for Fan, 4 for Heat."
      }
    ]
  },
  {
    "name": "ac_set_temperature",
    "description": "Set the AC temperature.",
    "parameters": [
      {
        "name": "temperature",
        "type": "int",
        "description": "The target temperature in Celsius (18-30)."
      }
    ]
  },
  {
    "name": "ac_set_target_temperature",
    "description": "Set the AC target temperature.",
    "parameters": [
      {
        "name": "temperature",
        "type": "float",
        "description": "The target temperature in Celsius (18-30)."
      }
    ]
  },
  {
    "name": "ac_set_target",
    "description": "turns on/off the controlling the room temperature.",
    "parameters": [
      {
        "name": "state",
        "type": "int",
        "description": "1 for ON, 0 for OFF."
      }
    ]
  }
]\n)json";

constexpr char kSensorsJson[] = R"json(\n[
  {
    "name": "temperature",
    "description": "Current temperature in Celsius.",
    "type": "float"
  },
  {
    "name": "power",
    "description": "Current power consumption in watts.",
    "type": "float"
  }
]\n)json";

constexpr char kDescriptionJson[] = R"json(\n{
  "actions": [
    {
      "name": "ir_send",
      "description": "Send an IR command.",
      "parameters": [
        {
          "name": "code",
          "type": "string",
          "description": "The IR code to send, in hexadecimal format."
        }
      ]
    },
    {
      "name": "ir_learn",
      "description": "Learn an IR command and return its code.",
      "parameters": []
    },
    {
      "name": "ac_set_power",
      "description": "Turn on/turn off the AC unit.",
      "parameters": [
        {
          "name": "power",
          "type": "int",
          "description": "1 for ON, 0 for OFF. 2 for toggle."
        }
      ]
    },
    {
      "name": "ac_set_mode",
      "description": "Set the AC mode.",
      "parameters": [
        {
          "name": "mode",
          "type": "int",
          "description": "0 for Auto, 1 for Cool, 2 for Dry, 3 for Fan, 4 for Heat."
        }
      ]
    },
    {
      "name": "ac_set_temperature",
      "description": "Set the AC temperature.",
      "parameters": [
        {
          "name": "temperature",
          "type": "int",
          "description": "The target temperature in Celsius (18-30)."
        }
      ]
    },
    {
      "name": "ac_set_target_temperature",
      "description": "Set the AC target temperature.",
      "parameters": [
        {
          "name": "temperature",
          "type": "float",
          "description": "The target temperature in Celsius (18-30)."
        }
      ]
    },
    {
      "name": "ac_set_target",
      "description": "turns on/off the controlling the room temperature.",
      "parameters": [
        {
          "name": "state",
          "type": "int",
          "description": "1 for ON, 0 for OFF."
        }
      ]
    }
  ],
  "sensors": [
    {
      "name": "temperature",
      "description": "Current temperature in Celsius.",
      "type": "float"
    },
    {
      "name": "power",
      "description": "Current power consumption in watts.",
      "type": "float"
    }
  ]
}\n)json";
} // namespace Nightmare
