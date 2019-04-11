#pragma once
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "JITMemoryManager.h"
#include <algorithm>
#include <memory>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include "CallGraph.h"

using namespace llvm;
using namespace llvm::orc;


class SurgeonJIT;

class ObjectListener {
public:
    ObjectListener(SurgeonJIT *JIT) : listener(new JITEventListener) {}
    template <typename ObjT, typename LoadResult>
    void operator()(VModuleKey H, const ObjT &Object, const LoadResult &LOS) {

        auto sizes = llvm::object::computeSymbolSizes(Object);

        for (auto& size : sizes)
        {
            if (size.second > 0)
            {
                symbolSizes[size.first.getName().get()] = size.second;
            }
        }

    }

    size_t GetSizeForSymbol(const std::string& name) { return symbolSizes[name]; }

private:
    std::unordered_map<std::string, size_t> symbolSizes;
    std::unique_ptr<JITEventListener> listener;
};


class SurgeonJIT {

    using Module = llvm::Module;

private:
    ObjectListener listener;

    ExecutionSession ES;
    std::shared_ptr<SymbolResolver> Resolver;
    std::unique_ptr<TargetMachine> TM;
    DataLayout DL;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer<RTDyldObjectLinkingLayer, SimpleCompiler> CompileLayer;
    LocalCXXRuntimeOverrides overrides;

    JITCallGraph callGraph;
    std::vector<std::unique_ptr<Module>> modules;
    std::unordered_map<std::string, size_t> functionModuleMapping;
    std::unordered_map<llvm::Module*, bool> modulesCSIEnabled;

    using OptimizeFunction =
        std::function<std::unique_ptr<Module>(std::unique_ptr<Module>)>;

    IRTransformLayer<decltype(CompileLayer), OptimizeFunction> OptimizeLayer;

public:

    SurgeonJIT()
        :
        listener(this),
        Resolver(createLegacyLookupResolver(
            ES,
            [this] (const std::string &Name) -> JITSymbol
    {
        return this->resolveSymbol(Name);

    },
            [] (Error Err) { cantFail(std::move(Err), "lookupFlags failed"); })),
        TM(EngineBuilder().selectTarget()),
        DL(TM->createDataLayout()),
        ObjectLayer(ES,
            [this] (VModuleKey)
    {
        return RTDyldObjectLinkingLayer::Resources{
            std::make_shared<JITMemoryManager>(), Resolver };
    }, std::ref(listener)),
        CompileLayer(ObjectLayer, SimpleCompiler(*TM)),
        OptimizeLayer(CompileLayer, [this] (std::unique_ptr<Module> M)
    {
        return optimizeModule(std::move(M));
    }),
        overrides([this] (const std::string &S) { return mangle(S); }) {

        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
        LoadAndAddModule("surgeon_inst_helpers.bc", true);
    }

    ~SurgeonJIT() {
        // Run any registered destructor.
        overrides.runDestructors();
    }

    TargetMachine &getTargetMachine() { return *TM; }

    VModuleKey addModule(std::unique_ptr<Module> M, bool enableCSI = false, bool addToDatabase = true);

    void* RecompileFunction(const std::string& functionName, bool enableCSI);
    void CallCSIConstructorForModule(VModuleKey& key, bool mustExist = false);

    JITSymbol findSymbol(const std::string Name, bool exportedOnly = true);

    void removeModule(VModuleKey K) {
        cantFail(OptimizeLayer.removeModule(K));
    }

    void preemptFunction(const std::string& functionName, const std::string& preempter);

    ExecutionSession& getExecutionSession() { return ES; }

    IRCompileLayer<RTDyldObjectLinkingLayer, SimpleCompiler>& getCompileLayer() { return CompileLayer; }
    DataLayout& GetDataLayout() { return DL; }
    JITCallGraph& GetCallGraph() { return callGraph; }



    size_t GetSizeForSymbol(const std::string& name) { return listener.GetSizeForSymbol(name); }

private:
    JITSymbol resolveSymbol(const std::string Name);

    std::unique_ptr<Module> optimizeModule(std::unique_ptr<Module> M);

    std::string mangle(StringRef Name);


    void LoadHelperModule(LLVMContext& context);
    std::unique_ptr<Module> helperModule;

    void LoadAndAddModule(const std::string& moduleName, bool enableCSI);


};
