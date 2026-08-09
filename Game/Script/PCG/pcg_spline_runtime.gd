@tool
extends Node3D
class_name PcgSplineRuntime

## Root helper for maps that host spline-driven PCG tools ([PcgSplineTools]).
##
## Why this exists: FlowGraphNode3D._ready() auto-executes its graph on the very
## first frame. The raycast-drape tools (fence / road) query the physics world
## for a ground hit — but static colliders may not be registered in the physics
## space yet at frame 0, so the rays miss, filter(hit) drops every point, and
## the fence/road come out EMPTY while the dock (no raycast) looks fine.
##
## This node waits a few physics frames after load, then re-executes every
## descendant FlowGraphNode3D once the colliders are live. Drop it as the scene
## root (or anywhere above the flow nodes) and the tools generate reliably on
## F6 / on opening the scene — no manual re-run needed.

## Physics frames to wait before the safety re-execution.
@export var settle_frames : int = 8
## If false, does nothing (useful when a scene drives execution itself).
@export var auto_regenerate : bool = true


func _ready() -> void:
	# Only at real runtime — in the editor the flow nodes stay dormant by design.
	if Engine.is_editor_hint():
		return
	if not auto_regenerate:
		return
	_regenerate_when_ready()


func _regenerate_when_ready() -> void:
	for i in range( settle_frames ):
		await get_tree().physics_frame
	regenerate()


## Re-run every spline PCG graph under this node. Safe to call again by hand
## (e.g. after reshaping a Path3D at runtime) — spawn_meshes clears its previous
## instances first.
func regenerate() -> void:
	var flow_nodes: Array[Node] = []
	_collect( self, flow_nodes )
	for fn in flow_nodes:
		fn.execute()


func _collect( node: Node, out: Array[Node] ) -> void:
	if node is FlowGraphNode3D:
		out.append( node )
	for child in node.get_children():
		_collect( child, out )
