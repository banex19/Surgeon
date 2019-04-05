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
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

class SurgeonJIT {
private:
    ExecutionSession ES;
    std::shared_ptr<SymbolResolver> Resolver;
    std::unique_ptr<TargetMachine> TM;
    DataLayout DL;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer<RTDyldObjectLinkingLayer, SimpleCompiler> CompileLayer;
    LocalCXXRuntimeOverrides overrides;

    using OptimizeFunction =
        std::function<std::unique_ptr<Module>(std::unique_ptr<Module>)>;

    IRTransformLayer<decltype(CompileLayer), OptimizeFunction> OptimizeLayer;

public:
    std::unique_ptr<llvm::Module> extraModule = nullptr;

    SurgeonJIT()
        : Resolver(createLegacyLookupResolver(
            ES,
            [this] (const std::string &Name) -> JITSymbol
    {
        return this->resolveSymbol(Name);

    },
            [] (Error Err) { cantFail(std::move(Err), "lookupFlags failed"); })),
        TM(EngineBuilder().selectTarget()), DL(TM->createDataLayout()),
        ObjectLayer(ES,
            [this] (VModuleKey)
    {
        return RTDyldObjectLinkingLayer::Resources{
            std::make_shared<SectionMemoryManager>(), Resolver };
    }),
        CompileLayer(ObjectLayer, SimpleCompiler(*TM)),
        OptimizeLayer(CompileLayer, [this] (std::unique_ptr<Module> M)
    {
        return optimizeModule(std::move(M));
    }),
        overrides([this] (const std::string &S) { return mangle(S); }) {
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    }

    ~SurgeonJIT() {
        // Run any registered destructor.
        overrides.runDestructors();
    }

    TargetMachine &getTargetMachine() { return *TM; }

    VModuleKey addModule(std::unique_ptr<Module> M);

    JITSymbol findSymbol(const std::string Name, bool exportedOnly = true);

    void removeModule(VModuleKey K) {
        cantFail(OptimizeLayer.removeModule(K));
    }

    ExecutionSession& getExecutionSession() { return ES; }

    IRCompileLayer<RTDyldObjectLinkingLayer, SimpleCompiler>& getCompileLayer() { return CompileLayer; }
    DataLayout& GetDataLayout() { return DL; }

private:
    JITSymbol resolveSymbol(const std::string Name);

    std::unique_ptr<Module> optimizeModule(std::unique_ptr<Module> M);

    std::string mangle(StringRef Name);

};



