/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: a Model Context Protocol server, so AI tools can read and edit levels. */

#include "editor_mcp_server.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/math/expression.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/os/time.h"
#include "core/version.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_paths.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"

static Array _make(std::initializer_list<Variant> p_values) {
	Array a;
	for (const Variant &v : p_values) {
		a.push_back(v);
	}
	return a;
}

static const int LOG_KEEP = 2000;
static const int MAX_REQUEST_BYTES = 64 * 1024 * 1024;
static const uint64_t CONNECTION_TIMEOUT_MSEC = 30000;

static const char *SERVER_INSTRUCTIONS =
		"OpenRAC Editor, the level editor of OpenRAC (Ratchet & Clank). The open scene is a level: "
		"Game/Mobys, Game/Ties, Game/Shrubs, Game/Collision, Game/Volumes, Game/Paths, Game/Cameras, Game/Sounds. "
		"Node paths are relative to the edited scene root (\".\" is the root). "
		"Values use JSON: vectors and colors as arrays ([x, y, z], [r, g, b, a]), Transform3D as "
		"{\"basis\": [[x], [y], [z]], \"origin\": [x, y, z]}, resources as {\"resource\": \"res://...\"}, "
		"nodes as {\"node\": \"path\"}. Every change made with node_set, node_create, node_delete, "
		"node_reparent and node_duplicate can be undone (undo, redo). Use class_info to learn what any "
		"node can do, node_call for methods, eval and run_gdscript for anything else.";

/* Log capture. */

void EditorMCPServer::_add_log(const String &p_line) {
	log_lines.push_back(p_line);
	log_total++;
	if (log_lines.size() > LOG_KEEP) {
		log_lines.remove_at(0);
	}
}

void EditorMCPServer::_print_handler(void *p_this, const String &p_string, bool p_error, bool p_rich) {
	EditorMCPServer *self = static_cast<EditorMCPServer *>(p_this);
	if (Thread::get_caller_id() != Thread::get_main_id()) {
		return;
	}
	self->_add_log(p_error ? "ERROR: " + p_string : p_string);
}

void EditorMCPServer::_error_handler(void *p_this, const char *p_func, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	EditorMCPServer *self = static_cast<EditorMCPServer *>(p_this);
	if (Thread::get_caller_id() != Thread::get_main_id()) {
		return;
	}
	String kind = p_type == ERR_HANDLER_WARNING ? "WARNING" : (p_type == ERR_HANDLER_SCRIPT ? "SCRIPT ERROR" : (p_type == ERR_HANDLER_SHADER ? "SHADER ERROR" : "ERROR"));
	String text = String::utf8(p_message && p_message[0] ? p_message : p_error);
	self->_add_log(vformat("%s: %s (%s:%d)", kind, text, String::utf8(p_file), p_line));
}

/* Server. */

String EditorMCPServer::get_info_path() {
	return EditorPaths::get_singleton()->get_config_dir().path_join("mcp.json");
}

void EditorMCPServer::_start() {
	if (!bool(EDITOR_GET("openrac/mcp/enabled"))) {
		return;
	}
	port = int(EDITOR_GET("openrac/mcp/port"));
	token = String(EDITOR_GET("openrac/mcp/token"));
	if (token.is_empty()) {
		uint8_t bytes[24];
		if (OS::get_singleton()->get_entropy(bytes, sizeof(bytes)) == OK) {
			token = String::hex_encode_buffer(bytes, sizeof(bytes));
			EditorSettings::get_singleton()->set_setting("openrac/mcp/token", token);
			EditorSettings::get_singleton()->save();
		}
	}

	server.instantiate();
	Error err = server->listen(port, IPAddress("127.0.0.1"));
	if (err != OK) {
		WARN_PRINT(vformat("OpenRAC Editor MCP: could not listen on 127.0.0.1:%d.", port));
		server.unref();
		return;
	}

	// Tell AI clients where to connect.
	Dictionary info;
	info["url"] = vformat("http://127.0.0.1:%d/mcp", port);
	info["token"] = token;
	info["pid"] = OS::get_singleton()->get_process_id();
	info["project"] = ProjectSettings::get_singleton()->get_resource_path();
	Ref<FileAccess> f = FileAccess::open(get_info_path(), FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(JSON::stringify(info, "\t"));
	}
	print_line(vformat("OpenRAC Editor MCP: listening on http://127.0.0.1:%d/mcp", port));
	set_process(true);
}

void EditorMCPServer::_stop() {
	connections.clear();
	if (server.is_valid()) {
		server->stop();
		server.unref();
	}
	set_process(false);
}

void EditorMCPServer::_poll() {
	if (server.is_null()) {
		return;
	}
	while (server->is_connection_available()) {
		Connection c;
		c.peer = server->take_connection();
		c.started_msec = OS::get_singleton()->get_ticks_msec();
		connections.push_back(c);
	}
	for (List<Connection>::Element *E = connections.front(); E;) {
		List<Connection>::Element *next = E->next();
		if (!_handle_connection(E->get())) {
			E->get().peer->disconnect_from_host();
			connections.erase(E);
		}
		E = next;
	}
}

static int _find_header_end(const Vector<uint8_t> &p_data) {
	const uint8_t *r = p_data.ptr();
	for (int i = 0; i + 3 < p_data.size(); i++) {
		if (r[i] == '\r' && r[i + 1] == '\n' && r[i + 2] == '\r' && r[i + 3] == '\n') {
			return i;
		}
	}
	return -1;
}

bool EditorMCPServer::_handle_connection(Connection &p_conn) {
	Ref<StreamPeerTCP> peer = p_conn.peer;
	peer->poll();
	StreamPeerTCP::Status status = peer->get_status();
	if (status == StreamPeerTCP::STATUS_ERROR || status == StreamPeerTCP::STATUS_NONE) {
		return false;
	}
	if (status != StreamPeerTCP::STATUS_CONNECTED) {
		return true;
	}

	if (p_conn.responded) {
		// Send what is left of the response, then close.
		while (p_conn.out_pos < p_conn.out.size()) {
			int sent = 0;
			Error err = peer->put_partial_data(p_conn.out.ptr() + p_conn.out_pos, p_conn.out.size() - p_conn.out_pos, sent);
			if (err != OK) {
				return false;
			}
			if (sent == 0) {
				return true;
			}
			p_conn.out_pos += sent;
		}
		return false;
	}

	if (OS::get_singleton()->get_ticks_msec() - p_conn.started_msec > CONNECTION_TIMEOUT_MSEC) {
		return false;
	}

	int available = peer->get_available_bytes();
	if (available > 0) {
		int old = p_conn.in.size();
		if (old + available > MAX_REQUEST_BYTES) {
			return false;
		}
		p_conn.in.resize(old + available);
		int got = 0;
		if (peer->get_partial_data(p_conn.in.ptrw() + old, available, got) != OK) {
			return false;
		}
		p_conn.in.resize(old + got);
	}

	int header_end = _find_header_end(p_conn.in);
	if (header_end < 0) {
		return true;
	}
	String head = String::utf8((const char *)p_conn.in.ptr(), header_end);
	Vector<String> lines = head.split("\r\n");
	Vector<String> request_line = lines[0].split(" ");
	if (request_line.size() < 2) {
		return false;
	}
	HashMap<String, String> headers;
	for (int i = 1; i < lines.size(); i++) {
		int colon = lines[i].find(":");
		if (colon > 0) {
			headers[lines[i].substr(0, colon).strip_edges().to_lower()] = lines[i].substr(colon + 1).strip_edges();
		}
	}
	int body_len = headers.has("content-length") ? headers["content-length"].to_int() : 0;
	if (body_len < 0 || body_len > MAX_REQUEST_BYTES) {
		return false;
	}
	int body_start = header_end + 4;
	if (p_conn.in.size() - body_start < body_len) {
		return true; // Wait for the rest of the body.
	}
	String body = String::utf8((const char *)p_conn.in.ptr() + body_start, body_len);

	int http_status = 200;
	Vector<String> extra_headers;
	String response = _handle_http(request_line[0], request_line[1], headers, body, http_status, extra_headers);

	String reason = http_status == 200 ? "OK" : (http_status == 202 ? "Accepted" : (http_status == 401 ? "Unauthorized" : (http_status == 403 ? "Forbidden" : (http_status == 404 ? "Not Found" : (http_status == 405 ? "Method Not Allowed" : "Bad Request")))));
	CharString payload = response.utf8();
	String header = vformat("HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n", http_status, reason, payload.length());
	for (const String &h : extra_headers) {
		header += h + "\r\n";
	}
	header += "\r\n";
	CharString header_utf8 = header.utf8();
	p_conn.out.resize(header_utf8.length() + payload.length());
	memcpy(p_conn.out.ptrw(), header_utf8.get_data(), header_utf8.length());
	if (payload.length()) {
		memcpy(p_conn.out.ptrw() + header_utf8.length(), payload.get_data(), payload.length());
	}
	p_conn.responded = true;
	p_conn.out_pos = 0;
	return _handle_connection(p_conn);
}

String EditorMCPServer::_handle_http(const String &p_method, const String &p_path, const HashMap<String, String> &p_headers, const String &p_body, int &r_status, Vector<String> &r_extra_headers) {
	// Refuse web pages: a browser sends an Origin header (DNS rebinding guard).
	if (p_headers.has("origin")) {
		String origin = p_headers["origin"];
		if (!origin.begins_with("http://127.0.0.1") && !origin.begins_with("http://localhost")) {
			r_status = 403;
			return JSON::stringify(_rpc_error(Variant(), -32000, "Origin not allowed."));
		}
	}
	if (!token.is_empty()) {
		String auth = p_headers.has("authorization") ? p_headers["authorization"] : String();
		if (auth != "Bearer " + token) {
			r_status = 401;
			return JSON::stringify(_rpc_error(Variant(), -32001, "Missing or wrong token. Read it from " + get_info_path() + "."));
		}
	}
	String path = p_path.get_slice("?", 0);
	if (path != "/mcp" && path != "/") {
		r_status = 404;
		return JSON::stringify(_rpc_error(Variant(), -32000, "Use POST /mcp."));
	}
	if (p_method == "DELETE") {
		r_status = 200;
		return String();
	}
	if (p_method != "POST") {
		r_status = 405;
		r_extra_headers.push_back("Allow: POST, DELETE");
		return JSON::stringify(_rpc_error(Variant(), -32000, "This server answers POST requests with JSON only."));
	}

	Variant parsed = JSON::parse_string(p_body);
	if (parsed.get_type() == Variant::ARRAY) {
		Array out;
		Array batch = parsed;
		for (int i = 0; i < batch.size(); i++) {
			bool notification = false;
			Variant res = batch[i].get_type() == Variant::DICTIONARY ? _handle_rpc(batch[i], notification) : Variant(_rpc_error(Variant(), -32600, "Invalid request."));
			if (!notification) {
				out.push_back(res);
			}
		}
		if (out.is_empty()) {
			r_status = 202;
			return String();
		}
		r_status = 200;
		return JSON::stringify(out, "", false);
	}
	if (parsed.get_type() != Variant::DICTIONARY) {
		r_status = 400;
		return JSON::stringify(_rpc_error(Variant(), -32700, "Parse error."));
	}
	bool notification = false;
	Variant res = _handle_rpc(parsed, notification);
	if (notification) {
		r_status = 202;
		return String();
	}
	Dictionary req = parsed;
	if (String(req.get("method", "")) == "initialize") {
		r_extra_headers.push_back("Mcp-Session-Id: " + session_id);
	}
	r_status = 200;
	return JSON::stringify(res, "", false);
}

Dictionary EditorMCPServer::_rpc_error(const Variant &p_id, int p_code, const String &p_message) const {
	Dictionary err;
	err["code"] = p_code;
	err["message"] = p_message;
	Dictionary res;
	res["jsonrpc"] = "2.0";
	res["id"] = p_id;
	res["error"] = err;
	return res;
}

Variant EditorMCPServer::_handle_rpc(const Dictionary &p_request, bool &r_is_notification) {
	r_is_notification = !p_request.has("id");
	String method = p_request.get("method", "");
	Variant id = p_request.get("id", Variant());
	// JSON numbers parse as floats; answer with the id as the client sent it.
	if (id.get_type() == Variant::FLOAT && double(id) == Math::floor(double(id))) {
		id = int64_t(double(id));
	}
	Dictionary params = p_request.get("params", Dictionary());
	if (r_is_notification) {
		return Variant();
	}

	Dictionary result;
	if (method == "initialize") {
		static const char *versions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };
		String wanted = params.get("protocolVersion", "");
		String version = versions[1];
		for (const char *v : versions) {
			if (wanted == v) {
				version = v;
			}
		}
		Dictionary tools;
		tools["listChanged"] = false;
		Dictionary caps;
		caps["tools"] = tools;
		Dictionary server_info;
		server_info["name"] = "openrac-editor";
		server_info["title"] = "OpenRAC Editor";
		server_info["version"] = GODOT_VERSION_FULL_CONFIG;
		result["protocolVersion"] = version;
		result["capabilities"] = caps;
		result["serverInfo"] = server_info;
		result["instructions"] = SERVER_INSTRUCTIONS;
	} else if (method == "ping") {
		// Empty result.
	} else if (method == "tools/list") {
		result["tools"] = _tool_list();
	} else if (method == "tools/call") {
		String name = params.get("name", "");
		Dictionary args = params.get("arguments", Dictionary());
		result = _call_tool(name, args);
	} else if (method == "resources/list") {
		result["resources"] = Array();
	} else if (method == "prompts/list") {
		result["prompts"] = Array();
	} else {
		return _rpc_error(id, -32601, "Method not found: " + method);
	}
	Dictionary res;
	res["jsonrpc"] = "2.0";
	res["id"] = id;
	res["result"] = result;
	return res;
}

/* Tool table. */

static Dictionary _prop(const String &p_type, const String &p_description) {
	Dictionary d;
	d["type"] = p_type;
	d["description"] = p_description;
	return d;
}

static Dictionary _any(const String &p_description) {
	Dictionary d;
	d["description"] = p_description;
	return d;
}

static Dictionary _tool(const String &p_name, const String &p_description, const Dictionary &p_props, const Vector<String> &p_required, bool p_read_only) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_props;
	Array req;
	for (const String &r : p_required) {
		req.push_back(r);
	}
	schema["required"] = req;
	Dictionary annotations;
	annotations["readOnlyHint"] = p_read_only;
	Dictionary t;
	t["name"] = p_name;
	t["description"] = p_description;
	t["inputSchema"] = schema;
	t["annotations"] = annotations;
	return t;
}

Array EditorMCPServer::_tool_list() const {
	Array tools;
	Dictionary p;

	tools.push_back(_tool("editor_state", "Open scenes, the edited scene, the selection, the project folder and the editor version.", Dictionary(), {}, true));

	p = Dictionary();
	p["path"] = _prop("string", "Scene file, e.g. res://levels/level_03/level_03.tscn. Switches to it when it is already open.");
	tools.push_back(_tool("scene_open", "Open a scene (a level) in the editor and make it the edited scene.", p, { "path" }, false));

	p = Dictionary();
	p["path"] = _prop("string", "Optional: save under this path instead.");
	tools.push_back(_tool("scene_save", "Save the edited scene.", p, {}, false));

	p = Dictionary();
	p["path"] = _prop("string", "Start node (default: the scene root).");
	p["depth"] = _prop("integer", "Levels below the start node (default 1; -1 for all).");
	p["class"] = _prop("string", "Only nodes of this class or a subclass.");
	p["name"] = _prop("string", "Only names matching this pattern (* and ? wildcards).");
	p["group"] = _prop("string", "Only nodes in this group.");
	p["meta"] = _prop("object", "Only nodes whose metadata has these values, e.g. {\"rc1_class\": 11}.");
	p["offset"] = _prop("integer", "Skip this many matches.");
	p["limit"] = _prop("integer", "At most this many matches (default 200).");
	tools.push_back(_tool("tree", "List the nodes of the edited scene with class, child count, script and metadata; filter by class, name, group or metadata.", p, {}, true));

	p = Dictionary();
	p["path"] = _prop("string", "Node path.");
	p["properties"] = _prop("array", "Only these properties (default: every stored or editor property).");
	p["all"] = _prop("boolean", "Include every property, internal ones too.");
	tools.push_back(_tool("node_get", "Read a node's properties, metadata, groups and children.", p, { "path" }, true));

	p = Dictionary();
	p["path"] = _prop("string", "Node path.");
	p["properties"] = _prop("object", "Property names and values, e.g. {\"position\": [1, 2, 3], \"metadata/rc1_group\": 4}. Undoable.");
	tools.push_back(_tool("node_set", "Set properties of a node as one undoable action.", p, { "path", "properties" }, false));

	p = Dictionary();
	p["path"] = _prop("string", "Node path.");
	p["method"] = _prop("string", "Method name (see class_info).");
	p["args"] = _prop("array", "Arguments, converted to the method's argument types.");
	tools.push_back(_tool("node_call", "Call a method on a node and return its result. Not undoable.", p, { "path", "method" }, false));

	p = Dictionary();
	p["parent"] = _prop("string", "Parent node path (default: the scene root).");
	p["type"] = _prop("string", "Class to create, e.g. Node3D, MeshInstance3D.");
	p["scene"] = _prop("string", "Or a scene file to instance, e.g. res://levels/level_03/mobys/moby_11.tscn.");
	p["name"] = _prop("string", "Node name.");
	p["properties"] = _prop("object", "Properties to set on the new node.");
	tools.push_back(_tool("node_create", "Create a node or instance a scene under a parent, as one undoable action.", p, {}, false));

	p = Dictionary();
	p["paths"] = _prop("array", "Node paths to delete.");
	tools.push_back(_tool("node_delete", "Delete nodes, as one undoable action.", p, { "paths" }, false));

	p = Dictionary();
	p["path"] = _prop("string", "Node path.");
	p["parent"] = _prop("string", "New parent path.");
	p["index"] = _prop("integer", "Position among the new parent's children (default: last).");
	p["keep_global_transform"] = _prop("boolean", "Keep the node where it is in the world (default true).");
	tools.push_back(_tool("node_reparent", "Move a node to another parent or position, as one undoable action.", p, { "path" }, false));

	p = Dictionary();
	p["path"] = _prop("string", "Node path.");
	p["name"] = _prop("string", "Name of the copy.");
	p["properties"] = _prop("object", "Properties to set on the copy.");
	tools.push_back(_tool("node_duplicate", "Copy a node with its children next to it, as one undoable action.", p, { "path" }, false));

	p = Dictionary();
	p["paths"] = _prop("array", "Node paths to select (empty clears the selection).");
	p["focus"] = _prop("boolean", "Move the 3D view to the selection.");
	tools.push_back(_tool("select", "Select nodes in the editor, so the inspector and 3D view show them.", p, { "paths" }, false));

	p = Dictionary();
	p["class"] = _prop("string", "A class name, or a node path to describe that node's class and script.");
	p["inherited"] = _prop("boolean", "Include inherited members (default false).");
	tools.push_back(_tool("class_info", "Describe a class: parent classes, properties, methods with arguments, signals and constants.", p, { "class" }, true));

	tools.push_back(_tool("undo", "Undo the last action in the edited scene's history.", Dictionary(), {}, false));
	tools.push_back(_tool("redo", "Redo the last undone action.", Dictionary(), {}, false));

	p = Dictionary();
	p["viewport"] = _prop("integer", "3D viewport 0 to 3 (default 0).");
	tools.push_back(_tool("view_get", "Read a 3D viewport's camera: orbit position, x_rotation, y_rotation, distance, and the camera transform and FOV.", p, {}, true));

	p = Dictionary();
	p["viewport"] = _prop("integer", "3D viewport 0 to 3 (default 0).");
	p["position"] = _prop("array", "Orbit centre [x, y, z].");
	p["x_rotation"] = _prop("number", "Pitch in radians.");
	p["y_rotation"] = _prop("number", "Yaw in radians.");
	p["distance"] = _prop("number", "Distance from the orbit centre.");
	p["look_from"] = _prop("array", "Or: put the camera at this point...");
	p["look_at"] = _prop("array", "...looking at this point.");
	tools.push_back(_tool("view_set", "Move a 3D viewport's camera.", p, {}, false));

	p = Dictionary();
	p["viewport"] = _prop("integer", "3D viewport 0 to 3 (default 0), or -1 for the whole editor window.");
	p["max_width"] = _prop("integer", "Scale the picture down to at most this width.");
	tools.push_back(_tool("screenshot", "Take a PNG picture of a 3D viewport or the whole editor window.", p, {}, true));

	p = Dictionary();
	p["expression"] = _prop("string", "A Godot Expression, e.g. get_child_count() or Vector3(1, 2, 3).length().");
	p["path"] = _prop("string", "Node the expression runs on (default: the scene root).");
	tools.push_back(_tool("eval", "Evaluate an expression on a node and return the value.", p, { "expression" }, false));

	p = Dictionary();
	p["code"] = _prop("string", "GDScript. Either a whole script with func run(), or just the body of run(). `scene` is the edited scene root, `editor` is EditorInterface. Return a value to get it back.");
	tools.push_back(_tool("run_gdscript", "Run GDScript in the editor and return what run() returns. Anything the editor can do is possible here.", p, { "code" }, false));

	p = Dictionary();
	p["since"] = _prop("integer", "Only lines after this line number (from a previous call's \"next\").");
	p["limit"] = _prop("integer", "At most this many lines (default 200).");
	tools.push_back(_tool("log", "Read the editor's output and errors.", p, {}, true));

	p = Dictionary();
	p["path"] = _prop("string", "Folder (default res://).");
	p["pattern"] = _prop("string", "Only files matching this pattern, e.g. *.tscn.");
	p["recursive"] = _prop("boolean", "Look in subfolders too.");
	tools.push_back(_tool("files", "List files and folders of the project.", p, {}, true));

	p = Dictionary();
	p["path"] = _prop("string", "Resource path, e.g. res://levels/level_03/mobys/moby_11.tscn.");
	p["properties"] = _prop("array", "Only these properties.");
	tools.push_back(_tool("resource_get", "Load a resource and read its properties.", p, { "path" }, true));

	p = Dictionary();
	p["kind"] = _prop("string", "Mobys, Ties, Shrubs, Volumes, Paths, GrindPaths, Cameras, Sounds, Collision or HeroWalls (default Mobys).");
	p["class"] = _any("Moby class id (number) or class name (text).");
	p["spawn_id"] = _prop("integer", "Moby spawn id (UID).");
	p["near"] = _prop("array", "Only objects within radius of this point [x, y, z] (scene coordinates).");
	p["radius"] = _prop("number", "Radius for near (default 20).");
	p["limit"] = _prop("integer", "At most this many (default 100).");
	tools.push_back(_tool("level_find", "Find level objects by class, spawn id or place. Returns path, class, spawn id, group and position.", p, {}, true));

	return tools;
}

Dictionary EditorMCPServer::_call_tool(const String &p_name, const Dictionary &p_args) {
	if (p_name == "editor_state") {
		return _tool_editor_state(p_args);
	} else if (p_name == "scene_open") {
		return _tool_scene_open(p_args);
	} else if (p_name == "scene_save") {
		return _tool_scene_save(p_args);
	} else if (p_name == "tree") {
		return _tool_tree(p_args);
	} else if (p_name == "node_get") {
		return _tool_node_get(p_args);
	} else if (p_name == "node_set") {
		return _tool_node_set(p_args);
	} else if (p_name == "node_call") {
		return _tool_node_call(p_args);
	} else if (p_name == "node_create") {
		return _tool_node_create(p_args);
	} else if (p_name == "node_delete") {
		return _tool_node_delete(p_args);
	} else if (p_name == "node_reparent") {
		return _tool_node_reparent(p_args);
	} else if (p_name == "node_duplicate") {
		return _tool_node_duplicate(p_args);
	} else if (p_name == "select") {
		return _tool_select(p_args);
	} else if (p_name == "class_info") {
		return _tool_class_info(p_args);
	} else if (p_name == "undo") {
		return _tool_undo(p_args);
	} else if (p_name == "redo") {
		return _tool_redo(p_args);
	} else if (p_name == "view_get") {
		return _tool_view_get(p_args);
	} else if (p_name == "view_set") {
		return _tool_view_set(p_args);
	} else if (p_name == "screenshot") {
		return _tool_screenshot(p_args);
	} else if (p_name == "eval") {
		return _tool_eval(p_args);
	} else if (p_name == "run_gdscript") {
		return _tool_run_gdscript(p_args);
	} else if (p_name == "log") {
		return _tool_log(p_args);
	} else if (p_name == "files") {
		return _tool_files(p_args);
	} else if (p_name == "resource_get") {
		return _tool_resource_get(p_args);
	} else if (p_name == "level_find") {
		return _tool_level_find(p_args);
	}
	return _error_result("Unknown tool: " + p_name);
}

/* Results. */

Dictionary EditorMCPServer::_text_result(const Variant &p_data, bool p_error) const {
	Dictionary text;
	text["type"] = "text";
	text["text"] = p_data.get_type() == Variant::STRING ? String(p_data) : JSON::stringify(p_data, "", false);
	Array content;
	content.push_back(text);
	Dictionary res;
	res["content"] = content;
	res["isError"] = p_error;
	if (p_data.get_type() == Variant::DICTIONARY && !p_error) {
		res["structuredContent"] = p_data;
	}
	return res;
}

Dictionary EditorMCPServer::_error_result(const String &p_message) const {
	return _text_result(p_message, true);
}

/* Helpers. */

Node *EditorMCPServer::_scene_root() const {
	return EditorNode::get_singleton()->get_edited_scene();
}

Node *EditorMCPServer::_get_node(const Variant &p_path, String &r_error) const {
	Node *root = _scene_root();
	if (!root) {
		r_error = "No scene is open. Use scene_open first.";
		return nullptr;
	}
	String path = p_path.get_type() == Variant::NIL ? String(".") : String(p_path);
	if (path.is_empty() || path == "." || path == "/") {
		return root;
	}
	if (path.begins_with("/root/")) {
		Node *n = root->get_tree()->get_root()->get_node_or_null(NodePath(path));
		if (n && (n == root || root->is_ancestor_of(n))) {
			return n;
		}
	}
	if (path.begins_with(String(root->get_name()) + "/")) {
		path = path.substr(String(root->get_name()).length() + 1);
	} else if (path == String(root->get_name())) {
		return root;
	}
	Node *n = root->get_node_or_null(NodePath(path));
	if (!n) {
		r_error = "No node at \"" + path + "\".";
	}
	return n;
}

String EditorMCPServer::_node_path(const Node *p_node) const {
	Node *root = _scene_root();
	if (!root || !p_node) {
		return String();
	}
	if (p_node == root) {
		return ".";
	}
	return String(root->get_path_to(p_node));
}

String EditorMCPServer::_meta_by_suffix(const Node *p_node, const String &p_suffix, Variant &r_value) {
	List<StringName> metas;
	p_node->get_meta_list(&metas);
	for (const StringName &m : metas) {
		String s = m;
		if (s.ends_with(p_suffix) && s.find("_") == s.length() - p_suffix.length()) {
			r_value = p_node->get_meta(m);
			return s;
		}
	}
	return String();
}

Dictionary EditorMCPServer::_node_summary(const Node *p_node) const {
	Dictionary d;
	d["path"] = _node_path(p_node);
	d["name"] = String(p_node->get_name());
	d["class"] = p_node->get_class();
	d["children"] = p_node->get_child_count(false);
	Ref<Script> sc = p_node->get_script();
	if (sc.is_valid()) {
		d["script"] = sc->get_path();
	}
	if (!p_node->get_scene_file_path().is_empty() && p_node != _scene_root()) {
		d["scene"] = p_node->get_scene_file_path();
	}
	List<StringName> metas;
	p_node->get_meta_list(&metas);
	if (!metas.is_empty()) {
		Dictionary meta;
		for (const StringName &m : metas) {
			Variant v = p_node->get_meta(m);
			// Keep summaries short: long arrays (pvars) only in node_get.
			if (v.is_array() && Array(v).size() > 8) {
				meta[m] = vformat("<%d values>", Array(v).size());
			} else {
				meta[m] = _to_json(v);
			}
		}
		d["meta"] = meta;
	}
	const Node3D *n3 = Object::cast_to<Node3D>(p_node);
	if (n3) {
		d["position"] = _to_json(n3->get_position());
	}
	return d;
}

static Array _vec(const real_t *p_v, int p_n) {
	Array a;
	for (int i = 0; i < p_n; i++) {
		a.push_back(p_v[i]);
	}
	return a;
}

Variant EditorMCPServer::_to_json(const Variant &p_value, int p_depth) const {
	if (p_depth > 16) {
		return "<too deep>";
	}
	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::STRING:
			return p_value;
		case Variant::FLOAT: {
			double f = p_value;
			if (Math::is_nan(f) || Math::is_inf(f)) {
				return String::num(f);
			}
			return f;
		}
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
			return String(p_value);
		case Variant::VECTOR2: {
			Vector2 v = p_value;
			return _vec(&v.x, 2);
		}
		case Variant::VECTOR2I: {
			Vector2i v = p_value;
			return _make({v.x, v.y});
		}
		case Variant::VECTOR3: {
			Vector3 v = p_value;
			return _vec(&v.x, 3);
		}
		case Variant::VECTOR3I: {
			Vector3i v = p_value;
			return _make({v.x, v.y, v.z});
		}
		case Variant::VECTOR4: {
			Vector4 v = p_value;
			return _vec(&v.x, 4);
		}
		case Variant::VECTOR4I: {
			Vector4i v = p_value;
			return _make({v.x, v.y, v.z, v.w});
		}
		case Variant::QUATERNION: {
			Quaternion q = p_value;
			return _make({q.x, q.y, q.z, q.w});
		}
		case Variant::COLOR: {
			Color c = p_value;
			return _make({c.r, c.g, c.b, c.a});
		}
		case Variant::PLANE: {
			Plane pl = p_value;
			return _make({pl.normal.x, pl.normal.y, pl.normal.z, pl.d});
		}
		case Variant::RECT2: {
			Rect2 r = p_value;
			Dictionary d;
			d["position"] = _to_json(r.position);
			d["size"] = _to_json(r.size);
			return d;
		}
		case Variant::RECT2I: {
			Rect2i r = p_value;
			Dictionary d;
			d["position"] = _to_json(r.position);
			d["size"] = _to_json(r.size);
			return d;
		}
		case Variant::AABB: {
			AABB b = p_value;
			Dictionary d;
			d["position"] = _to_json(b.position);
			d["size"] = _to_json(b.size);
			return d;
		}
		case Variant::BASIS: {
			Basis b = p_value;
			return _make({_to_json(b.get_column(0)), _to_json(b.get_column(1)), _to_json(b.get_column(2))});
		}
		case Variant::TRANSFORM2D: {
			Transform2D t = p_value;
			Dictionary d;
			d["x"] = _to_json(t.columns[0]);
			d["y"] = _to_json(t.columns[1]);
			d["origin"] = _to_json(t.columns[2]);
			return d;
		}
		case Variant::TRANSFORM3D: {
			Transform3D t = p_value;
			Dictionary d;
			d["basis"] = _to_json(t.basis);
			d["origin"] = _to_json(t.origin);
			return d;
		}
		case Variant::PROJECTION: {
			Projection pr = p_value;
			return _make({_to_json(pr.columns[0]), _to_json(pr.columns[1]), _to_json(pr.columns[2]), _to_json(pr.columns[3])});
		}
		case Variant::OBJECT: {
			Object *obj = p_value.get_validated_object();
			if (!obj) {
				return Variant();
			}
			Node *node = Object::cast_to<Node>(obj);
			if (node) {
				Dictionary d;
				Node *root = _scene_root();
				d["node"] = (root && (node == root || root->is_ancestor_of(node))) ? _node_path(node) : String(node->is_inside_tree() ? String(node->get_path()) : String(node->get_name()));
				d["class"] = node->get_class();
				return d;
			}
			Resource *res = Object::cast_to<Resource>(obj);
			Dictionary d;
			d["class"] = obj->get_class();
			if (res && !res->get_path().is_empty() && !res->get_path().contains("::")) {
				d["resource"] = res->get_path();
			} else if (res) {
				d["resource"] = res->get_path();
				d["embedded"] = true;
				if (!res->get_name().is_empty()) {
					d["name"] = res->get_name();
				}
			}
			d["id"] = String::num_uint64(obj->get_instance_id());
			return d;
		}
		case Variant::DICTIONARY: {
			Dictionary in = p_value;
			Dictionary out;
			for (const KeyValue<Variant, Variant> &kv : in) {
				out[String(kv.key)] = _to_json(kv.value, p_depth + 1);
			}
			return out;
		}
		case Variant::RID:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
			return String(p_value);
		default: {
			if (p_value.is_array()) {
				Array in = p_value;
				Array out;
				for (int i = 0; i < in.size(); i++) {
					out.push_back(_to_json(in[i], p_depth + 1));
				}
				return out;
			}
			return String(p_value);
		}
	}
}

static bool _num_array(const Variant &p_json, int p_n, double *r_out) {
	if (p_json.get_type() != Variant::ARRAY) {
		return false;
	}
	Array a = p_json;
	if (a.size() != p_n) {
		return false;
	}
	for (int i = 0; i < p_n; i++) {
		if (a[i].get_type() != Variant::INT && a[i].get_type() != Variant::FLOAT) {
			return false;
		}
		r_out[i] = a[i];
	}
	return true;
}

Variant EditorMCPServer::_from_json(const Variant &p_json, Variant::Type p_type, String &r_error) const {
	double v[4] = {};
	// References to nodes and resources.
	if (p_json.get_type() == Variant::DICTIONARY) {
		Dictionary d = p_json;
		if (d.has("node") && d.size() <= 2) {
			String err;
			Node *n = _get_node(d["node"], err);
			if (!n) {
				r_error = err;
			}
			return n;
		}
		if (d.has("resource") && !bool(d.get("embedded", false))) {
			Ref<Resource> res = ResourceLoader::load(d["resource"]);
			if (res.is_null()) {
				r_error = "Could not load " + String(d["resource"]) + ".";
			}
			return res;
		}
		if (d.has("new")) {
			Object *obj = ClassDB::instantiate(d["new"]);
			Resource *res = Object::cast_to<Resource>(obj);
			if (!res) {
				if (obj && !Object::cast_to<RefCounted>(obj)) {
					memdelete(obj);
				}
				r_error = "\"new\" must name a Resource class.";
				return Variant();
			}
			Ref<Resource> ref(res);
			Dictionary props = d.get("properties", Dictionary());
			for (const KeyValue<Variant, Variant> &kv : props) {
				String err;
				Variant val = _from_json(kv.value, _property_type(res, kv.key), err);
				if (!err.is_empty()) {
					r_error = err;
					return Variant();
				}
				res->set(kv.key, val);
			}
			return ref;
		}
	}

	switch (p_type) {
		case Variant::VECTOR2:
			if (_num_array(p_json, 2, v)) {
				return Vector2(v[0], v[1]);
			}
			break;
		case Variant::VECTOR2I:
			if (_num_array(p_json, 2, v)) {
				return Vector2i(v[0], v[1]);
			}
			break;
		case Variant::VECTOR3:
			if (_num_array(p_json, 3, v)) {
				return Vector3(v[0], v[1], v[2]);
			}
			break;
		case Variant::VECTOR3I:
			if (_num_array(p_json, 3, v)) {
				return Vector3i(v[0], v[1], v[2]);
			}
			break;
		case Variant::VECTOR4:
			if (_num_array(p_json, 4, v)) {
				return Vector4(v[0], v[1], v[2], v[3]);
			}
			break;
		case Variant::VECTOR4I:
			if (_num_array(p_json, 4, v)) {
				return Vector4i(v[0], v[1], v[2], v[3]);
			}
			break;
		case Variant::QUATERNION:
			if (_num_array(p_json, 4, v)) {
				return Quaternion(v[0], v[1], v[2], v[3]);
			}
			break;
		case Variant::PLANE:
			if (_num_array(p_json, 4, v)) {
				return Plane(v[0], v[1], v[2], v[3]);
			}
			break;
		case Variant::COLOR:
			if (_num_array(p_json, 4, v)) {
				return Color(v[0], v[1], v[2], v[3]);
			}
			if (_num_array(p_json, 3, v)) {
				return Color(v[0], v[1], v[2]);
			}
			if (p_json.get_type() == Variant::STRING) {
				return Color::from_string(p_json, Color());
			}
			break;
		case Variant::BASIS:
			if (p_json.get_type() == Variant::ARRAY && Array(p_json).size() == 3) {
				Array a = p_json;
				Basis b;
				for (int i = 0; i < 3; i++) {
					if (!_num_array(a[i], 3, v)) {
						r_error = "A Basis is three columns [[x], [y], [z]].";
						return Variant();
					}
					b.set_column(i, Vector3(v[0], v[1], v[2]));
				}
				return b;
			}
			break;
		case Variant::TRANSFORM3D:
			if (p_json.get_type() == Variant::DICTIONARY) {
				Dictionary d = p_json;
				Transform3D t;
				if (d.has("basis")) {
					t.basis = _from_json(d["basis"], Variant::BASIS, r_error);
				}
				if (d.has("origin")) {
					t.origin = _from_json(d["origin"], Variant::VECTOR3, r_error);
				}
				return t;
			}
			break;
		case Variant::TRANSFORM2D:
			if (p_json.get_type() == Variant::DICTIONARY) {
				Dictionary d = p_json;
				Transform2D t;
				t.columns[0] = _from_json(d.get("x", _make({1, 0})), Variant::VECTOR2, r_error);
				t.columns[1] = _from_json(d.get("y", _make({0, 1})), Variant::VECTOR2, r_error);
				t.columns[2] = _from_json(d.get("origin", _make({0, 0})), Variant::VECTOR2, r_error);
				return t;
			}
			break;
		case Variant::AABB:
		case Variant::RECT2:
		case Variant::RECT2I:
			if (p_json.get_type() == Variant::DICTIONARY) {
				Dictionary d = p_json;
				if (p_type == Variant::AABB) {
					return AABB(_from_json(d.get("position", Array()), Variant::VECTOR3, r_error), _from_json(d.get("size", Array()), Variant::VECTOR3, r_error));
				} else if (p_type == Variant::RECT2) {
					return Rect2(_from_json(d.get("position", Array()), Variant::VECTOR2, r_error), _from_json(d.get("size", Array()), Variant::VECTOR2, r_error));
				}
				return Rect2i(_from_json(d.get("position", Array()), Variant::VECTOR2I, r_error), _from_json(d.get("size", Array()), Variant::VECTOR2I, r_error));
			}
			break;
		case Variant::NIL:
			// No type known: keep JSON as it is, numbers that are whole become ints.
			if (p_json.get_type() == Variant::FLOAT && double(p_json) == Math::floor(double(p_json)) && Math::abs(double(p_json)) < 9007199254740992.0) {
				return int64_t(double(p_json));
			}
			return p_json;
		default:
			break;
	}

	if (p_json.get_type() == p_type) {
		return p_json;
	}
	// Plain conversions: number to int or float, text to StringName or NodePath, arrays to packed arrays.
	if (p_type == Variant::INT && p_json.get_type() == Variant::FLOAT) {
		return int64_t(double(p_json));
	}
	if (p_type == Variant::OBJECT && p_json.get_type() == Variant::NIL) {
		return Variant();
	}
	if (p_type == Variant::OBJECT && p_json.get_type() == Variant::STRING) {
		String s = p_json;
		if (s.begins_with("res://") || s.begins_with("uid://")) {
			return ResourceLoader::load(s);
		}
		String err;
		Node *n = _get_node(s, err);
		if (n) {
			return n;
		}
	}
	const Variant *args[1] = { &p_json };
	Variant out;
	Callable::CallError ce;
	Variant::construct(p_type, out, args, 1, ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		r_error = vformat("Cannot turn %s into %s.", JSON::stringify(p_json), Variant::get_type_name(p_type));
		return Variant();
	}
	return out;
}

Variant::Type EditorMCPServer::_property_type(Object *p_object, const StringName &p_property) const {
	String name = p_property;
	if (name.begins_with("metadata/")) {
		StringName meta = name.substr(9);
		return p_object->has_meta(meta) ? p_object->get_meta(meta).get_type() : Variant::NIL;
	}
	List<PropertyInfo> props;
	p_object->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (pi.name == p_property) {
			return pi.type == Variant::NIL ? p_object->get(p_property).get_type() : pi.type;
		}
	}
	return p_object->get(p_property).get_type();
}

Dictionary EditorMCPServer::_object_properties(Object *p_object, const Array &p_only, bool p_all) const {
	Dictionary out;
	if (!p_only.is_empty()) {
		for (int i = 0; i < p_only.size(); i++) {
			bool valid = false;
			Variant v = p_object->get(p_only[i], &valid);
			out[p_only[i]] = valid ? _to_json(v) : Variant("<no such property>");
		}
		return out;
	}
	List<PropertyInfo> props;
	p_object->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (pi.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		if (!p_all && !(pi.usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR))) {
			continue;
		}
		if (pi.name == "script" || String(pi.name).begins_with("metadata/")) {
			continue;
		}
		out[pi.name] = _to_json(p_object->get(pi.name));
	}
	return out;
}

void EditorMCPServer::_collect_owned(Node *p_node, Node *p_owner, List<Node *> &r_list) const {
	if (p_node->get_owner() == p_owner) {
		r_list.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		_collect_owned(p_node->get_child(i, false), p_owner, r_list);
	}
}

/* Tools. */

Dictionary EditorMCPServer::_tool_editor_state(const Dictionary &p_args) {
	Dictionary d;
	d["editor"] = "OpenRAC Editor " + String(GODOT_VERSION_FULL_CONFIG);
	d["project"] = ProjectSettings::get_singleton()->get_resource_path();
	Array open;
	PackedStringArray scenes = EditorInterface::get_singleton()->get_open_scenes();
	for (const String &s : scenes) {
		open.push_back(s);
	}
	d["open_scenes"] = open;
	Node *root = _scene_root();
	d["edited_scene"] = root ? Variant(root->get_scene_file_path()) : Variant();
	d["edited_root"] = root ? Variant(_node_summary(root)) : Variant();
	Array sel;
	if (root) {
		for (Node *n : EditorNode::get_singleton()->get_editor_selection()->get_top_selected_node_list()) {
			sel.push_back(_node_path(n));
		}
	}
	d["selection"] = sel;
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	if (root) {
		UndoRedo *history = ur->get_history_undo_redo(ur->get_history_id_for_object(root));
		if (history) {
			d["undo"] = history->has_undo() ? Variant(history->get_current_action_name()) : Variant();
			d["can_redo"] = history->has_redo();
		}
	}
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_scene_open(const Dictionary &p_args) {
	String path = p_args.get("path", "");
	if (!ResourceLoader::exists(path)) {
		return _error_result("No scene at " + path + ".");
	}
	EditorInterface::get_singleton()->open_scene_from_path(path);
	Node *root = _scene_root();
	if (!root || root->get_scene_file_path() != path) {
		return _error_result("Could not open " + path + ". See the log tool.");
	}
	return _text_result(_node_summary(root));
}

Dictionary EditorMCPServer::_tool_scene_save(const Dictionary &p_args) {
	Node *root = _scene_root();
	if (!root) {
		return _error_result("No scene is open.");
	}
	String path = p_args.get("path", "");
	if (!path.is_empty()) {
		EditorInterface::get_singleton()->save_scene_as(path, false);
	} else if (EditorInterface::get_singleton()->save_scene() != OK) {
		return _error_result("Could not save the scene.");
	}
	Dictionary d;
	d["saved"] = path.is_empty() ? root->get_scene_file_path() : path;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_tree(const Dictionary &p_args) {
	String err;
	Node *start = _get_node(p_args.get("path", Variant()), err);
	if (!start) {
		return _error_result(err);
	}
	int depth = p_args.get("depth", 1);
	String cls = p_args.get("class", "");
	String pattern = p_args.get("name", "");
	String group = p_args.get("group", "");
	Dictionary meta = p_args.get("meta", Dictionary());
	int offset = p_args.get("offset", 0);
	int limit = p_args.get("limit", 200);
	bool filtered = !cls.is_empty() || !pattern.is_empty() || !group.is_empty() || !meta.is_empty();
	if (filtered && !p_args.has("depth")) {
		depth = -1;
	}

	Array nodes;
	int matched = 0;
	List<Pair<Node *, int>> stack;
	stack.push_back(Pair<Node *, int>(start, 0));
	while (!stack.is_empty()) {
		Pair<Node *, int> cur = stack.front()->get();
		stack.pop_front();
		Node *n = cur.first;
		bool ok = true;
		if (!cls.is_empty() && !n->is_class(cls)) {
			ok = false;
		}
		if (ok && !pattern.is_empty() && !String(n->get_name()).matchn(pattern)) {
			ok = false;
		}
		if (ok && !group.is_empty() && !n->is_in_group(group)) {
			ok = false;
		}
		if (ok && !meta.is_empty()) {
			for (const KeyValue<Variant, Variant> &kv : meta) {
				if (!n->has_meta(kv.key)) {
					ok = false;
					break;
				}
				Variant have = n->get_meta(kv.key);
				Variant want = kv.value;
				bool same = (have.get_type() == Variant::INT || have.get_type() == Variant::FLOAT) && (want.get_type() == Variant::INT || want.get_type() == Variant::FLOAT) ? double(have) == double(want) : have == want;
				if (!same) {
					ok = false;
					break;
				}
			}
		}
		if (ok && (n != start || !filtered)) {
			if (matched >= offset && nodes.size() < limit) {
				Dictionary s = _node_summary(n);
				s["depth"] = cur.second;
				nodes.push_back(s);
			}
			matched++;
		}
		if (depth < 0 || cur.second < depth) {
			// Breadth first keeps parents before their children.
			for (int i = 0; i < n->get_child_count(false); i++) {
				stack.push_back(Pair<Node *, int>(n->get_child(i, false), cur.second + 1));
			}
		}
	}
	Dictionary d;
	d["nodes"] = nodes;
	d["matched"] = matched;
	d["returned"] = nodes.size();
	if (matched > offset + nodes.size()) {
		d["next_offset"] = offset + nodes.size();
	}
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_node_get(const Dictionary &p_args) {
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	Dictionary d = _node_summary(n);
	d["properties"] = _object_properties(n, p_args.get("properties", Array()), p_args.get("all", false));
	List<StringName> metas;
	n->get_meta_list(&metas);
	Dictionary meta;
	for (const StringName &m : metas) {
		meta[m] = _to_json(n->get_meta(m));
	}
	d["meta"] = meta;
	List<Node::GroupInfo> groups;
	n->get_groups(&groups);
	Array g;
	for (const Node::GroupInfo &gi : groups) {
		if (gi.persistent) {
			g.push_back(String(gi.name));
		}
	}
	d["groups"] = g;
	Array children;
	for (int i = 0; i < n->get_child_count(false) && i < 200; i++) {
		Node *c = n->get_child(i, false);
		children.push_back(String(c->get_name()) + " (" + c->get_class() + ")");
	}
	d["child_names"] = children;
	Node3D *n3 = Object::cast_to<Node3D>(n);
	if (n3 && n3->is_inside_tree()) {
		d["global_position"] = _to_json(n3->get_global_position());
	}
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_node_set(const Dictionary &p_args) {
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	Dictionary props = p_args.get("properties", Dictionary());
	if (props.is_empty()) {
		return _error_result("Give at least one property.");
	}
	// Convert everything first, so a bad value changes nothing.
	Vector<Pair<StringName, Variant>> values;
	for (const KeyValue<Variant, Variant> &kv : props) {
		StringName name = String(kv.key);
		Variant::Type type = _property_type(n, name);
		bool is_meta = String(name).begins_with("metadata/");
		if (!is_meta && type == Variant::NIL) {
			bool valid = false;
			n->get(name, &valid);
			if (!valid) {
				return _error_result("\"" + String(name) + "\" is not a property of " + n->get_class() + ". Use class_info or metadata/<name>.");
			}
		}
		Variant v = _from_json(kv.value, type, err);
		if (!err.is_empty()) {
			return _error_result(String(name) + ": " + err);
		}
		values.push_back(Pair<StringName, Variant>(name, v));
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(vformat("MCP: Set %s on %s", String(values[0].first), n->get_name()), UndoRedo::MERGE_DISABLE, n);
	for (const Pair<StringName, Variant> &pv : values) {
		ur->add_do_property(n, pv.first, pv.second);
		ur->add_undo_property(n, pv.first, n->get(pv.first));
	}
	ur->commit_action();
	Array names;
	for (const Pair<StringName, Variant> &pv : values) {
		names.push_back(String(pv.first));
	}
	Dictionary d;
	d["path"] = _node_path(n);
	d["properties"] = _object_properties(n, names, true);
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_node_call(const Dictionary &p_args) {
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	StringName method = String(p_args.get("method", ""));
	if (!n->has_method(method)) {
		return _error_result(n->get_class() + " has no method \"" + String(method) + "\".");
	}
	Array json_args = p_args.get("args", Array());
	// Argument types from the method's signature, when known.
	Vector<Variant::Type> types;
	List<MethodInfo> methods;
	n->get_method_list(&methods);
	for (const MethodInfo &mi : methods) {
		if (mi.name == method) {
			for (const PropertyInfo &a : mi.arguments) {
				types.push_back(a.type);
			}
			break;
		}
	}
	Vector<Variant> args;
	for (int i = 0; i < json_args.size(); i++) {
		Variant v = _from_json(json_args[i], i < types.size() ? types[i] : Variant::NIL, err);
		if (!err.is_empty()) {
			return _error_result(vformat("Argument %d: %s", i, err));
		}
		args.push_back(v);
	}
	Vector<const Variant *> argptrs;
	for (int i = 0; i < args.size(); i++) {
		argptrs.push_back(&args[i]);
	}
	Callable::CallError ce;
	Variant ret = n->callp(method, argptrs.ptrw(), argptrs.size(), ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		return _error_result(Variant::get_call_error_text(n, method, argptrs.ptrw(), argptrs.size(), ce));
	}
	Dictionary d;
	d["result"] = _to_json(ret);
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_node_create(const Dictionary &p_args) {
	Node *root = _scene_root();
	String err;
	Node *parent = _get_node(p_args.get("parent", Variant()), err);
	if (!parent) {
		return _error_result(err);
	}
	String type = p_args.get("type", "");
	String scene = p_args.get("scene", "");
	Node *n = nullptr;
	if (!scene.is_empty()) {
		Ref<PackedScene> ps = ResourceLoader::load(scene);
		if (ps.is_null()) {
			return _error_result("Could not load scene " + scene + ".");
		}
		n = ps->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
		if (!n) {
			return _error_result("Could not instance " + scene + ".");
		}
		n->set_scene_file_path(ps->get_path());
	} else {
		if (type.is_empty()) {
			return _error_result("Give type or scene.");
		}
		if (!ClassDB::class_exists(type) || !ClassDB::is_parent_class(type, "Node") || !ClassDB::can_instantiate(type)) {
			return _error_result("\"" + type + "\" is not a node class that can be created.");
		}
		n = Object::cast_to<Node>(ClassDB::instantiate(type));
	}
	String name = p_args.get("name", "");
	if (!name.is_empty()) {
		n->set_name(name);
	} else if (!scene.is_empty()) {
		n->set_name(scene.get_file().get_basename());
	} else {
		n->set_name(type);
	}
	Dictionary props = p_args.get("properties", Dictionary());
	for (const KeyValue<Variant, Variant> &kv : props) {
		Variant v = _from_json(kv.value, _property_type(n, String(kv.key)), err);
		if (!err.is_empty()) {
			memdelete(n);
			return _error_result(String(kv.key) + ": " + err);
		}
		n->set(String(kv.key), v);
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Create " + String(n->get_name()), UndoRedo::MERGE_DISABLE, parent);
	ur->add_do_method(parent, "add_child", n, true);
	ur->add_do_method(n, "set_owner", root);
	ur->add_do_reference(n);
	ur->add_undo_method(parent, "remove_child", n);
	ur->commit_action();
	return _text_result(_node_summary(n));
}

Dictionary EditorMCPServer::_tool_node_delete(const Dictionary &p_args) {
	Node *root = _scene_root();
	Array paths = p_args.get("paths", Array());
	if (p_args.has("path")) {
		paths.push_back(p_args["path"]);
	}
	Vector<Node *> nodes;
	String err;
	for (int i = 0; i < paths.size(); i++) {
		Node *n = _get_node(paths[i], err);
		if (!n) {
			return _error_result(err);
		}
		if (n == root) {
			return _error_result("The scene root cannot be deleted.");
		}
		nodes.push_back(n);
	}
	if (nodes.is_empty()) {
		return _error_result("Give paths.");
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(vformat("MCP: Delete %d node(s)", nodes.size()), UndoRedo::MERGE_DISABLE, root);
	Array deleted;
	for (Node *n : nodes) {
		Node *parent = n->get_parent();
		List<Node *> owned;
		_collect_owned(n, root, owned);
		deleted.push_back(_node_path(n));
		ur->add_do_method(parent, "remove_child", n);
		ur->add_undo_method(parent, "add_child", n, true);
		ur->add_undo_method(parent, "move_child", n, n->get_index(false));
		for (Node *o : owned) {
			ur->add_undo_method(o, "set_owner", root);
		}
		ur->add_undo_reference(n);
	}
	ur->commit_action();
	Dictionary d;
	d["deleted"] = deleted;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_node_reparent(const Dictionary &p_args) {
	Node *root = _scene_root();
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	if (n == root) {
		return _error_result("The scene root cannot be moved.");
	}
	Node *old_parent = n->get_parent();
	Node *new_parent = p_args.has("parent") ? _get_node(p_args["parent"], err) : old_parent;
	if (!new_parent) {
		return _error_result(err);
	}
	if (new_parent == n || n->is_ancestor_of(new_parent)) {
		return _error_result("A node cannot go under itself.");
	}
	bool keep = p_args.get("keep_global_transform", true);
	int old_index = n->get_index(false);
	int index = p_args.get("index", -1);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Move " + String(n->get_name()), UndoRedo::MERGE_DISABLE, root);
	if (new_parent != old_parent) {
		ur->add_do_method(n, "reparent", new_parent, keep);
		ur->add_undo_method(n, "reparent", old_parent, keep);
	}
	if (index >= 0) {
		ur->add_do_method(new_parent, "move_child", n, index);
	}
	ur->add_undo_method(old_parent, "move_child", n, old_index);
	ur->commit_action();
	return _text_result(_node_summary(n));
}

static void _match_owners(Node *p_orig, Node *p_copy, Node *p_owner, List<Node *> &r_list) {
	if (p_orig->get_owner() == p_owner) {
		r_list.push_back(p_copy);
	}
	int n = MIN(p_orig->get_child_count(false), p_copy->get_child_count(false));
	for (int i = 0; i < n; i++) {
		_match_owners(p_orig->get_child(i, false), p_copy->get_child(i, false), p_owner, r_list);
	}
}

Dictionary EditorMCPServer::_tool_node_duplicate(const Dictionary &p_args) {
	Node *root = _scene_root();
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	if (n == root) {
		return _error_result("The scene root cannot be copied.");
	}
	Node *copy = n->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_GROUPS | Node::DUPLICATE_SCRIPTS | Node::DUPLICATE_USE_INSTANTIATION);
	if (!copy) {
		return _error_result("Could not copy the node.");
	}
	String name = p_args.get("name", "");
	copy->set_name(name.is_empty() ? String(n->get_name()) : name);
	Dictionary props = p_args.get("properties", Dictionary());
	for (const KeyValue<Variant, Variant> &kv : props) {
		Variant v = _from_json(kv.value, _property_type(copy, String(kv.key)), err);
		if (!err.is_empty()) {
			memdelete(copy);
			return _error_result(String(kv.key) + ": " + err);
		}
		copy->set(String(kv.key), v);
	}
	List<Node *> owned;
	_match_owners(n, copy, root, owned);
	Node *parent = n->get_parent();
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Duplicate " + String(n->get_name()), UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(parent, "add_child", copy, true);
	ur->add_do_method(parent, "move_child", copy, n->get_index(false) + 1);
	for (Node *o : owned) {
		ur->add_do_method(o, "set_owner", root);
	}
	ur->add_do_reference(copy);
	ur->add_undo_method(parent, "remove_child", copy);
	ur->commit_action();
	return _text_result(_node_summary(copy));
}

Dictionary EditorMCPServer::_tool_select(const Dictionary &p_args) {
	Array paths = p_args.get("paths", Array());
	EditorSelection *sel = EditorNode::get_singleton()->get_editor_selection();
	Vector<Node *> nodes;
	String err;
	for (int i = 0; i < paths.size(); i++) {
		Node *n = _get_node(paths[i], err);
		if (!n) {
			return _error_result(err);
		}
		nodes.push_back(n);
	}
	sel->clear();
	for (Node *n : nodes) {
		sel->add_node(n);
	}
	if (nodes.size() == 1) {
		EditorInterface::get_singleton()->edit_node(nodes[0]);
	}
	sel->update(false);
	if (bool(p_args.get("focus", false)) && !nodes.is_empty() && Node3DEditor::get_singleton()) {
		Node3DEditor::get_singleton()->get_editor_viewport(0)->focus_selection();
	}
	Dictionary d;
	d["selected"] = paths;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_class_info(const Dictionary &p_args) {
	String cls = p_args.get("class", "");
	bool inherited = p_args.get("inherited", false);
	Object *obj = nullptr;
	if (!ClassDB::class_exists(cls)) {
		String err;
		obj = _get_node(cls, err);
		if (!obj) {
			return _error_result("\"" + cls + "\" is not a class or a node path.");
		}
		cls = obj->get_class();
	}
	Dictionary d;
	d["class"] = cls;
	Array parents;
	for (StringName p = ClassDB::get_parent_class(cls); p != StringName(); p = ClassDB::get_parent_class(p)) {
		parents.push_back(String(p));
	}
	d["inherits"] = parents;

	List<PropertyInfo> props;
	if (obj) {
		obj->get_property_list(&props);
	} else {
		ClassDB::get_property_list(cls, &props, !inherited);
	}
	Array prop_out;
	for (const PropertyInfo &pi : props) {
		if (pi.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		if (!(pi.usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR))) {
			continue;
		}
		String line = String(pi.name) + ": " + (pi.class_name != StringName() ? String(pi.class_name) : Variant::get_type_name(pi.type));
		if (!pi.hint_string.is_empty() && pi.hint_string.length() < 120) {
			line += " [" + pi.hint_string + "]";
		}
		prop_out.push_back(line);
	}
	d["properties"] = prop_out;

	List<MethodInfo> methods;
	if (obj) {
		obj->get_method_list(&methods);
	} else {
		ClassDB::get_method_list(cls, &methods, !inherited);
	}
	Array method_out;
	for (const MethodInfo &mi : methods) {
		if (String(mi.name).begins_with("_")) {
			continue;
		}
		String sig = String(mi.name) + "(";
		int i = 0;
		for (const PropertyInfo &a : mi.arguments) {
			if (i > 0) {
				sig += ", ";
			}
			sig += String(a.name) + ": " + (a.class_name != StringName() ? String(a.class_name) : Variant::get_type_name(a.type));
			i++;
		}
		sig += ")";
		if (mi.return_val.type != Variant::NIL || mi.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) {
			sig += " -> " + (mi.return_val.class_name != StringName() ? String(mi.return_val.class_name) : Variant::get_type_name(mi.return_val.type));
		}
		method_out.push_back(sig);
	}
	d["methods"] = method_out;

	List<MethodInfo> signals;
	ClassDB::get_signal_list(cls, &signals, !inherited);
	Array signal_out;
	for (const MethodInfo &mi : signals) {
		signal_out.push_back(String(mi.name));
	}
	d["signals"] = signal_out;

	List<String> constants;
	ClassDB::get_integer_constant_list(cls, &constants, true);
	Array const_out;
	for (const String &c : constants) {
		const_out.push_back(c);
	}
	d["constants"] = const_out;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_undo(const Dictionary &p_args) {
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	Dictionary d;
	d["undone"] = ur->undo();
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_redo(const Dictionary &p_args) {
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	Dictionary d;
	d["redone"] = ur->redo();
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_view_get(const Dictionary &p_args) {
	int idx = p_args.get("viewport", 0);
	if (!Node3DEditor::get_singleton() || idx < 0 || idx >= int(Node3DEditor::VIEWPORTS_COUNT)) {
		return _error_result("No such 3D viewport.");
	}
	Node3DEditorViewport *vp = Node3DEditor::get_singleton()->get_editor_viewport(idx);
	Dictionary state = vp->get_state();
	Dictionary d;
	d["position"] = _to_json(state.get("position", Vector3()));
	d["x_rotation"] = state.get("x_rotation", 0.0);
	d["y_rotation"] = state.get("y_rotation", 0.0);
	d["distance"] = state.get("distance", 0.0);
	Camera3D *cam = vp->get_camera_3d();
	if (cam) {
		d["camera_transform"] = _to_json(cam->get_global_transform());
		d["fov"] = cam->get_fov();
	}
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_view_set(const Dictionary &p_args) {
	int idx = p_args.get("viewport", 0);
	if (!Node3DEditor::get_singleton() || idx < 0 || idx >= int(Node3DEditor::VIEWPORTS_COUNT)) {
		return _error_result("No such 3D viewport.");
	}
	Node3DEditorViewport *vp = Node3DEditor::get_singleton()->get_editor_viewport(idx);
	Dictionary state = vp->get_state();
	String err;
	if (p_args.has("look_from") && p_args.has("look_at")) {
		Vector3 from = _from_json(p_args["look_from"], Variant::VECTOR3, err);
		Vector3 to = _from_json(p_args["look_at"], Variant::VECTOR3, err);
		if (!err.is_empty()) {
			return _error_result(err);
		}
		Vector3 dir = from - to;
		real_t dist = dir.length();
		if (dist < CMP_EPSILON) {
			return _error_result("look_from and look_at are the same point.");
		}
		dir /= dist;
		// The editor orbit camera sits at position + rotation(x, y) * (0, 0, distance).
		state["position"] = to;
		state["distance"] = dist;
		state["x_rotation"] = Math::asin(CLAMP(dir.y, -1.0f, 1.0f));
		state["y_rotation"] = Math::atan2(-dir.x, dir.z);
	}
	if (p_args.has("position")) {
		state["position"] = _from_json(p_args["position"], Variant::VECTOR3, err);
	}
	if (p_args.has("x_rotation")) {
		state["x_rotation"] = double(p_args["x_rotation"]);
	}
	if (p_args.has("y_rotation")) {
		state["y_rotation"] = double(p_args["y_rotation"]);
	}
	if (p_args.has("distance")) {
		state["distance"] = double(p_args["distance"]);
	}
	if (!err.is_empty()) {
		return _error_result(err);
	}
	vp->set_state(state);
	return _tool_view_get(p_args);
}

Dictionary EditorMCPServer::_tool_screenshot(const Dictionary &p_args) {
	int idx = p_args.get("viewport", 0);
	Ref<Image> img;
	if (idx < 0) {
		img = get_viewport()->get_texture()->get_image();
	} else {
		if (!Node3DEditor::get_singleton() || idx >= int(Node3DEditor::VIEWPORTS_COUNT)) {
			return _error_result("No such 3D viewport.");
		}
		Node3DEditorViewport *ev = Node3DEditor::get_singleton()->get_editor_viewport(idx);
		SubViewport *sv = ev->get_viewport_node();
		if (!sv || !ev->is_visible_in_tree()) {
			// A hidden viewport has no picture; fall back to the window.
			img = get_viewport()->get_texture()->get_image();
		} else {
			img = sv->get_texture()->get_image();
		}
	}
	if (img.is_null() || img->is_empty()) {
		return _error_result("No picture yet; try again after a frame.");
	}
	int max_width = p_args.get("max_width", 1600);
	if (max_width > 0 && img->get_width() > max_width) {
		img->resize(max_width, MAX(1, img->get_height() * max_width / img->get_width()), Image::INTERPOLATE_BILINEAR);
	}
	img->convert(Image::FORMAT_RGB8);
	Vector<uint8_t> png = img->save_png_to_buffer();
	Dictionary image;
	image["type"] = "image";
	image["mimeType"] = "image/png";
	image["data"] = CryptoCore::b64_encode_str(png.ptr(), png.size());
	Dictionary text;
	text["type"] = "text";
	text["text"] = vformat("%dx%d", img->get_width(), img->get_height());
	Array content;
	content.push_back(image);
	content.push_back(text);
	Dictionary res;
	res["content"] = content;
	res["isError"] = false;
	return res;
}

Dictionary EditorMCPServer::_tool_eval(const Dictionary &p_args) {
	String err;
	Node *n = _get_node(p_args.get("path", Variant()), err);
	if (!n) {
		return _error_result(err);
	}
	Ref<Expression> expr;
	expr.instantiate();
	if (expr->parse(p_args.get("expression", "")) != OK) {
		return _error_result(expr->get_error_text());
	}
	Variant ret = expr->execute(Array(), n, false);
	if (expr->has_execute_failed()) {
		return _error_result(expr->get_error_text());
	}
	Dictionary d;
	d["result"] = _to_json(ret);
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_run_gdscript(const Dictionary &p_args) {
	String code = p_args.get("code", "");
	if (!ClassDB::class_exists("GDScript")) {
		return _error_result("GDScript is not available.");
	}
	String source;
	if (code.contains("func run(")) {
		source = code;
		if (!code.contains("extends ")) {
			source = "extends RefCounted\n" + source;
		}
	} else {
		source = "extends RefCounted\nvar scene: Node\nvar editor\nfunc run():\n";
		for (const String &line : code.split("\n")) {
			source += "\t" + line + "\n";
		}
		source += "\treturn null\n";
	}
	// Only tool scripts run in the editor.
	if (!source.contains("@tool")) {
		source = "@tool\n" + source;
	}
	if (!source.contains("var scene")) {
		source += "\nvar scene: Node\nvar editor\n";
	}
	Ref<Script> script(Object::cast_to<Script>(ClassDB::instantiate("GDScript")));
	if (script.is_null()) {
		return _error_result("Could not create a script.");
	}
	int log_before = int(log_total);
	script->set_source_code(source);
	if (script->reload() != OK || !script->can_instantiate()) {
		String errors;
		for (int i = MAX(0, log_lines.size() - int(log_total - log_before)); i < log_lines.size(); i++) {
			errors += log_lines[i] + "\n";
		}
		return _error_result("The script does not compile. " + (errors.is_empty() ? String("See the log tool.") : "\n" + errors));
	}
	Ref<RefCounted> obj;
	obj.instantiate();
	obj->set_script(script);
	if (!obj->get_script_instance()) {
		return _error_result("The script must extend RefCounted.");
	}
	obj->set("scene", _scene_root());
	obj->set("editor", EditorInterface::get_singleton());
	Callable::CallError ce;
	Variant ret = obj->callp("run", nullptr, 0, ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		return _error_result("run() could not be called.");
	}
	Dictionary d;
	d["result"] = _to_json(ret);
	Array output;
	for (int i = MAX(0, log_lines.size() - int(log_total - log_before)); i < log_lines.size(); i++) {
		output.push_back(log_lines[i]);
	}
	d["output"] = output;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_log(const Dictionary &p_args) {
	int64_t since = p_args.get("since", 0);
	int limit = p_args.get("limit", 200);
	int64_t first = int64_t(log_total) - log_lines.size();
	int64_t start = MAX(since, first);
	if (int64_t(log_total) - start > limit) {
		start = int64_t(log_total) - limit;
	}
	Array lines;
	for (int64_t i = start; i < int64_t(log_total); i++) {
		lines.push_back(log_lines[int(i - first)]);
	}
	Dictionary d;
	d["lines"] = lines;
	d["next"] = int64_t(log_total);
	return _text_result(d);
}

static void _list_files(const String &p_dir, const String &p_pattern, bool p_recursive, Array &r_files, Array &r_dirs, int p_limit) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	for (const String &f : da->get_files()) {
		if (r_files.size() >= p_limit) {
			return;
		}
		if (p_pattern.is_empty() || f.matchn(p_pattern)) {
			r_files.push_back(p_dir.path_join(f));
		}
	}
	for (const String &d : da->get_directories()) {
		if (d.begins_with(".")) {
			continue;
		}
		r_dirs.push_back(p_dir.path_join(d));
		if (p_recursive) {
			_list_files(p_dir.path_join(d), p_pattern, true, r_files, r_dirs, p_limit);
		}
	}
}

Dictionary EditorMCPServer::_tool_files(const Dictionary &p_args) {
	String path = p_args.get("path", "res://");
	if (!path.begins_with("res://") && !path.begins_with("user://")) {
		return _error_result("Use a res:// or user:// path.");
	}
	Array files;
	Array dirs;
	_list_files(path, p_args.get("pattern", ""), p_args.get("recursive", false), files, dirs, 5000);
	Dictionary d;
	d["files"] = files;
	d["folders"] = dirs;
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_resource_get(const Dictionary &p_args) {
	String path = p_args.get("path", "");
	Ref<Resource> res = ResourceLoader::load(path);
	if (res.is_null()) {
		return _error_result("Could not load " + path + ".");
	}
	Dictionary d;
	d["class"] = res->get_class();
	d["path"] = res->get_path();
	d["properties"] = _object_properties(res.ptr(), p_args.get("properties", Array()), false);
	return _text_result(d);
}

Dictionary EditorMCPServer::_tool_level_find(const Dictionary &p_args) {
	Node *root = _scene_root();
	if (!root) {
		return _error_result("No scene is open.");
	}
	String kind = p_args.get("kind", "Mobys");
	Node *group = root->get_node_or_null(NodePath("Game/" + kind));
	if (!group) {
		return _error_result("This scene has no Game/" + kind + ".");
	}
	Variant want_class = p_args.get("class", Variant());
	bool by_spawn = p_args.has("spawn_id");
	int64_t want_spawn = p_args.get("spawn_id", -1);
	String err;
	bool by_place = p_args.has("near");
	Vector3 near;
	if (by_place) {
		near = _from_json(p_args["near"], Variant::VECTOR3, err);
		if (!err.is_empty()) {
			return _error_result(err);
		}
	}
	real_t radius = double(p_args.get("radius", 20.0));
	int limit = p_args.get("limit", 100);

	Array found;
	int matched = 0;
	List<Node *> todo;
	for (int i = 0; i < group->get_child_count(false); i++) {
		todo.push_back(group->get_child(i, false));
	}
	while (!todo.is_empty()) {
		Node *n = todo.front()->get();
		todo.pop_front();
		Node3D *n3 = Object::cast_to<Node3D>(n);
		if (!n3) {
			for (int i = 0; i < n->get_child_count(false); i++) {
				todo.push_back(n->get_child(i, false));
			}
			continue;
		}
		Variant cls_id;
		(void)_meta_by_suffix(n, "_class", cls_id);
		Variant cls_name;
		(void)_meta_by_suffix(n, "_class_name", cls_name);
		Variant spawn;
		(void)_meta_by_suffix(n, "_spawn_id", spawn);
		if (want_class.get_type() == Variant::INT || want_class.get_type() == Variant::FLOAT) {
			if (cls_id.get_type() == Variant::NIL || int64_t(cls_id) != int64_t(double(want_class))) {
				continue;
			}
		} else if (want_class.get_type() == Variant::STRING) {
			if (!String(cls_name).matchn(String(want_class))) {
				continue;
			}
		}
		if (by_spawn && (spawn.get_type() == Variant::NIL || int64_t(spawn) != want_spawn)) {
			continue;
		}
		Vector3 pos = n3->is_inside_tree() ? n3->get_global_position() : n3->get_position();
		if (by_place && pos.distance_to(near) > radius) {
			continue;
		}
		matched++;
		if (found.size() < limit) {
			Dictionary d;
			d["path"] = _node_path(n);
			if (cls_id.get_type() != Variant::NIL) {
				d["class"] = cls_id;
			}
			if (cls_name.get_type() != Variant::NIL) {
				d["class_name"] = cls_name;
			}
			if (spawn.get_type() != Variant::NIL) {
				d["spawn_id"] = spawn;
			}
			Variant grp;
			if (!_meta_by_suffix(n, "_group", grp).is_empty()) {
				d["group"] = grp;
			}
			d["position"] = _to_json(pos);
			if (by_place) {
				d["distance"] = pos.distance_to(near);
			}
			found.push_back(d);
		}
	}
	Dictionary d;
	d["objects"] = found;
	d["matched"] = matched;
	return _text_result(d);
}

/* Life. */

void EditorMCPServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_start();
		} break;
		case NOTIFICATION_PROCESS: {
			_poll();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_stop();
		} break;
	}
}

EditorMCPServer::EditorMCPServer() {
	set_name("MCPServer");
	EDITOR_DEF("openrac/mcp/enabled", true);
	EDITOR_DEF("openrac/mcp/port", 7420);
	EDITOR_DEF("openrac/mcp/token", "");
	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(Variant::INT, "openrac/mcp/port", PROPERTY_HINT_RANGE, "1024,65535,1"));

	uint8_t bytes[8];
	if (OS::get_singleton()->get_entropy(bytes, sizeof(bytes)) == OK) {
		session_id = String::hex_encode_buffer(bytes, sizeof(bytes));
	}

	print_handler.printfunc = _print_handler;
	print_handler.userdata = this;
	add_print_handler(&print_handler);
	error_handler.errfunc = _error_handler;
	error_handler.userdata = this;
	add_error_handler(&error_handler);
}

EditorMCPServer::~EditorMCPServer() {
	remove_print_handler(&print_handler);
	remove_error_handler(&error_handler);
}
