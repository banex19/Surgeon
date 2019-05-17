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
#include <cctype>


#include <JIT.h>
#include "Interactive.h"

using namespace clang;
using namespace llvm;

std::vector<std::string> splitAndPrepend(std::string strToSplit, char delimeter, const std::string& prepend = "") {
    std::stringstream ss(strToSplit);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter))
    {
        if (item.size() > 0)
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

    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently("/home/daniele/llvm/build/lib/clang/7.0.0/lib/linux/"
        "libclang_rt.csi-x86_64.so")) {
        std::cout << "Error loading CSI runtime\n";
        exit(-1);
    }

    OptionsStore::LoadOptions("surgeon.cfg");

    SurgeonJIT JIT;

    // Preload tools from the configuration file.
    std::fstream toolFile{ "tools.cfg" };
    if (toolFile) {
        std::string line;
        while (std::getline(toolFile, line)) {
            ltrim(line);
            if (line.size() == 0 || line[0] == '#')
                continue;
            auto tokens = split(line, ' ', true);
            if (tokens.size() != 3)
                continue;
            CSITool tool{ tokens[0], tokens[1], tokens[2] };
            JIT.LoadCSITool(tool);
        }

        toolFile.close();
    }

    // Prepare DiagnosticEngine 
    DiagnosticOptions DiagOpts;
    TextDiagnosticPrinter* textDiagPrinter =
        new clang::TextDiagnosticPrinter(errs(),
            &DiagOpts);
    IntrusiveRefCntPtr<clang::DiagnosticIDs> pDiagIDs;
    DiagnosticsEngine* pDiagnosticsEngine =
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
        std::vector<const char*> args;
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
        TargetInfo* pTargetInfo = TargetInfo::CreateTargetInfo(*pDiagnosticsEngine, targetOptions);
        Clang.setTarget(pTargetInfo);

        // Create and execute action
        CodeGenAction* compilerAction = new EmitLLVMOnlyAction();
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
        size_t start = 1;
        size_t end = (getenv("TWICE") ? (start + 2) : (start + 1));
        for (size_t i = start; i < end; ++i)
        {
            int numArgs = 0;
            char** args = nullptr;

            std::set<std::string> instrumented;

            if (i == 1)
            {
                while (true)
                {
                    auto tokens = ShowPromptAndGetInput("(surgeon)");

                    if (tokens.size() == 0)
                    {
                        std::cout << "Command not recognized\n";
                    }
                    else if (tokens[0] == "break" || (tokens[0].size() == 1 && tokens[0][0] == 'b'))
                    {
                        const std::string& function = tokens.size() > 1 ? tokens[1] : "";


                        if (tokens.size() < 3)
                        {
                            std::cout << "Command 'break' requires at least two arguments (function to instrument and tool name)\n";
                        }
                        else if (JIT.IsFunctionInAnySubtree(function, instrumented)) {
                            std::cout << "Function " << function << " is already in an instrumented tree\n";
                        }
                        else if (!JIT.findSymbol(function, false))
                        {
                            std::cout << "Function '" << function << "' doesn't exist\n";
                        }
                        else
                        {
                            bool toolsExist = true;
                            std::vector<std::string> tools{ tokens.begin() + 2, tokens.end() };
                            for (auto& tool : tools) {
                                if (!JIT.IsCSIToolRegistered(tool))
                                {
                                    std::cout << "Tool " << tool << " is not registered\n";
                                    toolsExist = false;
                                    break;
                                }
                            }
                            if (toolsExist) {
                                void* newAddr = JIT.RecompileFunction(function, true, tools);
                                instrumented.insert(function);
                                //  std::cout << "Old addr: " << addr << ", new addr: " << newAddr << "\n";
                            }
                        }
                    }
                    else if (tokens[0] == "run" || (tokens[0].size() == 1 && tokens[0][0] == 'r'))
                    {
                        numArgs = tokens.size();
                        args = new char* [numArgs];

                        args[0] = new char[4];
                        memcpy(args[0], "jit", 3);
                        args[0][3] = '\0';

                        // Copy any additional argument passed by the user.
                        for (size_t i = 1; i < tokens.size(); ++i) {
                            args[i] = new char[tokens[i].size() + 1];
                            memcpy(args[i], tokens[i].data(), tokens[i].size());
                            args[i][tokens[i].size()] = '\0';
                        }

                        break;
                    }
                    else if (tokens[0] == "load") {
                        if (tokens.size() != 4)
                        {
                            std::cout << "Command 'load' requires three arguments (tool's library path, bitcode path, and tool name)\n";
                        }
                        else {
                            bool res = JIT.LoadCSITool(CSITool(tokens[3], tokens[1], tokens[2]));
                            if (res) std::cout << "Loaded CSI tool " << tokens[1] << " with name " << tokens[3] << "\n";
                        }

                    }
                    else if (tokens[0] == "quit" || tokens[0] == "exit" || (tokens[0].size() == 1 && tokens[0][0] == 'q'))
                    {
                        exit(0);
                    }
                    else
                    {
                        std::cout << "Command not recognized\n";
                    }
                }

                //  addr = newAddr;
            }


            int(*fn)(int, char**) = (int(*)(int, char**))(addr);

            int res = fn(numArgs, args);
            std::cout << "Main returned " << res << "\n";

            for (int i = 0; i < numArgs; ++i)
                delete[] args[i];
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