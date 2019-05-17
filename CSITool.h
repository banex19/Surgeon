#pragma once
#include <string>
#include "llvm/Support/DynamicLibrary.h"

class CSITool {
public:
    CSITool() = default;
    CSITool(const std::string& toolName, const std::string& libraryFilename, const std::string& bitcodeFilename) :
        name(toolName), libFilename(libraryFilename), bitcodeFilename(bitcodeFilename)
    {}

    const std::string& GetToolName() const { return name; }
    const std::string& GetLibraryFilename() const { return libFilename; }
    const std::string& GetBitcodeFilename() const { return bitcodeFilename; }

protected:
    std::string name, libFilename, bitcodeFilename;
};

class LoadedCSITool : public CSITool {
public:
    LoadedCSITool() = default;
    LoadedCSITool(const LoadedCSITool& other) = default;
    
    LoadedCSITool(const CSITool& tool, llvm::sys::DynamicLibrary handle) : CSITool(tool), handle(handle)
    {
    }

    llvm::sys::DynamicLibrary& GetLibrary() { return handle; }
private:
    llvm::sys::DynamicLibrary handle;
};