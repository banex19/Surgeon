#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <sstream>
#include <iterator>
#include <algorithm>

#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/Module.h>
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Basic/TargetInfo.h>

#include <JIT.h>

using namespace clang;
using namespace llvm;

std::vector<std::string> splitAndPrepend(std::string strToSplit, char delimeter, const std::string& prepend = "") {
    std::stringstream ss(strToSplit);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter))
    {
        splittedStrings.push_back(prepend + item);
    }
    return splittedStrings;
}

int main(int argc, char** argv) {
    std::vector<std::string> filenames;

    std::string defaultArgsWhole = "-mrelax-all -disable-free -disable-llvm-verifier -discard-value-names "
        "-mrelocation-model static -mthread-model posix -mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases -munwind-tables "
        "-fuse-init-array -target-cpu x86-64 -dwarf-column-info -debugger-tuning=gdb -v -resource-dir /home/daniele/llvm/build/lib/clang/7.0.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/c++/7.3.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/x86_64-linux-gnu/c++/7.3.0 -internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/x86_64-linux-gnu/c++/7.3.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/c++/7.3.0/backward -internal-isystem /usr/local/include -internal-isystem /home/daniele/llvm/build/lib/clang/7.0.0/include "
        "-internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include "
        "-fdeprecated-macro -fdebug-compilation-dir /home/daniele/surgeon/test -ferror-limit 19 -fmessage-length 80 -fobjc-runtime=gcc "
        "-fcxx-exceptions -fexceptions -fdiagnostics-show-option -fcolor-diagnostics -faddrsig";

    std::vector<std::string> defaultArgs = splitAndPrepend(defaultArgsWhole, ' ');

    if (argc < 3)
    {
        std::cout << "Specify a root directory and filenames\n";
        exit(-1);
    }

    std::string sourceRoot = std::string(argv[1]) + "/";
    filenames = splitAndPrepend(argv[2], ',', sourceRoot);

    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();
    InitializeAllTargets();

    SurgeonJIT JIT;

    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    // Prepare DiagnosticEngine 
    DiagnosticOptions DiagOpts;
    TextDiagnosticPrinter *textDiagPrinter =
        new clang::TextDiagnosticPrinter(errs(),
            &DiagOpts);
    IntrusiveRefCntPtr<clang::DiagnosticIDs> pDiagIDs;
    DiagnosticsEngine *pDiagnosticsEngine =
        new DiagnosticsEngine(pDiagIDs,
            &DiagOpts,
            textDiagPrinter);

    std::vector<std::unique_ptr<llvm::Module>> modules;


    std::vector<std::string> constructors;
    std::vector<VModuleKey> keys;



    // Initialize CompilerInvocation
    for (auto& name : filenames)
    {
        // Prepare compilation arguments
        std::vector<const char *> args;
        args.push_back(name.c_str());
        for (size_t i = 3; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        for (size_t i = 0; i < defaultArgs.size(); ++i)
            args.push_back(defaultArgs[i].c_str());

        std::shared_ptr<CompilerInvocation> CI = std::make_shared<CompilerInvocation>();
        CompilerInvocation::CreateFromArgs(*CI, &args[0], &args[0] + args.size(), *pDiagnosticsEngine);

        // Map code filename to a memoryBuffer
        auto file = MemoryBuffer::getFile(name);
        if (!file)
        {
            std::cout << "Error reading file " << name << "\n";
            exit(-1);
        }

        std::unique_ptr<MemoryBuffer> buffer = std::move(file.get());
        CI->getPreprocessorOpts().addRemappedFile(name, buffer.get());


        std::cout << "Loaded file " << name << "\n";

        // Create and initialize CompilerInstance
        CompilerInstance Clang;
        Clang.setInvocation(CI);
        Clang.createDiagnostics();


        const std::shared_ptr<clang::TargetOptions> targetOptions = std::make_shared<clang::TargetOptions>();
        targetOptions->Triple = std::string("bpf");
        TargetInfo *pTargetInfo = TargetInfo::CreateTargetInfo(*pDiagnosticsEngine, targetOptions);
        Clang.setTarget(pTargetInfo);

        // Create and execute action
        CodeGenAction *compilerAction = new EmitLLVMOnlyAction();
        //CodeGenAction *compilerAction = new EmitAssemblyAction();
        if (!Clang.ExecuteAction(*compilerAction))
        {
            std::cout << "Error compiling\n";
            exit(-1);
        }

        auto output = compilerAction->takeModule();
        //llvm::errs() << *output << "\n";

        //modules.push_back(std::move(output));
        for (auto constructor : getConstructors(*output))
        {
            constructors.push_back(constructor.Func->getName());
        }

        auto key = JIT.addModule(std::move(output));
        keys.push_back(key);

       /* auto Err = JIT.getCompileLayer().emitAndFinalize(key);
        if (Err)
        {
            llvm::errs() << "Error: " << Err << "\n";
            exit(-1);
        } */


        buffer.release();
    }

    for (auto& key : keys)
    {
        /*auto Err = JIT.getCompileLayer().emitAndFinalize(key);
        if (Err)
        {
            llvm::errs() << "Error: " << Err << "\n";
            exit(-1);
        }*/
    }

    std::cout << "Running " << constructors.size() << " constructors\n";
    for (auto& constructor : constructors)
    {
        JITSymbol entrySymbol = JIT.findSymbol(constructor, false);

        if (entrySymbol && entrySymbol.getAddress())
        {
            void* addr = (void*)entrySymbol.getAddress().get();
            assert(addr != nullptr);
            void(*fn)() = (void(*)())(addr);
            std::cout << "Calling constructor: " << constructor << "\n";
            fn();
        }
        else
        {
            std::cout << "Constructor not found\n";
        }
    }


    auto entrySymbol = JIT.findSymbol("main");

    if (entrySymbol)
    {
        std::cout << "Running program...\n";
        void* addr = (void*)entrySymbol.getAddress().get();
        assert(addr != nullptr);
        char** args = new char* [1];
        args[0] = "jit";
        int(*fn)(int, char**) = (int(*)(int, char**))(addr);
        //for (size_t i = 0; i < 5; ++i)
            fn(1, args);

        delete[]args;
    }
    else
    {
        llvm::errs() << "No entry point found\n";
    }

    return 0;

}