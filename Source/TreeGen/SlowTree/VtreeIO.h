#pragma once
// Vendored from Reference/SlowTree/src/io/ProjectIO.cpp/.h 解析侧 (commit 10e6c66)。
// 仅保留 .vtree 读取: 写入(save/writeNode)、OBJ/FBX/USD 导出、单节点 kv 接口
// 是应用/转换器职责(转换器为 Python 侧 tools/convert_vtree_preset.py), 不随核心移植。
#include <istream>
#include <string>

class NodeGraph;
struct LightingParams;

namespace VtreeIO {
    // 从 .vtree 文本加载节点图(先 clear 再重建)。lighting 可选, 非空时读出光照块。
    // 返回 false = 打开失败或首行非 "VEGTOOL"(调用方报错)。
    bool load(NodeGraph& graph, const std::string& path, LightingParams* lighting = nullptr);
    // 加载内置默认工程(HelloTree)——与上游 loadDefaultTemplate 逐字一致。
    void loadDefaultTemplate(NodeGraph& graph);
    // 从内存文本流(VEGTOOL 格式)加载节点图(供预设/自测使用; load 文件版共用同一 parseStream)。
    bool loadFromStream(NodeGraph& graph, std::istream& stream);
    // 内置默认工程(HelloTree)的 .vtree 文本(供预设系统/转换器取用, 与 kDefaultTemplate 同一份)。
    const std::string& defaultTemplateText();
}
