#!/usr/bin/env python3
"""Regression guards for safe machine-info and os-release parsing/writing."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def function_body(signature, next_marker):
    start = source.index(signature)
    end = source.index(next_marker, start)
    return source[start:end]


parser = function_body(
    "parse_key_value_line (const gchar *line, const gchar *key)",
    "/* Read a KEY=value file",
)
for fragment, message in [
    ("g_ascii_isalnum", "assignment keys must be parsed explicitly"),
    ("*p != '='", "only KEY=value assignments should match"),
    ("quote != '\\0'", "quoted and unquoted values need distinct parsing"),
    ("if (!closed)", "unterminated quoted values must be rejected"),
    ("goto malformed", "malformed assignments must be ignored"),
    ("*p == '#'", "comments must not become part of values"),
]:
    require(fragment in parser, message)

reader = function_body(
    "read_key_value_file (const gchar *path, const gchar *key)",
    "/* Read a DMI sysfs file",
)
require("parse_key_value_line (lines[i], key)" in reader,
        "file reads must use the shared assignment parser")
require("g_free (result);\n      result = value;" in reader,
        "the last valid duplicate key must win")
require("g_file_get_contents" in reader,
        "configuration files must be read as data, not executed")
require("g_shell_parse_argv" not in source and "g_spawn" not in source,
        "configuration parsing must never invoke a shell")

writer = function_body(
    "write_machine_info_key (const gchar *key, const gchar *value, GError **error)",
    "/* --------------------------------------------------------------------------\n   Input validation",
)
require("parse_key_value_line (lines[i], key)" in writer,
        "updates must identify existing assignments with the shared parser")
require("Collapse duplicate valid assignments" in writer,
        "updates must collapse duplicate valid assignments")
require("Preserve comments, malformed lines, and unrelated entries" in writer,
        "updates must preserve unrelated file contents")
require("rcl_machine_info_quote_value (value)" in writer,
        "updates must serialize values with escaping")
require("atomic_replace_file_mode" in writer,
        "updates must retain atomic replacement")

quote = function_body(
    "rcl_machine_info_quote_value (const gchar *value)",
    "/* --------------------------------------------------------------------------\n   JSON string escaping",
)
require("*p == '\\\\' || *p == '\"'" in quote,
        "machine-info serialization must escape backslashes and quotes")

validator = function_body(
    "rcl_machine_info_value_is_safe (const gchar *value)",
    "/* Serialize a value as a machine-info",
)
require("*p < 0x20 || *p == 0x7f" in validator,
        "control characters must be rejected")
require("g_utf8_validate" in validator,
        "machine-info values must be valid UTF-8")

print("configuration parsing and writing invariants: PASS")
