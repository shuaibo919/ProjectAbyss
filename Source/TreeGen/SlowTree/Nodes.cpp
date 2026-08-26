#include "Nodes.h"

// Vendored from Reference/SlowTree/src/graph/Nodes.cpp (commit 10e6c66)。
// 改动(见 UPSTREAM_SYNC.md): 仅保留构造函数。ImGui drawProperties()、
// 文件选择对话框、MeshImport 依赖全部属于编辑器 UI, 不随核心移植。

// ---------- TrunkNode ----------
TrunkNode::TrunkNode() { type = NodeType::Trunk; }

// ---------- BranchNode ----------
BranchNode::BranchNode() { type = NodeType::Branch; }

// ---------- TwigNode ----------
TwigNode::TwigNode() { type = NodeType::Twig; }

// ---------- LeafClusterNode ----------
LeafClusterNode::LeafClusterNode() { type = NodeType::LeafCluster; }

// ---------- RootsNode ----------
RootsNode::RootsNode() { type = NodeType::Roots; }

// ---------- SpineNode ----------
SpineNode::SpineNode() { type = NodeType::Spine; }

// ---------- FrondNode ----------
FrondNode::FrondNode() { type = NodeType::Frond; }

// ---------- ExportNode ----------
ExportNode::ExportNode() { type = NodeType::Export; }

// ---------- CustomNode ----------
CustomNode::CustomNode() {
    type = NodeType::Custom;
    if (params.script.empty()) params.script = kDefaultCustomScript;
}

// ---------- ImportTrunkNode ----------
ImportTrunkNode::ImportTrunkNode() { type = NodeType::ImportTrunk; }

// ---------- ImportLeafNode ----------
ImportLeafNode::ImportLeafNode() { type = NodeType::ImportLeaf; }

// ---------- ScatterNode ----------
ScatterNode::ScatterNode() { type = NodeType::Scatter; }
