#pragma once
// Vendored from Reference/SlowTree/src/graph/NodeGraph.h (commit 10e6c66).
// 改动(见 UPSTREAM_SYNC.md):
//  - #include "NodeTypes.h" → "SlowTreeTypes.h"
//  - TreeNode 移除纯虚 drawProperties()(ImGui 属性面板仅编辑器需要)
//  - rootNode() 由"unordered_map 首个 Trunk"改为"id 最小的根 Trunk"(消除迭代序不确定性)
//  - buildDefaultTemplate() 依赖由 ProjectIO 换成 VtreeIO(实现不变)
#include "SlowTreeTypes.h"
#include <godot_cpp/variant/vector2.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

using NodeId = uint32_t;
using PinId  = uint32_t;
using LinkId = uint32_t;

static constexpr NodeId  INVALID_NODE = 0;
static constexpr PinId   INVALID_PIN  = 0;
static constexpr LinkId  INVALID_LINK = 0;

struct Pin {
    PinId  id     = INVALID_PIN;
    NodeId owner  = INVALID_NODE;
    bool   input  = false;   // true=输入 false=输出
};

struct Link {
    LinkId id      = INVALID_LINK;
    PinId  fromPin = INVALID_PIN;  // 上游节点输出Pin
    PinId  toPin   = INVALID_PIN;  // 下游节点输入Pin
};

// 注释框(类 UE/SD 的 Comment/Group): 纯编辑器标注, 不参与树生成。
// 可框住一片节点, 顶部有可编辑的大标题。使用独立 ID 段避免与节点/Pin/连线冲突。
struct CommentFrame {
    NodeId    id    = INVALID_NODE;
    std::string text = "Comment";
    godot::Vector2 editorPos = {0.0f, 0.0f};
    godot::Vector2 size      = {320.0f, 180.0f};
};

// 抽象节点基类
class TreeNode {
public:
    NodeId    id      = INVALID_NODE;
    NodeType  type;
    godot::Vector2 editorPos = {0.0f, 0.0f};
    bool      dirty   = true;

    Pin              outputPin;
    std::vector<Pin> inputPins;   // 当前版本每个节点只有1个输入

    virtual ~TreeNode() = default;
    virtual NodeType    getType()  const = 0;
    virtual const char* getLabel() const = 0;
    // 深拷贝自身（含参数），用于复制/粘贴的剪贴板暂存
    virtual std::unique_ptr<TreeNode> clone() const = 0;
    // 若 other 与自身同类型，则拷贝其参数（粘贴时把剪贴板参数灌入新节点）
    virtual void copyParamsFrom(const TreeNode* other) = 0;
};

class NodeGraph {
public:
    NodeId   addNode(NodeType type, godot::Vector2 pos = {0.0f, 0.0f});
    void     removeNode(NodeId id);
    TreeNode* getNode(NodeId id);
    const std::unordered_map<NodeId, std::unique_ptr<TreeNode>>& nodes() const;

    LinkId   addLink(PinId fromPin, PinId toPin);
    void     removeLink(LinkId id);
    const std::vector<Link>& links() const { return m_links; }

    // 按pin查link
    LinkId   linkFromPin(PinId pin) const;

    // 顺Link找子节点（输出Pin -> 下游节点）
    std::vector<TreeNode*> childrenOf(NodeId id) const;
    // 找根节点（没有输入连接的Trunk）
    TreeNode* rootNode() const;

    void markDirty();
    bool isDirty() const { return m_dirty; }
    void clearDirty()    { m_dirty = false; }

    // 构建默认模板（Trunk->Branch->Twig->LeafCluster）
    void buildDefaultTemplate();

    // 清空整个图并重置 ID 计数器（用于新建/读取工程）
    void clear();

    // 便捷：添加子节点并自动连线到parent的outputPin
    NodeId addChildNode(NodeId parentId, NodeType type);

    // ---- 注释框(编辑器标注, 不影响生成, 不置 dirty) ----
    NodeId addComment(godot::Vector2 pos = {0.0f, 0.0f});
    void   removeComment(NodeId id);
    std::vector<CommentFrame>&       comments()       { return m_comments; }
    const std::vector<CommentFrame>& comments() const { return m_comments; }
    CommentFrame* getComment(NodeId id);
    bool   isComment(NodeId id) const;

private:
    std::unordered_map<NodeId, std::unique_ptr<TreeNode>> m_nodes;
    std::vector<Link> m_links;
    std::vector<CommentFrame> m_comments;
    // imgui-node-editor 中 Node/Pin/Link 共用同一 ID 空间，
    // 三者必须互不重叠，否则会触发 ID 冲突且节点无法拖动。
    NodeId   m_nextNodeId = 1;
    PinId    m_nextPinId  = 10000;
    LinkId   m_nextLinkId = 100000;
    NodeId   m_nextCommentId = 1000000;  // 注释框独立 ID 段(与 node/pin/link 互不重叠)
    bool     m_dirty      = true;

    // Pin -> owner node，用于反查
    std::unordered_map<PinId, NodeId> m_pinOwner;
    // Pin -> Link
    std::unordered_map<PinId, LinkId> m_pinToLink;
};
