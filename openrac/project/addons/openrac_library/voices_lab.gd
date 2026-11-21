@tool
extends RefCounted
## The Voices dock's helper process: openrac/tools/voice_lab.py serve, run with the launcher's
## Python. The dock sends it one JSON command a line and reads its answers and progress lines
## (voice_lab.py's RESULT and PROGRESS prefixes) on a thread of its own, so the editor never waits.
## poll() hands what arrived to the editor's thread.

const RESULT: String = "openrac-voices: "
const PROGRESS: String = "openrac-voices-progress: "
## Lines the process printed for people, kept to say why it stopped.
const KEPT_LINES: int = 30

var pipe: Dictionary = {}
var reader: Thread
var error_reader: Thread
var lock := Mutex.new()
## What arrived, oldest first: {"kind": "answer" | "progress" | "stopped", ...}.
var inbox: Array = []
var last_lines: PackedStringArray = []
var next_id: int = 0
## Each command waiting for its answer, by id: [command, Callable(ok: bool, result: Variant, error: String)].
var pending: Dictionary = {}
var stopped_told: bool = false


## The command line for the process: voice_lab.py with the folders the launcher named.
static func arguments(tools: String, data: String, overrides: String, game: String, version: String) -> PackedStringArray:
	var args := PackedStringArray([tools.path_join("voice_lab.py"), "--data", data])

	if overrides != "":
		args.append_array(["--overrides", overrides])

	if game != "" and version != "":
		args.append_array(["--game", game, "--version", version])

	args.append("serve")
	return args


static func quiet_json(text: String) -> Variant:
	var json := JSON.new()
	return json.data if json.parse(text) == OK else null


## One line of the process, as what it says: an answer, a progress line, or words for people.
static func parse_line(line: String) -> Dictionary:
	if line.begins_with(RESULT):
		var value: Variant = quiet_json(line.substr(RESULT.length()))

		if value is Dictionary:
			return {"kind": "answer", "id": int(value.get("id", -1)) if value.get("id") != null else -1,
				"ok": bool(value.get("ok", false)), "result": value.get("result"),
				"error": String(value.get("error", "")) if value.get("error") != null else ""}

	if line.begins_with(PROGRESS):
		var value: Variant = quiet_json(line.substr(PROGRESS.length()))

		if value is Dictionary:
			return {"kind": "progress", "id": int(value.get("id", -1)) if value.get("id") != null else -1,
				"stage": String(value.get("stage", "")), "done": int(value.get("done", 0)),
				"total": int(value.get("total", 0)), "text": String(value.get("text", ""))}

	return {"kind": "line", "text": line}


## Starts the process; an empty string, or what went wrong.
func start(python: String, args: PackedStringArray) -> String:
	stop()
	stopped_told = false
	last_lines = PackedStringArray()

	if python == "" or not FileAccess.file_exists(args[0]):
		return "Open the level from the launcher (Mods, Level editor, Open) first."

	pipe = OS.execute_with_pipe(python, args)

	if pipe.is_empty():
		return "Couldn't start %s." % python

	reader = Thread.new()
	reader.start(read.bind(pipe["stdio"], false))
	error_reader = Thread.new()
	error_reader.start(read.bind(pipe["stderr"], true))
	return ""


func running() -> bool:
	return not pipe.is_empty() and OS.is_process_running(int(pipe["pid"]))


## Sends COMMAND with ARGS; DONE gets the answer on the editor's thread (poll()). Returns its id.
func send(command: String, args: Dictionary, done: Callable) -> int:
	next_id += 1
	pending[next_id] = [command, done]

	if not running():
		pending.erase(next_id)
		done.call(false, null, "The voice tools are not running.")
		return next_id

	var stdio: FileAccess = pipe["stdio"]
	stdio.store_line(JSON.stringify({"id": next_id, "command": command, "args": args}))
	stdio.flush()
	return next_id


## Whether a command is waiting for its answer.
func busy() -> bool:
	return not pending.is_empty()


## On a reader's thread: each line of the process, until it ends.
func read(file: FileAccess, errors: bool) -> void:
	while file.is_open():
		var line: String = file.get_line()

		if file.get_error() != OK and line == "":
			break

		line = line.strip_edges()

		if line == "":
			continue

		var said: Dictionary = {"kind": "line", "text": line} if errors else parse_line(line)
		lock.lock()

		if said["kind"] == "line":
			# A progress bar rewrites its line with carriage returns: its last state.
			last_lines.append(String(said["text"]).split("\r")[-1])

			if last_lines.size() > KEPT_LINES:
				last_lines = last_lines.slice(last_lines.size() - KEPT_LINES)
		else:
			inbox.append(said)

		lock.unlock()

	if not errors:
		lock.lock()
		inbox.append({"kind": "stopped"})
		lock.unlock()


## The lines that arrived, on the editor's thread: answers go to their commands; progress and a
## stop are returned for the dock to show.
func poll() -> Array:
	lock.lock()
	var arrived: Array = inbox.duplicate()
	inbox.clear()
	lock.unlock()
	var shown: Array = []

	for said in arrived:
		match String(said["kind"]):
			"answer":
				var waiting: Variant = pending.get(said["id"])

				if waiting != null:
					pending.erase(said["id"])
					(waiting[1] as Callable).call(said["ok"], said["result"], said["error"])
			"progress":
				shown.append(said)
			"stopped":
				if not stopped_told:
					stopped_told = true
					var why: String = "\n".join(last_words(8))

					for id in pending.keys():
						var waiting: Array = pending[id]
						(waiting[1] as Callable).call(false, null, "The voice tools stopped.\n" + why)

					pending.clear()
					shown.append({"kind": "stopped", "text": why})

	return shown


func last_words(count: int) -> PackedStringArray:
	lock.lock()
	var words: PackedStringArray = last_lines.slice(max(0, last_lines.size() - count))
	lock.unlock()
	return words


## Ends the process at once (a long listen or install stops halfway; what it saved stays).
func stop() -> void:
	if not pipe.is_empty():
		stopped_told = true

		if OS.is_process_running(int(pipe["pid"])):
			OS.kill(int(pipe["pid"]))

	# The readers end on the pipes' end, which the process's end brings (its own workers have pipes
	# of their own); the pipes are closed after, never under a reader.
	for thread in [reader, error_reader]:
		if thread != null and thread.is_started():
			thread.wait_to_finish()

	if not pipe.is_empty():
		(pipe["stdio"] as FileAccess).close()
		(pipe["stderr"] as FileAccess).close()

	reader = null
	error_reader = null
	pipe = {}

	for id in pending.keys():
		var waiting: Array = pending[id]
		(waiting[1] as Callable).call(false, null, "Stopped.")

	pending.clear()
	lock.lock()
	inbox.clear()
	lock.unlock()
