#include <JIT.h>

VModuleKey SurgeonJIT::addModule(std::unique_ptr<Module> M) {
    // Add the module to the JIT with a new VModuleKey.
    auto K = ES.allocateVModule();
    cantFail(OptimizeLayer.addModule(K, std::move(M)));
    return K;
}

JITSymbol SurgeonJIT::findSymbol(const std::string Name, bool exportedOnly) {
    std::string MangledName = mangle(Name);
    return OptimizeLayer.findSymbol(MangledName, exportedOnly);
}

JITSymbol SurgeonJIT::resolveSymbol(const std::string Name) {
    if (auto Sym = findSymbol(Name))
    {
        return Sym;
    }

    // __cxa_atexit and __dso_handle are handled in a special way.
    if (auto Sym = overrides.searchOverrides(Name))
        return Sym;

    if (uint64_t addr = SectionMemoryManager::getSymbolAddressInProcess(Name))
        return JITSymbol(addr, JITSymbolFlags::Exported);

    // Symbol not found.
    return JITSymbol(nullptr);
}

std::unique_ptr<Module> SurgeonJIT::optimizeModule(std::unique_ptr<Module> M) {
    // Create a function pass manager.
    auto FPM = llvm::make_unique<legacy::FunctionPassManager>(M.get());

    // Add some optimizations.
    FPM->add(createInstructionCombiningPass());
    FPM->add(createReassociatePass());
    FPM->add(createGVNPass());
    FPM->add(createCFGSimplificationPass());
    FPM->doInitialization();

    // Run the optimizations over all functions in the module being added to
    // the JIT.
    for (auto &F : *M)
        FPM->run(F);

    return M;
}

std::string SurgeonJIT::mangle(StringRef Name) {
    std::string MangledName;
    raw_string_ostream MangledNameStream(MangledName);
    Mangler::getNameWithPrefix(MangledNameStream, Name, DL);

    return MangledName;
}
