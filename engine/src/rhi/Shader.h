// ErMao Engine - 着色器运行时编译（改 HLSL 不用重编 exe）
#pragma once

#include "core/Types.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace erm {

using Microsoft::WRL::ComPtr;

// 一个 Shader 对象 = 同一 .hlsl 文件中的一个 VS/PS/CS 组合。
// 编译失败会打印 D3DCompile 的完整错误列表（带行号），并弹出错误框。
class Shader {
public:
    bool Compile(ID3D11Device* device, const std::string& file, const char* vsEntry, const char* psEntry,
                 const char* csEntry = nullptr);
    bool CreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count);

    void Bind(ID3D11DeviceContext* ctx) const;

    ID3D11VertexShader*  VS() const { return m_vs.Get(); }
    ID3D11PixelShader*   PS() const { return m_ps.Get(); }
    ID3D11ComputeShader* CS() const { return m_cs.Get(); }
    ID3D11InputLayout*   Layout() const { return m_layout.Get(); }

private:
    // 编译单个入口；成功时 outBlob 为该 stage 的字节码，outShader 为裸指针（由调用方 Attach）
    bool CompileStage(ID3D11Device* device, const std::string& source, const std::string& name, const char* entry,
                      const char* target, ComPtr<ID3DBlob>* outBlob, IUnknown** outShader);

    ComPtr<ID3D11VertexShader>  m_vs;
    ComPtr<ID3D11PixelShader>   m_ps;
    ComPtr<ID3D11ComputeShader> m_cs;
    ComPtr<ID3D11InputLayout>   m_layout;
    ComPtr<ID3DBlob>            m_vsBlob;
};

} // namespace erm
