#pragma once
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include <unordered_map>
#include <set>

using namespace llvm;


struct CallGraphNode {
    CallGraphNode(const std::string& name) : name(name) {}

    void AddChild(CallGraphNode* child) { children.insert(child); }

    std::string name;
    std::set<CallGraphNode*> children;
};

class JITCallGraph {
public:
    JITCallGraph() {}

    ~JITCallGraph() {
        for (auto& node : nodes)
        {
            delete node.second;
        }
        nodes.clear();
    }

    void AddModule(llvm::Module& module);



    std::set<std::string> GetNodeAndAllChildren(const std::string& name);


private:
    void GetAllChildrenForNodeRec(CallGraphNode* node, std::set<std::string>& nodes);
    
    void AddChild(const std::string& parent, const std::string& child) {
        auto parentNode = GetOrCreateNode(parent);
        auto childNode = GetOrCreateNode(child);
        parentNode->AddChild(childNode);
    }

    void AddChild(CallGraphNode* parent, const std::string& child) {
        parent->AddChild(GetOrCreateNode(child));
    }

    CallGraphNode* GetNode(const std::string& name) {
        return nodes[name];
    }

    CallGraphNode* GetOrCreateNode(const std::string& name) {
        auto node = nodes[name];
        if (!node)
        {
            node = new CallGraphNode(name);
            nodes[name] = node;
        }

        return node;
    }

    std::unordered_map<std::string, CallGraphNode*> nodes;
};