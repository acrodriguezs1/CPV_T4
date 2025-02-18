/*
 *  Copyright 2019-2024 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <random>

#include "Tutorial04_Instancing.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ColorConversion.h"
#include "../../Common/src/TexturedCube.hpp"
#include "imgui.h"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new Tutorial04_Instancing();
}

void Tutorial04_Instancing::CreatePipelineState()
{
    // clang-format off
    // Define vertex shader input layout
    // This tutorial uses two types of input: per-vertex data and per-instance data.
    LayoutElement LayoutElems[] =
    {
        // Per-vertex data - first buffer slot
        // Attribute 0 - vertex position
        LayoutElement{0, 0, 3, VT_FLOAT32, False},
        // Attribute 1 - texture coordinates
        LayoutElement{1, 0, 2, VT_FLOAT32, False},
            
        // Per-instance data - second buffer slot
        // We will use four attributes to encode instance-specific 4x4 transformation matrix
        // Attribute 2 - first row
        LayoutElement{2, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 3 - second row
        LayoutElement{3, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 4 - third row
        LayoutElement{4, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 5 - fourth row
        LayoutElement{5, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
    };
    // clang-format on

    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    TexturedCube::CreatePSOInfo CubePsoCI;
    CubePsoCI.pDevice                = m_pDevice;
    CubePsoCI.RTVFormat              = m_pSwapChain->GetDesc().ColorBufferFormat;
    CubePsoCI.DSVFormat              = m_pSwapChain->GetDesc().DepthBufferFormat;
    CubePsoCI.pShaderSourceFactory   = pShaderSourceFactory;
    CubePsoCI.VSFilePath             = "cube_inst.vsh";
    CubePsoCI.PSFilePath             = "cube_inst.psh";
    CubePsoCI.ExtraLayoutElements    = LayoutElems;
    CubePsoCI.NumExtraLayoutElements = _countof(LayoutElems);

    m_pPSO = TexturedCube::CreatePipelineState(CubePsoCI, m_ConvertPSOutputToGamma);

    // Create dynamic uniform buffer that will store our transformation matrix
    // Dynamic buffers can be frequently updated by the CPU
    CreateUniformBuffer(m_pDevice, sizeof(float4x4) * 2, "VS constants CB", &m_VSConstants);

    // Since we did not explicitly specify the type for 'Constants' variable, default
    // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables
    // never change and are bound directly to the pipeline state object.
    m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);

    // Since we are using mutable variable, we must create a shader resource binding object
    // http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/
    m_pPSO->CreateShaderResourceBinding(&m_SRB, true);
}

void Tutorial04_Instancing::CreateInstanceBuffer()
{
    // Create instance data buffer that will store transformation matrices
    BufferDesc InstBuffDesc;
    InstBuffDesc.Name = "Instance data buffer";
    // Use default usage as this buffer will only be updated when grid size changes
    InstBuffDesc.Usage     = USAGE_DEFAULT;
    InstBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
    InstBuffDesc.Size      = sizeof(float4x4) * MaxInstances;
    m_pDevice->CreateBuffer(InstBuffDesc, nullptr, &m_InstanceBuffer);
    PopulateInstanceBuffer();
}

void Tutorial04_Instancing::UpdateUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    
    ImGui::End();
}

void Tutorial04_Instancing::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    CreatePipelineState();

    // Load textured cube
    m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(m_pDevice, GEOMETRY_PRIMITIVE_VERTEX_FLAG_POS_TEX);
    m_CubeIndexBuffer  = TexturedCube::CreateIndexBuffer(m_pDevice);
    m_TextureSRV       = TexturedCube::LoadTexture(m_pDevice, "DGLogo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    // Set cube texture SRV in the SRB
    m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_TextureSRV);

    CreateInstanceBuffer();
}
static float camera = 0.1;


static float angle = PI_F ;
static float angle2 = PI_F;

void Tutorial04_Instancing::PopulateInstanceBuffer()
{
    // Tamaño de la base (5x5)
    int gridSize   = 5;
    int totalCubos = 20; // 1 arriba, 2 en el medio, y la base

    std::vector<float4x4> InstanceData(totalCubos);

    // Separación entre cubos
    float separacionX = 0.5f;
    float separacionY = 0.5f;
    float separacionZ = 0.5f;
    float scaleHiloVertical = 0.5f;

    float baseAltura = 0.0f; // Altura base de los cubos
    float scale      = 0.2f; // Escala de los cubos

    int index = 0;
    angle += 0.001;
    angle2 += 0.01;


    // Cubo superior
    InstanceData[0] = float4x4::Scale(0.2f, 0.2f, 0.2f) * float4x4::Translation(0.0f, 2 * 0.5f, 0.0f) * float4x4:: RotationY(angle);
    // palos primer piso de palos :) 
    InstanceData[4] = float4x4::Scale(0.7f, 0.01f, 0.01f) * float4x4::Translation(0.0f,  0.5f, 0.0f) * float4x4::RotationY(angle);
    InstanceData[5] = float4x4::Scale(0.01f, 0.2f, 0.01f) * float4x4::Translation(0.0f, 0.7f, 0.0f) * float4x4::RotationY(angle);
    InstanceData[6] = float4x4::Scale(0.01f, 0.1f, 0.01f) * float4x4::Translation(0.7f, 0.4f, 0.0) * float4x4::RotationY(angle);
    InstanceData[7] = float4x4::Scale(0.01f, 0.13f, 0.01f) * float4x4::Translation(-0.7f, 0.4f, 0.0) * float4x4::RotationY(angle);
    // segundo nivel con dos cubos 
    InstanceData[3] = float4x4::Scale(0.2f, 0.2f, 0.2f) * float4x4::Translation(0.7f, 0.01f, 0.0f) * float4x4::RotationY(angle);
    InstanceData[8] = float4x4::Scale(0.2f, 0.2f, 0.2f) * float4x4::Translation(-0.7f, 0.01f, 0.0f) * float4x4::RotationY(angle);
    //Tercer  nivel con dos cubos
    InstanceData[1] = float4x4::Scale(0.2f, 0.2f, 0.2f) * float4x4::Translation(-1.2f, -1.0f, 0.0f) * float4x4::RotationY(angle);
    InstanceData[2] = float4x4::Scale(0.2f, 0.2f, 0.2f) * float4x4::Translation(0.0f, -1.0f, 0.0f) * float4x4::RotationY(angle);
    
    //segundo nivel de palos 
     InstanceData[9] = float4x4::Scale(0.01f, 0.25f, 0.01f) * float4x4::Translation(-1.2f, -0.6f, 0.0) * float4x4::RotationY(angle);
     InstanceData[10] = float4x4::Scale(0.01f, 0.25f, 0.01f) * float4x4::Translation(0.0f, -0.6f, 0.0) * float4x4::RotationY(angle);
     InstanceData[11]  = float4x4::Scale(0.6f, 0.01f, 0.01f) * float4x4::Translation(-0.6f, -0.4f, 0.0f) * float4x4::RotationY(angle);
     InstanceData[12]  = float4x4::Scale(0.01f, 0.15f, 0.01f) * float4x4::Translation(-0.6f, -0.3f, 0.0) * float4x4::RotationY(angle);
    // **Actualizar el buffer**
    Uint32 DataSize = static_cast<Uint32>(sizeof(InstanceData[0]) * InstanceData.size());
    m_pImmediateContext->UpdateBuffer(m_InstanceBuffer, 0, DataSize, InstanceData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

}


// Render a frame
void Tutorial04_Instancing::Render()
{
    auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
    // Clear the back buffer
    float4 ClearColor = {0.350f, 0.350f, 0.350f, 1.0f};

     PopulateInstanceBuffer();

    if (m_ConvertPSOutputToGamma)
    {
        // If manual gamma correction is required, we need to clear the render target with sRGB color
        ClearColor = LinearToSRGB(ClearColor);
    }
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        // Map the buffer and write current world-view-projection matrix
        MapHelper<float4x4> CBConstants(m_pImmediateContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        CBConstants[0] = m_ViewProjMatrix;
        CBConstants[1] = m_RotationMatrix;
    }

    // Bind vertex, instance and index buffers
    const Uint64 offsets[] = {0, 0};
    IBuffer*     pBuffs[]  = {m_CubeVertexBuffer, m_InstanceBuffer};
    m_pImmediateContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set the pipeline state
    m_pImmediateContext->SetPipelineState(m_pPSO);
    // Commit shader resources. RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode
    // makes sure that resources are transitioned to required states.
    m_pImmediateContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs DrawAttrs;       // This is an indexed draw call
    DrawAttrs.IndexType    = VT_UINT32; // Index type
    DrawAttrs.NumIndices   = 36;
    DrawAttrs.NumInstances = m_GridSize * m_GridSize * m_GridSize; // The number of instances
    // Verify the state of vertex and index buffers
    DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
    m_pImmediateContext->DrawIndexed(DrawAttrs);
}

void Tutorial04_Instancing::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);

  
    static int viewOption = 0;
    ImGui::Begin("Vista de Cámara");
    ImGui::Combo("Vista", &viewOption, "arriba\0frente\0abajo\0");
    ImGui::End();

    float4x4 View;
    switch (viewOption)
    {
        case 0: // arriba
            View = float4x4::Translation(0.f, 6.0f, 0.0f) * float4x4::RotationX(PI_F / 2);
            break;
        case 1: // frente
            View = float4x4::Translation(0.f, 1.0f, -7.0f) * float4x4::RotationY(PI_F);
            break;
        case 2: // abajo
            View = float4x4::Translation(0.f, -6.0f, 0.0f) * float4x4::RotationX(-PI_F / 2);
            break;
      
        default:
            View = float4x4::Translation(0.f, 1.0f, -7.0f) * float4x4::RotationY(PI_F);
            break;
    }


    // La pretransformación de la superficie se configura de forma estándar.
    auto SrfPreTransform = GetSurfacePretransformMatrix(float3{0, 0, 1});

    // Se configura la proyección (campo de visión, plano cercano y lejano)
    auto Proj = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 100.f);

    // La matriz final de vista-proyección se obtiene multiplicando las tres matrices.
    m_ViewProjMatrix = View * SrfPreTransform * Proj;
    m_RotationMatrix = float4x4::Identity();
}

} // namespace Diligent
