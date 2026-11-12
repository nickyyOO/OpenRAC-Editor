/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: a Model Context Protocol server, so AI tools can read and edit levels. */

#pragma once

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/string/print_string.h"
#include "scene/main/node.h"

class EditorMCPServer : public Node {
	GDCLASS(EditorMCPServer, Node);

	struct Connection {
		Ref<StreamPeerTCP> peer;
		Vector<uint8_t> in;
		Vector<uint8_t> out;
		int out_pos = 0;
		uint64_t started_msec = 0;
		bool responded = false;
	};

	Ref<TCPServer> server;
	List<Connection> connections;
	String token;
	int port = 0;
	String session_id;

	// The last lines of output and errors, for the log tool.
	PrintHandlerList print_handler;
	ErrorHandlerList error_handler;
	Vector<String> log_lines;
	uint64_t log_total = 0;

	static void _print_handler(void *p_this, const String &p_string, bool p_error, bool p_rich);
	static void _error_handler(void *p_this, const char *p_func, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type);
	void _add_log(const String &p_line);

	void _start();
	void _stop();
	void _poll();
	bool _handle_connection(Connection &p_conn);
	String _handle_http(const String &p_method, const String &p_path, const HashMap<String, String> &p_headers, const String &p_body, int &r_status, Vector<String> &r_extra_headers);
	Variant _handle_rpc(const Dictionary &p_request, bool &r_is_notification);
	Dictionary _rpc_error(const Variant &p_id, int p_code, const String &p_message) const;

	Array _tool_list() const;
	Dictionary _call_tool(const String &p_name, const Dictionary &p_args);

	// Tools.
	Dictionary _tool_editor_state(const Dictionary &p_args);
	Dictionary _tool_scene_open(const Dictionary &p_args);
	Dictionary _tool_scene_save(const Dictionary &p_args);
	Dictionary _tool_tree(const Dictionary &p_args);
	Dictionary _tool_node_get(const Dictionary &p_args);
	Dictionary _tool_node_set(const Dictionary &p_args);
	Dictionary _tool_node_call(const Dictionary &p_args);
	Dictionary _tool_node_create(const Dictionary &p_args);
	Dictionary _tool_node_delete(const Dictionary &p_args);
	Dictionary _tool_node_reparent(const Dictionary &p_args);
	Dictionary _tool_node_duplicate(const Dictionary &p_args);
	Dictionary _tool_select(const Dictionary &p_args);
	Dictionary _tool_class_info(const Dictionary &p_args);
	Dictionary _tool_undo(const Dictionary &p_args);
	Dictionary _tool_redo(const Dictionary &p_args);
	Dictionary _tool_view_get(const Dictionary &p_args);
	Dictionary _tool_view_set(const Dictionary &p_args);
	Dictionary _tool_screenshot(const Dictionary &p_args);
	Dictionary _tool_eval(const Dictionary &p_args);
	Dictionary _tool_run_gdscript(const Dictionary &p_args);
	Dictionary _tool_log(const Dictionary &p_args);
	Dictionary _tool_files(const Dictionary &p_args);
	Dictionary _tool_resource_get(const Dictionary &p_args);
	Dictionary _tool_level_find(const Dictionary &p_args);

	// Helpers.
	Node *_scene_root() const;
	Node *_get_node(const Variant &p_path, String &r_error) const;
	String _node_path(const Node *p_node) const;
	Dictionary _node_summary(const Node *p_node) const;
	Variant _to_json(const Variant &p_value, int p_depth = 0) const;
	Variant _from_json(const Variant &p_json, Variant::Type p_type, String &r_error) const;
	Variant::Type _property_type(Object *p_object, const StringName &p_property) const;
	Dictionary _object_properties(Object *p_object, const Array &p_only, bool p_all) const;
	void _collect_owned(Node *p_node, Node *p_owner, List<Node *> &r_list) const;
	static String _meta_by_suffix(const Node *p_node, const String &p_suffix, Variant &r_value);

	Dictionary _text_result(const Variant &p_data, bool p_error = false) const;
	Dictionary _error_result(const String &p_message) const;

protected:
	void _notification(int p_what);

public:
	static String get_info_path();

	EditorMCPServer();
	~EditorMCPServer();
};
