#include "CallGraph.h"
#include <iostream>

void JITCallGraph::AddModule(llvm::Module & module) {
    for (Function& function : module)
    {
        if (!function.isDeclaration())
        {
            CallGraphNode* parent = GetOrCreateNode(function.getName());
            for (BasicBlock& block : function)
            {
                for (Instruction& inst : block)
                {
                    Function* target = nullptr;
                    if (CallInst *callInst = dyn_cast<CallInst>(&inst))
                    {
                        target = callInst->getCalledFunction();
                    }
                    else if (InvokeInst* invokeInst = dyn_cast<InvokeInst>(&inst))
                    {
                        target = invokeInst->getCalledFunction();
                    }

                    if (target && target->hasName())
                    {
                        AddChild(parent, target->getName());
                    }
                }
            }
        }
    }
}

std::set<std::string> JITCallGraph::GetNodeAndAllChildren(const std::string & name) {
    std::set<std::string> nodes;

    auto parent = GetNode(name);

    if (parent)
    {
        GetAllChildrenForNodeRec(parent, nodes);
    }
    return nodes;
}

inline void JITCallGraph::GetAllChildrenForNodeRec(CallGraphNode * node, std::set<std::string>& nodes) {
    if (nodes.find(node->name) != nodes.end())
        return;

    nodes.insert(node->name);

    auto& children = node->children;
    for (auto child : children)
    {
        GetAllChildrenForNodeRec(child, nodes);
    }
}
