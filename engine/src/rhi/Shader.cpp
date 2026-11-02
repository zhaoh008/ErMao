// ErMao Engine - 着色器实现
#include "rhi/Shader.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <d3dcompiler.h>
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace erm {

bool Shader::Compile(ID3D11Device* device, const std::string& file, const char* vsEntry, const char* psEntry,
                     const char* csEntry) {
    const std::string path = FileSystem::ShaderPath(file);
    if (path.empty()) {
        LogError("找不到着色器文件: %s", file.c_str());
        return false;
    }
    std::string source;
    if (!FileSystem::ReadTextFile(path, source)) {
        LogError("无法读取着色器: %s", path.c_str());
        return false;
    }

    if (vsEntry) {
        IUnknown* shader = nullptr;
        if (!CompileStage(device, source, path, vsEntry, "vs_5_0", &m_vsBlob, &shader)) return false;
        m_vs.Attach(static_cast<ID3D11VertexShader*>(shader));
    }
    if (psEntry) {
        IUnknown* shader = nullptr;
        if (!CompileStage(device, source, path, psEntry, "ps_5_0", nullptr, &shader)) return false;
        m_ps.Attach(static_cast<ID3D11PixelShader*>(shader));
    }
    if (csEntry) {
        IUnknown* shader = nullptr;
        if (!CompileStage(device, source, path, csEntry, "cs_5_0", nullptr, &shader)) return false;
        m_cs.Attach(static_cast<ID3D11ComputeShader*>(shader));
    }
    LogInfo("着色器编译完成: %s", file.c_str());
    return true;
}

bool Shader::CompileStage(ID3D11Device* device, const std::string& source, const std::string& name,
                          const char* entry, const char* target, ComPtr<ID3DBlob>* outBlob, IUnknown** outShader) {
    // 始终带 DEBUG 信息：这样 X4000 级别的警告在 Release 也能看到（审核 P2-9）
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG;
#ifdef _DEBUG
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
#ifdef NDEBUG
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, nullptr, entry, target,
                                  flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        LogError("着色器编译失败 [%s::%s]\n%s", name.c_str(), entry, msg);
        MessageBoxA(nullptr, msg, ("Shader compile error: " + std::string(entry)).c_str(), MB_OK | MB_ICONERROR);
        return false;
    }
    if (errors) {
        const char* warn = static_cast<const char*>(errors->GetBufferPointer());
        if (warn && warn[0]) LogWarn("着色器警告 [%s::%s]\n%s", name.c_str(), entry, warn);
    }

    HRESULT createHr = E_FAIL;
    if (std::strcmp(target, "vs_5_0") == 0) {
        ID3D11VertexShader* shader = nullptr;
        createHr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        *outShader = shader;
    } else if (std::strcmp(target, "ps_5_0") == 0) {
        ID3D11PixelShader* shader = nullptr;
        createHr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        *outShader = shader;
    } else {
        ID3D11ComputeShader* shader = nullptr;
        createHr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        *outShader = shader;
    }
    if (FAILED(createHr)) {
        LogError("创建 %s 对象失败 (0x%08X)", target, (unsigned)createHr);
        return false;
    }
    if (outBlob) *outBlob = blob;
    return true;
}

bool Shader::CreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count) {
    if (!m_vsBlob) {
        LogError("CreateInputLayout: 顶点着色器尚未编译");
        return false;
    }
    const HRESULT hr = device->CreateInputLayout(elements, count, m_vsBlob->GetBufferPointer(),
                                                 m_vsBlob->GetBufferSize(), &m_layout);
    if (FAILED(hr)) {
        LogError("CreateInputLayout 失败 (0x%08X)", (unsigned)hr);
        return false;
    }
    return true;
}

void Shader::Bind(ID3D11DeviceContext* ctx) const {
    if (m_layout) ctx->IASetInputLayout(m_layout.Get());
    if (m_vs) ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    if (m_ps) ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    if (m_cs) ctx->CSSetShader(m_cs.Get(), nullptr, 0);
}

} // namespace erm
