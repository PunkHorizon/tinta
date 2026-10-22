"""Check Russian key coverage and printf compatibility with the English UI."""
import ast
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src/i18n.cpp").read_text(encoding="utf-8")
table = source.split("const Entry kEntries[] = {", 1)[1].split("\n};", 1)[0]
pattern = r'\{\s*"([^"]+)"\s*,\s*L"((?:\\.|[^"\\])*)"'
english_rows = re.findall(pattern, table)
russian_rows = re.findall(pattern, (root / "src/i18n_ru.inc").read_text(encoding="ascii"))
english = dict(english_rows)
russian = dict(russian_rows)
assert len(russian) == len(russian_rows), "Duplicate Russian keys"
optional_keys = {"settings.keep_deleted_tabs", "settings.keep_deleted_tabs.hint"}
assert english.keys() <= russian.keys() and russian.keys() - english.keys() <= optional_keys, (
    f"Missing: {english.keys() - russian.keys()}; unknown: {russian.keys() - english.keys()}"
)
placeholder = re.compile(r"%(?:\d+\$)?(?:\.[0-9]+)?(?:zu|ls|[sdf])")
for key, value in russian.items():
    translated = ast.literal_eval('"' + value + '"')
    original = ast.literal_eval('"' + english.get(key, "") + '"')
    assert translated.strip(), f"Empty translation: {key}"
    assert placeholder.findall(translated) == placeholder.findall(original), key
print(f"Russian: {len(russian)} keys, complete coverage, printf arguments preserved")
