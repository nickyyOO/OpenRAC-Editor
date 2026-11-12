#!/bin/sh
# Runs the cutscene module's tests in the built editor, without a window:
#   modules/openrac_cutscene/tests/run_tests.sh [path to the editor binary]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
editor=${1:-"$root/bin/godot.linuxbsd.editor.x86_64"}
[ -x "$editor" ] || { echo "no editor at $editor: build it first"; exit 2; }
project=$(mktemp -d)
trap 'rm -rf "$project"' EXIT
printf 'config_version=5\n[application]\nconfig/name="cutscene tests"\n' > "$project/project.godot"
status=0
for test in "$here"/*_test.gd; do
	cp "$test" "$project/"
	echo "== $(basename "$test")"
	log="$project/$(basename "$test").log"
	"$editor" --headless --path "$project" --script "res://$(basename "$test")" >"$log" 2>&1 || status=1
	grep -v -e '^$' -e 'PhysicsServer' -e 'initialize_physics' "$log" || true
	# An error of the engine or the script is a failure too, though the checks went on; the
	# errors the tests ask for are listed in expected_errors.txt.
	if grep -e '^ERROR:' -e '^SCRIPT ERROR:' "$log" | grep -q -v -F -f "$here/expected_errors.txt"; then
		echo "FAILED: errors while running $(basename "$test")"
		status=1
	fi
done
exit $status
