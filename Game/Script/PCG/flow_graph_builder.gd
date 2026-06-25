@tool
extends RefCounted
class_name FlowGraphBuilder

## Programmatic builder for Flow (PCGODOT) graphs — path B.
##
## Constructs a FlowGraphResource entirely in code: no editor dock, no
## GUI, no `editor: Control` dependency. The output is the same `data`
## dictionary the editor would have saved, so FlowNodeIO.evaluate_graph()
## (the runtime path driven by FlowGraphNode3D) consumes it unchanged.
##
## Schema mirrors what the editor writes (see e.g. demo/graph01.tres):
##   data = {
##     "type": "flow_graph_nodes", "version": 1,
##     "frames": [], "min_pos": Vector2,
##     "nodes": [ { template, name, position, args_port, settings, ... } ],
##     "links": [ { from_node, from_port, to_node, to_port, keep_alive } ],
##   }
##
## Node `settings` need only carry the fields you want to override; the
## evaluator fills the rest from each node's NodeSettings defaults
## (FlowNodeIO.dict_to_resource only writes keys present in the dict).

# Auto-incrementing suffix so every node name is unique even when several
# nodes share a template, matching the editor's id_NNNN_<template> scheme.
var _counter: int = 0

var _nodes: Array[Dictionary] = []
var _links: Array[Dictionary] = []

## Add a node. Returns its generated unique name (use it to wire links).
## @param template  Node template id (e.g. "grid", "transform", "spawn_meshes").
## @param settings  Sparse overrides for that node's NodeSettings fields.
## @param position  Graph position; purely cosmetic for runtime execution.
## @param input_ports  { "Port Label": port_index } entries to mark connected.
func AddNode( template: String, settings: Dictionary = {}, position: Vector2 = Vector2.ZERO, input_ports: Dictionary = {} ) -> StringName:
	var node_name := StringName( "id_%04d_%s" % [ _counter, template ] )
	_counter += 1

	var args_port: Dictionary = {}
	for label in input_ports:
		args_port[ label ] = {
			"connected": true,
			"port": int( input_ports[ label ] ),
		}

	_nodes.append( {
		"template": template,
		"name": node_name,
		"position": position,
		"args_port": args_port,
		"settings": settings,
		"show_disconnected_inputs": false,
	} )
	return node_name

## Wire an output port of one node into an input port of another.
func Connect( from_node: StringName, from_port: int, to_node: StringName, to_port: int ) -> void:
	_links.append( {
		"from_node": from_node,
		"from_port": from_port,
		"to_node": to_node,
		"to_port": to_port,
		"keep_alive": false,
	} )

## Assemble the FlowGraphResource. Not @tool-only — works at runtime.
func Build() -> FlowGraphResource:
	var graph := FlowGraphResource.new()
	graph.data = {
		"type": "flow_graph_nodes",
		"version": 1,
		"frames": [],
		"min_pos": Vector2.ZERO,
		"nodes": _nodes,
		"links": _links,
	}
	return graph

## Convenience: build and persist to a `.tres` in one call.
func SaveTo( path: String ) -> Error:
	var graph := Build()
	return ResourceSaver.save( graph, path )
