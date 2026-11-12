# MCP server

OpenRAC Editor has a built-in [Model Context Protocol](https://modelcontextprotocol.io) server, so
an AI assistant can read and edit the open level the way you do: find objects, change them, take
pictures of the 3D view and undo what it did.

## Connect

The server starts with the editor and listens on `http://127.0.0.1:7420/mcp`, on this computer
only. It needs a token, which the editor writes, with the address, to `mcp.json` in its settings
folder:

| System | File |
|---|---|
| Linux | `~/.config/openrac_editor/mcp.json` |
| Windows | `%APPDATA%\openrac_editor\mcp.json` |
| macOS | `~/Library/Application Support/openrac_editor/mcp.json` |

Add it to your client as a Streamable HTTP server with the header
`Authorization: Bearer <token>`. For Claude Code:

```sh
claude mcp add --transport http openrac-editor http://127.0.0.1:7420/mcp \
  --header "Authorization: Bearer <token>"
```

**Editor Settings > OpenRAC > MCP** turns it off, or changes the port or the token.

## Tools

| Tool | What it does |
|---|---|
| `editor_state` | Open scenes, the edited scene, the selection, the last undo step |
| `scene_open`, `scene_save` | Open or switch to a level, save it |
| `tree` | List nodes, filtered by class, name, group or metadata |
| `node_get`, `node_set` | Read and change properties and metadata |
| `node_create`, `node_delete`, `node_reparent`, `node_duplicate` | Change the scene tree |
| `node_call` | Call any method of a node |
| `class_info` | Properties, methods, signals and constants of a class or node |
| `undo`, `redo` | The edited scene's history |
| `select` | Select nodes, and move the 3D view to them |
| `view_get`, `view_set` | Read or move a 3D view's camera |
| `screenshot` | A PNG of a 3D view or the whole window |
| `eval` | Run an expression on a node |
| `run_gdscript` | Run GDScript in the editor (`scene` is the level, `editor` is EditorInterface) |
| `log` | The editor's output and errors |
| `files`, `resource_get` | Browse the project and read resources |
| `level_find` | Find mobys, ties, shrubs, volumes and more by class, spawn id or place |

Every change made with `node_set`, `node_create`, `node_delete`, `node_reparent` and
`node_duplicate` is one step in the editor's history, so you see it and can undo it.
`node_call` and `run_gdscript` are not undoable.

## Values

Values are JSON. Vectors and colours are arrays (`[x, y, z]`, `[r, g, b, a]`), a `Transform3D` is
`{"basis": [[x], [y], [z]], "origin": [x, y, z]}`, a resource is `{"resource": "res://..."}` and a
node is `{"node": "Game/Mobys/Vendor_0019"}`. Metadata is set as `metadata/<name>`.
