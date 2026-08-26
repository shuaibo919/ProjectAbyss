#pragma once
// Vendored from Reference/SlowTree/src/graph/Nodes.h (commit 10e6c66).
// 改动(见 UPSTREAM_SYNC.md): 剥离 ImGui drawProperties()(编辑器 UI 不随核心移植),
// 仅保留参数结构 + 构造/克隆/参数拷贝。imgui 不在本移植中出现。
#include "NodeGraph.h"
#include "SlowTreeTypes.h"
#include <memory>

// ---- Trunk ----
class TrunkNode : public TreeNode {
public:
    TrunkParams params;
    TrunkNode();
    NodeType    getType()  const override { return NodeType::Trunk; }
    const char* getLabel() const override { return "Trunk"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<TrunkNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const TrunkNode*>(o)) params = p->params; }
};

// ---- Roots ----
class RootsNode : public TreeNode {
public:
    RootsParams params;
    RootsNode();
    NodeType    getType()  const override { return NodeType::Roots; }
    const char* getLabel() const override { return "Roots"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<RootsNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const RootsNode*>(o)) params = p->params; }
};

// ---- Branch ----
class BranchNode : public TreeNode {
public:
    BranchParams params;
    BranchNode();
    NodeType    getType()  const override { return NodeType::Branch; }
    const char* getLabel() const override { return "Branch"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<BranchNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const BranchNode*>(o)) params = p->params; }
};

// ---- Twig ----
class TwigNode : public TreeNode {
public:
    TwigParams params;
    TwigNode();
    NodeType    getType()  const override { return NodeType::Twig; }
    const char* getLabel() const override { return "Twig"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<TwigNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const TwigNode*>(o)) params = p->params; }
};

// ---- LeafCluster ----
class LeafClusterNode : public TreeNode {
public:
    LeafClusterParams params;
    LeafClusterNode();
    NodeType    getType()  const override { return NodeType::LeafCluster; }
    const char* getLabel() const override { return "Leaf Cluster"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<LeafClusterNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const LeafClusterNode*>(o)) params = p->params; }
};

// ---- Spine ----
class SpineNode : public TreeNode {
public:
    SpineParams params;
    SpineNode();
    NodeType    getType()  const override { return NodeType::Spine; }
    const char* getLabel() const override { return "Spine"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<SpineNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const SpineNode*>(o)) params = p->params; }
};

// ---- Frond ----
class FrondNode : public TreeNode {
public:
    FrondParams params;
    FrondNode();
    NodeType    getType()  const override { return NodeType::Frond; }
    const char* getLabel() const override { return "Frond"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<FrondNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const FrondNode*>(o)) params = p->params; }
};

// ---- Export ----
class ExportNode : public TreeNode {
public:
    ExportParams params;
    ExportNode();
    NodeType    getType()  const override { return NodeType::Export; }
    const char* getLabel() const override { return "Export"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<ExportNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const ExportNode*>(o)) params = p->params; }
};

// ---- Custom（脚本自定义枝条） ----
class CustomNode : public TreeNode {
public:
    CustomParams params;
    CustomNode();
    NodeType    getType()  const override { return NodeType::Custom; }
    const char* getLabel() const override { return "Custom"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<CustomNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const CustomNode*>(o)) params = p->params; }
};

// ---- ImportTrunk（导入带骨骼枝干 FBX，作为一株的根） ----
class ImportTrunkNode : public TreeNode {
public:
    ImportTrunkParams params;
    ImportTrunkNode();
    NodeType    getType()  const override { return NodeType::ImportTrunk; }
    const char* getLabel() const override { return "Import Trunk"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<ImportTrunkNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const ImportTrunkNode*>(o)) params = p->params; }
};

// ---- ImportLeaf（导入枝叶单体 FBX，散布原型/预览） ----
class ImportLeafNode : public TreeNode {
public:
    ImportLeafParams params;
    ImportLeafNode();
    NodeType    getType()  const override { return NodeType::ImportLeaf; }
    const char* getLabel() const override { return "Import Leaf"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<ImportLeafNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const ImportLeafNode*>(o)) params = p->params; }
};

// ---- Scatter（沿父级骨骼链撒叶，输出实例化叶原型） ----
class ScatterNode : public TreeNode {
public:
    ScatterParams params;
    ScatterNode();
    NodeType    getType()  const override { return NodeType::Scatter; }
    const char* getLabel() const override { return "Scatter"; }
    std::unique_ptr<TreeNode> clone() const override { return std::make_unique<ScatterNode>(*this); }
    void copyParamsFrom(const TreeNode* o) override { if (auto* p = dynamic_cast<const ScatterNode*>(o)) params = p->params; }
};
