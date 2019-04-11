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
#include <llvm/Support/CommandLine.h>

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Basic/TargetInfo.h>

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <JIT.h>

using namespace clang;
using namespace llvm;

static inline void lowerString(std::string& data) {
    std::transform(data.begin(), data.end(), data.begin(), ::tolower);
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [] (int ch)
    {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [] (int ch)
    {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

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

void ParseLLVMOptions() {
    const char* llvmArgsPtr = getenv("SURGEON_LLVM_ARGS");
    if (llvmArgsPtr == nullptr || getenv("SURGEON_ALREADY_PARSED_LLVM"))
        return;

    std::vector<std::string> llvmArgs = splitAndPrepend((std::string)llvmArgsPtr, ' ');

    if (llvmArgs.size() == 0)
        return;

    std::vector<const char*> args;
    args.reserve(llvmArgs.size() + 1);

    args.push_back(" "); // Fake first argument.

    for (auto& arg : llvmArgs)
    {
        args.push_back(arg.c_str());
    }

    cl::ParseCommandLineOptions(args.size(), args.data());
#ifndef WIN32
    setenv("SURGEON_ALREADY_PARSED_LLVM", "1", true);
#endif
}

void sig_handler(int signo) {
    if (signo == SIGINT)
    {
        exit(0);
    }
}

int main(int argc, char** argv) {

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
        std::cerr << "WARNING: Can't catch SIGINT\n";
    }

    std::vector<std::string> filenames;

    std::string defaultArgsWhole = "-mrelax-all -disable-free -disable-llvm-verifier -discard-value-names "
        "-mrelocation-model static -mthread-model posix -mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases -munwind-tables "
        "-fuse-init-array -target-cpu x86-64 -dwarf-column-info -debugger-tuning=gdb -resource-dir /home/daniele/llvm/build/lib/clang/7.0.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/c++/7.3.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/x86_64-linux-gnu/c++/7.3.0 -internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/x86_64-linux-gnu/c++/7.3.0 "
        "-internal-isystem /usr/bin/../lib/gcc/x86_64-linux-gnu/7.3.0/../../../../include/c++/7.3.0/backward -internal-isystem /usr/local/include -internal-isystem /home/daniele/llvm/build/lib/clang/7.0.0/include "
        "-internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include "
        "-fdeprecated-macro -ferror-limit 19 -fmessage-length 80 -fobjc-runtime=gcc "
        "-fcxx-exceptions -fexceptions  -fcolor-diagnostics -faddrsig";

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

    ParseLLVMOptions();

    llvm::sys::DynamicLibrary::LoadLibraryPermanently("/home/daniele/llvm/build/lib/clang/7.0.0/lib/linux/"
        "libclang_rt.csi-x86_64.so");


    SurgeonJIT JIT;

  

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

    // Call CSI constructors.
    for (auto& key : keys)
    {
        JIT.CallCSIConstructorForModule(key);
    }

    for (auto& constructor : constructors)
    {
        JITSymbol entrySymbol = JIT.findSymbol(constructor, false);

        if (entrySymbol && entrySymbol.getAddress())
        {
            void* addr = (void*)entrySymbol.getAddress().get();
            assert(addr != nullptr);
            void(*fn)() = (void(*)())(addr);
            fn();
        }
        else
        {
            std::cout << "Constructor " << constructor << " not found\n";
        }
    }



    auto entrySymbol = JIT.findSymbol("main");

    if (entrySymbol)
    {
        void* addr = (void*)entrySymbol.getAddress().get();
        assert(addr != nullptr);
        size_t start = (getenv("SKIP_NORMAL") ? 1 : 0);
        size_t end = (getenv("ONLY_ONCE") ? (start + 1) : 3);
        for (size_t i = start; i < end; ++i)
        {
            if (i == 1)
            {
                std::cout << "(surgeon) Which function do you want to analyze? ";
                std::string command;
                std::getline(std::cin, command);
                trim(command);
                while (!JIT.findSymbol(command, false))
                {
                    std::cout << "The function '" << command << "' doesn't seem to exist. Try again.\n";
                    std::cout << "(surgeon) Which function do you want to analyze? ";
                    std::getline(std::cin, command);
                    trim(command);
                }
                void* newAddr = JIT.RecompileFunction(command, true);
                std::cout << "Old addr: " << addr << ", new addr: " << newAddr << "\n";

              //  addr = newAddr;
            }

            char** args = new char*[3];
            args[0] = (char*)"jit";
            args[1] = (char*)"../test";
            args[2] = (char*)"test.cpp,test2.cpp,add.cpp";
            int(*fn)(int, char**) = (int(*)(int, char**))(addr);

            int res = fn(1, args);
            std::cout << "Main returned " << res << "\n";

            delete[]args;
        }
    }
    else
    {
        llvm::errs() << "No entry point found\n";
    }

    std::cout << "Exiting Surgeon\n";
    exit(0);

    return 0;

}