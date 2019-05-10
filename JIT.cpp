#include <JIT.h>
#include <llvm/Transforms/Instrumentation.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include <iostream>

VModuleKey SurgeonJIT::addModule(std::unique_ptr<llvm::Module> M, bool enableCSI, bool addToDatabase) {
    if (addToDatabase)
    {

        callGraph.AddModule(*M);

        size_t moduleIndex = modules.size();
        for (Function& function : *M)
        {
            if (!function.isDeclaration())
                functionModuleMapping[function.getName()] = moduleIndex + 1;
        }

        for (auto& global : M->getGlobalList())
        {
            if (global.getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage)
            {
                //global.setLinkage(GlobalValue::LinkageTypes::InternalLinkage);
            }
        }

        modules.push_back(std::move(CloneModule(*M)));
    }

    modulesCSIEnabled[M.get()] = enableCSI;

    // Add the module to the JIT with a new VModuleKey.
    auto K = ES.allocateVModule();

    isInstrumented[K] = !addToDatabase;

    cantFail(OptimizeLayer.addModule(K, std::move(M)));

    return K;
}

template <typename T>
bool IsInSet(const T& element, std::set<T>& s) {
    return s.find(element) != s.end();
}

void RemoveConstrsDestrAliasesAndSetGlobalsExternal(llvm::Module& M, bool removeAlises = true, bool setGlobalsExternal = true) {
    std::vector<GlobalValue*> toRemove;
    for (auto& global : M.getGlobalList())
    {
        if (!global.isDeclaration())
        {
            if (global.hasName() && (global.getName() == "llvm.global_ctors" || global.getName() == "llvm.global_dtors"))
                toRemove.push_back(&global);

            else if (setGlobalsExternal && !global.hasComdat() && global.getLinkage() != llvm::GlobalValue::LinkageTypes::PrivateLinkage)
            {
                global.setInitializer(nullptr);
                global.setLinkage(llvm::GlobalValue::LinkageTypes::ExternalLinkage);
            }
            else
            {
            }

        }
    }

    if (removeAlises)
    {
        for (auto& alias : M.aliases())
        {
            alias.replaceAllUsesWith(alias.getAliasee());
            toRemove.push_back(&alias);
        }
    }

    for (auto& val : toRemove)
    {
        val->eraseFromParent();
    }
}

void* SurgeonJIT::RecompileFunction(const std::string& functionName, bool enableCSI) {
    if (functionModuleMapping.find(functionName) == functionModuleMapping.end())
    {
        llvm::errs() << "Function " << functionName << " to be recompiled cannot be found\n";
        return nullptr;
    }

    std::string instrumentationPrefix = GenerateInstrumentationPrefix(functionName);

    size_t originalFunctionSize = GetSizeForSymbol(functionName);
    // llvm::errs() << "Size of original function: " << originalFunctionSize << "\n";

    auto functionSetWhole = callGraph.GetNodeAndAllChildren(functionName);

    std::vector<VModuleKey> keys;
    VModuleKey entryKey;

    std::unordered_map<size_t, std::set<std::string>> functionsByModule;
    std::set<std::string> allFunctions;
    for (auto& fn : functionSetWhole)
    {
        if (functionModuleMapping[fn] != 0)
        {
            functionsByModule[functionModuleMapping[fn]].insert(fn);
            allFunctions.insert(fn);
            //  llvm::errs() << "Will instrument " << fn << "\n";
        }
    }

    for (auto moduleFunctionsPair : functionsByModule)
    {
        size_t moduleIndex = moduleFunctionsPair.first;
        auto& functionSet = moduleFunctionsPair.second;
        assert(moduleIndex != 0);
        moduleIndex--;

        auto module = std::move(CloneModule(*modules[moduleIndex]));

        std::vector<Function*> targetFunctions;

        RemoveConstrsDestrAliasesAndSetGlobalsExternal(*module);


        for (auto& function : *module)
        {
            std::string name = function.getName();
            if (!function.isDeclaration() && !function.hasComdat() && !IsInSet(name, functionSet) &&
                function.getLinkage() != llvm::GlobalValue::LinkageTypes::PrivateLinkage)
            {
                if (GetSizeForSymbol(function.getName()) > 0)
                {
                    function.deleteBody();
                    function.setLinkage(llvm::GlobalValue::LinkageTypes::ExternalLinkage);
                }
            }

            if (IsInSet(name, functionSet) && !function.isDeclaration())
            {
                targetFunctions.push_back(&function);

                bool entryPoint = false;
                if (name == functionName)
                {
                    entryPoint = true;
                }

                std::string newName = instrumentationPrefix + function.getName().str();
                if (module->getFunction(newName))
                {
                    llvm::errs() << "ERROR: Function " << newName << " already exists in module " << moduleIndex << "\n";
                    exit(-1);
                }

                std::string oldName = function.getName().str();
                function.setName(newName);
                function.setLinkage(GlobalValue::LinkageTypes::WeakODRLinkage);
                // llvm::errs() << "Changed name from " << oldName << " to " << function.getName().str() << "\n";
            }
        }

        assert(targetFunctions.size() == functionSet.size());

        for (auto& functionPtr : targetFunctions)
        {
            Function& function = *functionPtr;
            for (auto& block : function)
            {
                for (auto& inst : block)
                {
                    Function* target = nullptr;
                    if (CallInst * callInst = dyn_cast<CallInst>(&inst))
                    {
                        target = callInst->getCalledFunction();
                    }
                    else if (InvokeInst * invokeInst = dyn_cast<InvokeInst>(&inst))
                    {
                        target = invokeInst->getCalledFunction();
                    }

                    if (target && target->hasName())
                    {
                        if (IsInSet((std::string)target->getName(), allFunctions))
                        {
                            Function* substituteFunction = (Function*)module->getOrInsertFunction(
                                instrumentationPrefix + target->getName().str(),
                                target->getFunctionType());

                            if (CallInst * callInst = dyn_cast<CallInst>(&inst))
                            {
                                callInst->setCalledFunction(substituteFunction);
                            }
                            else if (InvokeInst * invokeInst = dyn_cast<InvokeInst>(&inst))
                            {
                                invokeInst->setCalledFunction(substituteFunction);
                            }
                        }
                    }
                }
            }
        }

        auto key = addModule(std::move(module), enableCSI, false);
        keys.push_back(key);

        if (IsInSet(functionName, functionSet))
        {
            // This VModule will contain the entry point.
            entryKey = key;
        }
    }

    VModuleKey surgeonKey;
    {
        // Insert the interactive loop.
        FunctionType* functionType = nullptr;

        auto module = std::move(CloneModule(*modules[functionModuleMapping[functionName] - 1]));

        auto helperModule = LoadHelperModule(module->getContext());
        std::unique_ptr<Module> helper = std::move(CloneModule(*helperModule));

        RemoveConstrsDestrAliasesAndSetGlobalsExternal(*helper, false, false);

        llvm::Linker linker(*module);
        linker.linkInModule(std::move(helper), llvm::Linker::Flags::None);

        RemoveConstrsDestrAliasesAndSetGlobalsExternal(*module);

        for (auto& function : *module)
        {
            std::string name = function.getName();
            if (!function.isDeclaration() && !function.hasComdat() && name != functionName &&
                function.getLinkage() != llvm::GlobalValue::LinkageTypes::PrivateLinkage)
            {
                if (GetSizeForSymbol(function.getName()) > 0)
                {
                    function.deleteBody();
                    function.setLinkage(llvm::GlobalValue::LinkageTypes::ExternalLinkage);
                }
            }

            if (name == functionName)
            {
                functionType = function.getFunctionType();
                function.setName(instrumentationPrefix + "_original_" + function.getName());
            }
        }

        assert(functionType != nullptr);

        ValueToValueMapTy vMap;
        SmallVector<ReturnInst*, 10> returns;

        Function * preemptTemplate = module->getFunction("interactive_preempt");
        Function * cycleTemplate = module->getFunction("interactive_cycle");
        Function * cycle = nullptr;

        auto & context = module->getContext();
        {
            GlobalVariable* addressGlobal = (GlobalVariable*)module->getOrInsertGlobal(instrumentationPrefix + "address",
                llvm::IntegerType::getInt64Ty(context));
            addressGlobal->setConstant(false);
            addressGlobal->setInitializer(llvm::ConstantInt::getIntegerValue(llvm::IntegerType::getInt64Ty(context),
                APInt(64, 0x123456789)));

            cycle = (Function*)module->getOrInsertFunction("__surgeon_cycle_" + functionName,
                FunctionType::get(llvm::Type::getVoidTy(context), functionType->params(), false));
            llvm::BasicBlock* block = BasicBlock::Create(context, "entryBlock", cycle);
            IRBuilder<> builder{ block };
            Value* instrumentedAddress = builder.CreateLoad(addressGlobal);

            instrumentedAddress = builder.CreateIntToPtr(instrumentedAddress, functionType->getPointerTo());

            std::vector<Value*> args;
            for (Argument& arg : cycle->args())
            {
                args.push_back(&arg);
            }

            CallInst* call = builder.CreateCall(cycleTemplate, std::vector<Value*>());
            builder.CreateRetVoid();
            InlineFunctionInfo info;
            // llvm::errs() << "Inlining function " << *call->getCalledFunction() << "\n";
            if (!InlineFunction(call, info, nullptr, false)) {

                llvm::errs() << "Can't inline function " << *call->getCalledFunction() << "\n";

                assert(false);
            }


            std::vector<CallInst*> callsToReplace;
            for (auto& block : *cycle)
            {
                for (auto& inst : block)
                {
                    CallInst* fakeCall = dyn_cast<CallInst>(&inst);
                    if (fakeCall)
                    {
                        if (fakeCall->getCalledFunction()->getName() == "interactive_fake_call")
                        {
                            callsToReplace.push_back(fakeCall);
                        }
                    }
                }

            }
            assert(callsToReplace.size() > 0);

            for (auto& callToReplace : callsToReplace)
            {
                builder.SetInsertPoint(callToReplace);
                CallInst* actualCall = builder.CreateCall(instrumentedAddress, args, "call_to_actual_function");
            }
        }
        {
            llvm::Function* interactiveTrampoline = (Function*)module->getOrInsertFunction(instrumentationPrefix + "_interactive_" + functionName, functionType);
            llvm::BasicBlock* block = BasicBlock::Create(context, "entryBlock", interactiveTrampoline);
            IRBuilder<> builder{ block };


            std::vector<Value*> args;
            for (Argument& arg : interactiveTrampoline->args())
            {
                args.push_back(&arg);
            }

            CallInst* call = builder.CreateCall(preemptTemplate, std::vector<Value*>());
            InlineFunctionInfo info;
            assert(InlineFunction(call, info));
            CallInst* callCycle = dyn_cast<CallInst>(&block->getInstList().front());
            assert(callCycle != nullptr && callCycle->getCalledFunction()->getName() == "call_to_interactive_cycle");

            call = builder.CreateCall((Function*)module->getOrInsertFunction(instrumentationPrefix + "_original_" + functionName, functionType), args);
            if (!functionType->getReturnType()->isVoidTy())
            {
                builder.CreateRet(call);
            }
            else {
                builder.CreateRetVoid();
            }

            builder.SetInsertPoint(callCycle);
            builder.CreateCall(cycle, args);
        }

        if (llvm::verifyModule(*module, &llvm::errs()))
        {
            llvm::errs() << "Broken trampoline\n";
            exit(-1);
        }

        surgeonKey = addModule(std::move(module), false, false);
    }

    // This is mainly for debug, to fail early and get meaningful errors
    // if any symbol cannot be resolved.
    if (auto Err = OptimizeLayer.emitAndFinalize(keys[0]))
    {
        llvm::errs() << "Error finalizing first: " << Err << "\n";
        exit(-1);
    }

    /*if (entryKey != 0)
    {
        if (auto Err = OptimizeLayer.emitAndFinalize(entryKey))
        {
            llvm::errs() << "Error finalizing: " << Err << "\n";
            exit(-1);
        }
    } */


    if (enableCSI)
    {
        for (auto key : keys)
        {
            //   llvm::errs() << "Calling CSI constructor for module " << key << "\n";
            CallCSIConstructorForModule(key, true);
        }
    }

    //llvm::errs() << "Finding new symbol\n";
    std::string interactiveFunctionName = instrumentationPrefix + "_interactive_" + functionName;

    void* finalAddr = (void*)OptimizeLayer.findSymbolIn(entryKey, instrumentationPrefix + functionName, false).getAddress().get();
    void* trampAddr = (void*)(findSymbol(interactiveFunctionName).getAddress().get());


    size_t trampolineSize = GetOveriddenSizeForSymbol(interactiveFunctionName);
    if (trampolineSize > originalFunctionSize) {
        std::cout << "Trampoline size: " << trampolineSize << ", original size: " << originalFunctionSize << "\n";
        assert(GetSizeForSymbol(interactiveFunctionName) <= originalFunctionSize);
    }

    void* pointerToAddr = (void*)OptimizeLayer.findSymbolIn(surgeonKey, instrumentationPrefix + "address", false).getAddress().get();
    assert(trampAddr);
    assert(finalAddr);
    assert(pointerToAddr);

    *((uintptr_t*)(pointerToAddr)) = (uintptr_t)finalAddr;
    preemptFunction(functionName, interactiveFunctionName);

    return finalAddr;
}

void SurgeonJIT::CallCSIConstructorForModule(VModuleKey & key, bool mustExist) {
    auto csiConstructorSymbol = CompileLayer.findSymbolIn(key, "csirt.unit_ctor", false);
    if (csiConstructorSymbol)
    {
        void* csiConstructor = (void*)cantFail(csiConstructorSymbol.getAddress());
        void(*fn)(void) = (void(*)(void))csiConstructor;
        fn();
    }
    else
    {
        if (mustExist)
        {
            llvm::errs() << "CSI constructor not found for VModule " << key << "\n";
            exit(-1);
        }
    }
}

JITSymbol SurgeonJIT::findSymbol(const std::string Name, bool exportedOnly) {
    std::string MangledName = mangle(Name);
    return OptimizeLayer.findSymbol(MangledName, exportedOnly);
}

void SurgeonJIT::preemptFunction(const std::string & functionName, const std::string & preempter) {
    if (auto sym = findSymbol(functionName))
    {
        void* addr = (void*)sym.getAddress().get();
        assert(addr != nullptr);

        void* newAddr = (void*)(findSymbol(preempter).getAddress().get());

        assert(newAddr != nullptr);

        memcpy(addr, newAddr, GetOveriddenSizeForSymbol(preempter));

        //llvm::errs() << "Preempting function " << functionName << " (size " << GetSizeForSymbol(functionName) << ") with function of size " << GetSizeForSymbol(preempter) << "\n";
    }
    else
    {
        llvm::errs() << "Cannot find function " << functionName << " to preempt\n";
    }
}

JITSymbol SurgeonJIT::resolveSymbol(const std::string Name) {
    std::string actualName = Name;

    if (auto Sym = findSymbol(actualName, false))
    {
        return Sym;
    }

    // __cxa_atexit and __dso_handle are handled in a special way.
    if (auto Sym = overrides.searchOverrides(actualName))
        return Sym;


    if (uint64_t addr = SectionMemoryManager::getSymbolAddressInProcess(actualName))
        return JITSymbol(addr, JITSymbolFlags::Exported);

    // Symbol not found.
    return JITSymbol(nullptr);
}

static void addComprehensiveStaticInstrumentationPass(const llvm::PassManagerBuilder & builder,
    llvm::legacy::PassManagerBase & PM) {
    CSIOptions options;
    options.jitMode = true;
    PM.add(createComprehensiveStaticInstrumentationLegacyPass(options));

    // CSI inserts complex instrumentation that mostly follows the logic of the
    // original code, but operates on "shadow" values.  It can benefit from
    // re-running some general purpose optimization passes.
    if (builder.OptLevel > 0)
    {
        PM.add(createEarlyCSEPass());
        PM.add(createReassociatePass());
        PM.add(createLICMPass());
        PM.add(createGVNPass());
        PM.add(createInstructionCombiningPass());
        PM.add(createDeadStoreEliminationPass());
        PM.add(createFunctionInliningPass());
        PM.add(createAlwaysInlinerLegacyPass());
    }
}

std::unique_ptr<Module> SurgeonJIT::optimizeModule(std::unique_ptr<Module> M) {
    bool enableCSI = modulesCSIEnabled[M.get()];
    llvm::PassManagerBuilder builder;
    builder.OptLevel = 3;

    legacy::PassManager modulePasses;

    // Create a function pass manager.
    auto FPM = llvm::make_unique<legacy::FunctionPassManager>(M.get());

    /* // Add some optimizations.
     FPM->add(createInstructionCombiningPass());
     FPM->add(createReassociatePass());
     FPM->add(createGVNPass());
     FPM->add(createCFGSimplificationPass()); */

    if (enableCSI)
    {
        // llvm::errs() << "Enabling CSI for module " << M->getName() << "\n";
        builder.addExtension(llvm::PassManagerBuilder::EP_TapirLate,
            addComprehensiveStaticInstrumentationPass);
    }

    builder.populateFunctionPassManager(*FPM);
    builder.populateModulePassManager(modulePasses);

    FPM->doInitialization();

    // Run the optimizations over all functions in the module being added to
    // the JIT.
    for (auto& F : *M)
        FPM->run(F);

    modulePasses.run(*M);

    FPM->doFinalization();

    if (getenv("SURGEON_PRINT_MODULE"))
        llvm::errs() << *M;

    return M;
}

std::string SurgeonJIT::mangle(StringRef Name) {
    std::string MangledName;
    raw_string_ostream MangledNameStream(MangledName);
    Mangler::getNameWithPrefix(MangledNameStream, Name, DL);

    return MangledName;
}

std::string SurgeonJIT::GenerateInstrumentationPrefix(const std::string & rootFunctionName)
{
    return "surgeon_instr_" + rootFunctionName + "_";
}

std::unique_ptr<llvm::Module> SurgeonJIT::LoadHelperModule(LLVMContext & context) {
    SMDiagnostic error;
    auto m = parseIRFile("surgeon_helpers.bc", error, context);
    if (m)
    {
        return std::move(m);
    }
    else
    {
        llvm::errs() << "Error loading helper module (surgeon_helpers.bc): "
            << error.getMessage() << "\n";
        exit(-1);
    }
}

void SurgeonJIT::LoadAndAddModule(const std::string & moduleName, bool enableCSI) {
    LLVMContext* context = new LLVMContext();
    SMDiagnostic error;
    auto m = parseIRFile(moduleName, error, *context);
    if (m)
    {
        auto key = addModule(std::move(m), enableCSI, false);
        if (auto Err = OptimizeLayer.emitAndFinalize(key))
        {
            llvm::errs() << "Error: " << Err << "\n";
        }
        if (enableCSI)
            CallCSIConstructorForModule(key, true);
    }
    else
    {
        llvm::errs() << "Error loading module (" << moduleName << "): "
            << error.getMessage() << "\n";
        exit(-1);
    }
}
