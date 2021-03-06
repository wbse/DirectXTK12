//--------------------------------------------------------------------------------------
// File: ModelLoadSDKMESH.cpp
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=615561
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Model.h"

#include "Effects.h"
#include "VertexTypes.h"

#include "DirectXHelpers.h"
#include "PlatformHelpers.h"
#include "BinaryReader.h"
#include "DescriptorHeap.h"
#include "CommonStates.h"

#include "SDKMesh.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    int GetUniqueTextureIndex(const wchar_t* textureName, std::map<std::wstring, int>& textureDictionary)
    {
        if (textureName == nullptr || !textureName[0])
            return -1;

        auto i = textureDictionary.find(textureName);
        if (i == std::cend(textureDictionary))
        {
            int index = static_cast<int>(textureDictionary.size());
            textureDictionary[textureName] = index;
            return index;
        }
        else
        {
            return i->second;
        }
    }

    void InitMaterial(
        const DXUT::SDKMESH_MATERIAL& mh,
        bool perVertexColor,
        bool enableSkinning,
        bool enableDualTexture,
        bool enableNormalMaps,
        _Out_ Model::ModelMaterialInfo& m,
        _Inout_ std::map<std::wstring, int32_t>& textureDictionary)
    {
        wchar_t matName[DXUT::MAX_MATERIAL_NAME];
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, mh.Name, -1, matName, DXUT::MAX_MATERIAL_NAME);

        wchar_t diffuseName[DXUT::MAX_TEXTURE_NAME];
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, mh.DiffuseTexture, -1, diffuseName, DXUT::MAX_TEXTURE_NAME);

        wchar_t specularName[DXUT::MAX_TEXTURE_NAME];
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, mh.SpecularTexture, -1, specularName, DXUT::MAX_TEXTURE_NAME);

        wchar_t normalName[DXUT::MAX_TEXTURE_NAME];
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, mh.NormalTexture, -1, normalName, DXUT::MAX_TEXTURE_NAME);

        if (enableDualTexture && !mh.SpecularTexture[0])
        {
            DebugTrace("WARNING: Material '%s' has multiple texture coords but not multiple textures\n", mh.Name);
            enableDualTexture = false;
        }

        if (enableNormalMaps)
        {
            if (!mh.NormalTexture[0])
            {
                enableNormalMaps = false;
                *normalName = 0;
            }
        }
        else if (mh.NormalTexture[0])
        {
            DebugTrace("WARNING: Material '%s' has a normal map, but vertex buffer is missing tangents\n", mh.Name);
            *normalName = 0;
        }

        m = {};
        m.name = matName;
        m.perVertexColor = perVertexColor;
        m.enableSkinning = enableSkinning;
        m.enableDualTexture = enableDualTexture;
        m.enableNormalMaps = enableNormalMaps;
        m.ambientColor = XMFLOAT3(mh.Ambient.x, mh.Ambient.y, mh.Ambient.z);
        m.diffuseColor = XMFLOAT3(mh.Diffuse.x, mh.Diffuse.y, mh.Diffuse.z);
        m.emissiveColor = XMFLOAT3(mh.Emissive.x, mh.Emissive.y, mh.Emissive.z);

        if (mh.Diffuse.w != 1.f && mh.Diffuse.w != 0.f)
        {
            m.alphaValue = mh.Diffuse.w;
        }
        else
            m.alphaValue = 1.f;

        if (mh.Power)
        {
            m.specularPower = mh.Power;
            m.specularColor = XMFLOAT3(mh.Specular.x, mh.Specular.y, mh.Specular.z);
        }

        m.diffuseTextureIndex = GetUniqueTextureIndex(diffuseName, textureDictionary);
        m.specularTextureIndex = GetUniqueTextureIndex(specularName, textureDictionary);
        m.normalTextureIndex = GetUniqueTextureIndex(normalName, textureDictionary);

        m.samplerIndex = (m.diffuseTextureIndex == -1) ? -1 : static_cast<int>(CommonStates::SamplerIndex::AnisotropicWrap);
        m.samplerIndex2 = (enableDualTexture) ? static_cast<int>(CommonStates::SamplerIndex::AnisotropicWrap) : -1;
    }


    //--------------------------------------------------------------------------------------
    // Direct3D 9 Vertex Declaration to Direct3D 12 Input Layout mapping

    void GetInputLayoutDesc(_In_reads_(32) const DXUT::D3DVERTEXELEMENT9 decl[], std::vector<D3D12_INPUT_ELEMENT_DESC>& inputDesc,
        bool &perVertexColor, bool& enableSkinning, bool& dualTexture, bool& normalMaps)
    {
        static const D3D12_INPUT_ELEMENT_DESC elements[] =
        {
            { "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",       0, DXGI_FORMAT_B8G8R8A8_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BINORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES",0, DXGI_FORMAT_R8G8B8A8_UINT,      0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        using namespace DXUT;

        uint32_t offset = 0;
        uint32_t texcoords = 0;

        bool posfound = false;

        for (uint32_t index = 0; index < DXUT::MAX_VERTEX_ELEMENTS; ++index)
        {
            if (decl[index].Usage == 0xFF)
                break;

            if (decl[index].Type == D3DDECLTYPE_UNUSED)
                break;

            if (decl[index].Offset != offset)
                break;

            if (decl[index].Usage == D3DDECLUSAGE_POSITION && decl[index].Type == D3DDECLTYPE_FLOAT3)
            {
                inputDesc.push_back(elements[0]);
                offset += 12;
                posfound = true;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_NORMAL)
            {
                if (decl[index].Type == D3DDECLTYPE_FLOAT3)
                {
                    inputDesc.push_back(elements[1]);
                    offset += 12;
                }
                else if (decl[index].Type == D3DDECLTYPE_FLOAT16_4)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[1];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_SHORT4N)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[1];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_UBYTE4N)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[1];
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    inputDesc.push_back(desc);
                    offset += 4;
                }
                else
                    break;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_COLOR && decl[index].Type == D3DDECLTYPE_D3DCOLOR)
            {
                inputDesc.push_back(elements[2]);
                offset += 4;
                perVertexColor = true;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_TANGENT)
            {
                if (decl[index].Type == D3DDECLTYPE_FLOAT3)
                {
                    normalMaps = true;
                    inputDesc.push_back(elements[3]);
                    offset += 12;
                }
                else if (decl[index].Type == D3DDECLTYPE_FLOAT16_4)
                {
                    normalMaps = true;
                    D3D12_INPUT_ELEMENT_DESC desc = elements[3];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_SHORT4N)
                {
                    normalMaps = true;
                    D3D12_INPUT_ELEMENT_DESC desc = elements[3];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_UBYTE4N)
                {
                    normalMaps = true;
                    D3D12_INPUT_ELEMENT_DESC desc = elements[3];
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    inputDesc.push_back(desc);
                    offset += 4;
                }
                else
                    break;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_BINORMAL)
            {
                if (decl[index].Type == D3DDECLTYPE_FLOAT3)
                {
                    inputDesc.push_back(elements[4]);
                    offset += 12;
                }
                else if (decl[index].Type == D3DDECLTYPE_FLOAT16_4)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[4];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_SHORT4N)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[4];
                    desc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
                    inputDesc.push_back(desc);
                    offset += 8;
                }
                else if (decl[index].Type == D3DDECLTYPE_UBYTE4N)
                {
                    D3D12_INPUT_ELEMENT_DESC desc = elements[4];
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    inputDesc.push_back(desc);
                    offset += 4;
                }
                else
                    break;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_TEXCOORD)
            {
                D3D12_INPUT_ELEMENT_DESC desc = elements[5];
                desc.SemanticIndex = decl[index].UsageIndex;

                bool unk = false;
                switch (decl[index].Type)
                {
                case D3DDECLTYPE_FLOAT2:    offset += 8; break;
                case D3DDECLTYPE_FLOAT1:    desc.Format = DXGI_FORMAT_R32_FLOAT; offset += 4; break;
                case D3DDECLTYPE_FLOAT3:    desc.Format = DXGI_FORMAT_R32G32B32_FLOAT; offset += 12; break;
                case D3DDECLTYPE_FLOAT4:    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; offset += 16; break;
                case D3DDECLTYPE_FLOAT16_2: desc.Format = DXGI_FORMAT_R16G16_FLOAT; offset += 4; break;
                case D3DDECLTYPE_FLOAT16_4: desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; offset += 8; break;

                default:
                    unk = true;
                    break;
                }

                if (unk)
                    break;

                ++texcoords;

                inputDesc.push_back(desc);
            }
            else if (decl[index].Usage == D3DDECLUSAGE_BLENDINDICES && decl[index].Type == D3DDECLTYPE_UBYTE4)
            {
                enableSkinning = true;
                inputDesc.push_back(elements[6]);
                offset += 4;
            }
            else if (decl[index].Usage == D3DDECLUSAGE_BLENDWEIGHT && decl[index].Type == D3DDECLTYPE_UBYTE4N)
            {
                enableSkinning = true;
                inputDesc.push_back(elements[7]);
                offset += 4;
            }
            else
                break;
        }

        if (!posfound)
            throw std::exception("SV_Position is required");

        if (texcoords == 2)
        {
            dualTexture = true;
        }
    }
}

//======================================================================================
// Model Loader
//======================================================================================

_Use_decl_annotations_
std::unique_ptr<Model> DirectX::Model::CreateFromSDKMESH( const uint8_t* meshData, size_t dataSize )
{
    if ( !meshData )
        throw std::exception("meshData cannot be null");

    // File Headers
    if ( dataSize < sizeof(DXUT::SDKMESH_HEADER) )
        throw std::exception("End of file");
    auto header = reinterpret_cast<const DXUT::SDKMESH_HEADER*>( meshData );

    size_t headerSize = sizeof( DXUT::SDKMESH_HEADER )
                        + header->NumVertexBuffers * sizeof(DXUT::SDKMESH_VERTEX_BUFFER_HEADER)
                        + header->NumIndexBuffers * sizeof(DXUT::SDKMESH_INDEX_BUFFER_HEADER);
    if ( header->HeaderSize != headerSize )
        throw std::exception("Not a valid SDKMESH file");

    if ( dataSize < header->HeaderSize )
        throw std::exception("End of file");

    if( header->Version != DXUT::SDKMESH_FILE_VERSION )
        throw std::exception("Not a supported SDKMESH version");
                          
    if ( header->IsBigEndian )
        throw std::exception("Loading BigEndian SDKMESH files not supported");

    if ( !header->NumMeshes )
        throw std::exception("No meshes found");

    if ( !header->NumVertexBuffers )
        throw std::exception("No vertex buffers found");

    if ( !header->NumIndexBuffers )
        throw std::exception("No index buffers found");

    if ( !header->NumTotalSubsets )
        throw std::exception("No subsets found");

    if ( !header->NumMaterials )
        throw std::exception("No materials found");

    // Sub-headers
    if ( dataSize < header->VertexStreamHeadersOffset
         || ( dataSize < (header->VertexStreamHeadersOffset + header->NumVertexBuffers * sizeof(DXUT::SDKMESH_VERTEX_BUFFER_HEADER) ) ) )
        throw std::exception("End of file");
    auto vbArray = reinterpret_cast<const DXUT::SDKMESH_VERTEX_BUFFER_HEADER*>( meshData + header->VertexStreamHeadersOffset );
    
    if ( dataSize < header->IndexStreamHeadersOffset
         || ( dataSize < (header->IndexStreamHeadersOffset + header->NumIndexBuffers * sizeof(DXUT::SDKMESH_INDEX_BUFFER_HEADER) ) ) )
        throw std::exception("End of file");
    auto ibArray = reinterpret_cast<const DXUT::SDKMESH_INDEX_BUFFER_HEADER*>( meshData + header->IndexStreamHeadersOffset );

    if ( dataSize < header->MeshDataOffset
         || ( dataSize < (header->MeshDataOffset + header->NumMeshes * sizeof(DXUT::SDKMESH_MESH) ) ) )
        throw std::exception("End of file");
    auto meshArray = reinterpret_cast<const DXUT::SDKMESH_MESH*>( meshData + header->MeshDataOffset );

    if ( dataSize < header->SubsetDataOffset
         || ( dataSize < (header->SubsetDataOffset + header->NumTotalSubsets * sizeof(DXUT::SDKMESH_SUBSET) ) ) )
        throw std::exception("End of file");
    auto subsetArray = reinterpret_cast<const DXUT::SDKMESH_SUBSET*>( meshData + header->SubsetDataOffset );

    if ( dataSize < header->FrameDataOffset
         || (dataSize < (header->FrameDataOffset + header->NumFrames * sizeof(DXUT::SDKMESH_FRAME) ) ) )
        throw std::exception("End of file");
    // TODO - auto frameArray = reinterpret_cast<const DXUT::SDKMESH_FRAME*>( meshData + header->FrameDataOffset );

    if ( dataSize < header->MaterialDataOffset
         || (dataSize < (header->MaterialDataOffset + header->NumMaterials * sizeof(DXUT::SDKMESH_MATERIAL) ) ) )
        throw std::exception("End of file");
    auto materialArray = reinterpret_cast<const DXUT::SDKMESH_MATERIAL*>( meshData + header->MaterialDataOffset );

    // Buffer data
    uint64_t bufferDataOffset = header->HeaderSize + header->NonBufferDataSize;
    if ( ( dataSize < bufferDataOffset )
         || ( dataSize < bufferDataOffset + header->BufferDataSize ) )
        throw std::exception("End of file");
    const uint8_t* bufferData = meshData + bufferDataOffset; 

    // Create vertex buffers
    std::vector<std::shared_ptr<std::vector<D3D12_INPUT_ELEMENT_DESC>>> vbDecls;
    vbDecls.resize( header->NumVertexBuffers );

    std::vector<bool> perVertexColor;
    perVertexColor.resize( header->NumVertexBuffers );

    std::vector<bool> enableSkinning;
    enableSkinning.resize( header->NumVertexBuffers );

    std::vector<bool> enableDualTexture;
    enableDualTexture.resize( header->NumVertexBuffers );

    std::vector<bool> enableNormalMaps;
    enableNormalMaps.resize(header->NumVertexBuffers);

    for( UINT j=0; j < header->NumVertexBuffers; ++j )
    {
        auto& vh = vbArray[j];

        if ( dataSize < vh.DataOffset
             || ( dataSize < vh.DataOffset + vh.SizeBytes ) )
            throw std::exception("End of file");

        vbDecls[j] = std::make_shared<std::vector<D3D12_INPUT_ELEMENT_DESC>>();
        bool vertColor = false;
        bool skinning = false;
        bool dualTexture = false;
        bool normalMaps = false;
        GetInputLayoutDesc( vh.Decl, *vbDecls[j].get(), vertColor, skinning, dualTexture, normalMaps );
        perVertexColor[j] = vertColor;
        enableSkinning[j] = skinning;
        enableDualTexture[j] = !skinning && dualTexture;
        enableNormalMaps[j] = !skinning && !dualTexture && normalMaps;
    }

    // Validate index buffers
    for (UINT j = 0; j < header->NumIndexBuffers; ++j)
    {
        auto& ih = ibArray[j];

        if (dataSize < ih.DataOffset
            || (dataSize < ih.DataOffset + ih.SizeBytes))
            throw std::exception("End of file");

        if (ih.IndexType != DXUT::IT_16BIT && ih.IndexType != DXUT::IT_32BIT)
            throw std::exception("Invalid index buffer type found");
    }

    // Create meshes
    std::vector<ModelMaterialInfo> materials;
    materials.resize( header->NumMaterials );

    std::map<std::wstring, int> textureDictionary;

    std::unique_ptr<Model> model(new Model);
    model->meshes.reserve( header->NumMeshes );

    uint32_t partCount = 0;

    for( UINT meshIndex = 0; meshIndex < header->NumMeshes; ++meshIndex )
    {
        auto& mh = meshArray[ meshIndex ];

        if ( !mh.NumSubsets
             || !mh.NumVertexBuffers
             || mh.IndexBuffer >= header->NumIndexBuffers
             || mh.VertexBuffers[0] >= header->NumVertexBuffers )
            throw std::exception("Invalid mesh found");

        // mh.NumVertexBuffers is sometimes not what you'd expect, so we skip validating it

        if ( dataSize < mh.SubsetOffset
             || (dataSize < mh.SubsetOffset + mh.NumSubsets*sizeof(UINT) ) )
            throw std::exception("End of file");

        auto subsets = reinterpret_cast<const UINT*>( meshData + mh.SubsetOffset );

        if ( mh.NumFrameInfluences > 0 )
        {
            if ( dataSize < mh.FrameInfluenceOffset
                 || (dataSize < mh.FrameInfluenceOffset + mh.NumFrameInfluences*sizeof(UINT) ) )
                throw std::exception("End of file");

            // TODO - auto influences = reinterpret_cast<const UINT*>( meshData + mh.FrameInfluenceOffset );
        }

        auto mesh = std::make_shared<ModelMesh>();
        wchar_t meshName[ DXUT::MAX_MESH_NAME ];
        MultiByteToWideChar( CP_ACP, MB_PRECOMPOSED, mh.Name, -1, meshName, DXUT::MAX_MESH_NAME );
        mesh->name = meshName;

        // Extents
        mesh->boundingBox.Center = mh.BoundingBoxCenter;
        mesh->boundingBox.Extents = mh.BoundingBoxExtents;
        BoundingSphere::CreateFromBoundingBox( mesh->boundingSphere, mesh->boundingBox );
       
        // Create subsets
        for( UINT j = 0; j < mh.NumSubsets; ++j )
        {
            auto sIndex = subsets[ j ];
            if ( sIndex >= header->NumTotalSubsets )
                throw std::exception("Invalid mesh found");

            auto& subset = subsetArray[ sIndex ];

            D3D_PRIMITIVE_TOPOLOGY primType;
            switch( subset.PrimitiveType )
            {
            case DXUT::PT_TRIANGLE_LIST:        primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;       break;
            case DXUT::PT_TRIANGLE_STRIP:       primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;      break;
            case DXUT::PT_LINE_LIST:            primType = D3D_PRIMITIVE_TOPOLOGY_LINELIST;           break;
            case DXUT::PT_LINE_STRIP:           primType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;          break;
            case DXUT::PT_POINT_LIST:           primType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;          break;
            case DXUT::PT_TRIANGLE_LIST_ADJ:    primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;   break;
            case DXUT::PT_TRIANGLE_STRIP_ADJ:   primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;  break;
            case DXUT::PT_LINE_LIST_ADJ:        primType = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;       break;
            case DXUT::PT_LINE_STRIP_ADJ:       primType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;      break;

            case DXUT::PT_QUAD_PATCH_LIST:
            case DXUT::PT_TRIANGLE_PATCH_LIST:
                throw std::exception("Direct3D9 era tessellation not supported");

            default:
                throw std::exception("Unknown primitive type");
            }

            if ( subset.MaterialID >= header->NumMaterials )
                throw std::exception("Invalid mesh found");

            auto& mat = materials[ subset.MaterialID ];

            const size_t vi = mh.VertexBuffers[0];
            InitMaterial(materialArray[subset.MaterialID],
                perVertexColor[vi], enableSkinning[vi], enableDualTexture[vi], enableNormalMaps[vi],
                mat, textureDictionary);

            auto part = new ModelMeshPart(partCount++);

            const auto& vh = vbArray[mh.VertexBuffers[0]];
            const auto& ih = ibArray[mh.IndexBuffer];

            part->indexCount = static_cast<uint32_t>( subset.IndexCount );
            part->startIndex = static_cast<uint32_t>( subset.IndexStart );
            part->vertexOffset = static_cast<uint32_t>( subset.VertexStart );
            part->vertexStride = static_cast<uint32_t>( vh.StrideBytes );
            part->indexFormat = ( ibArray[ mh.IndexBuffer ].IndexType == DXUT::IT_32BIT ) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
            part->primitiveType = primType; 

            // Vertex data
            auto verts = reinterpret_cast<const uint8_t*>(bufferData + (vh.DataOffset - bufferDataOffset));
            part->vertexBuffer = GraphicsMemory::Get().Allocate((size_t) vh.SizeBytes);
            memcpy(part->vertexBuffer.Memory(), verts, (size_t) vh.SizeBytes);

            // Index data
            auto indices = reinterpret_cast<const uint8_t*>(bufferData + (ih.DataOffset - bufferDataOffset));
            part->indexBuffer = GraphicsMemory::Get().Allocate((size_t) ih.SizeBytes);
            memcpy(part->indexBuffer.Memory(), indices, (size_t) ih.SizeBytes);

            part->materialIndex = subset.MaterialID;
            part->vbDecl = vbDecls[ mh.VertexBuffers[0] ];

            if (mat.alphaValue < 1.0f)
                mesh->alphaMeshParts.emplace_back( part );
            else
                mesh->opaqueMeshParts.emplace_back( part );
        }

        model->meshes.emplace_back( mesh );
    }

    // Copy the materials and texture names into contiguous arrays
    model->materials = std::move(materials);
    model->textureNames.resize(textureDictionary.size());
    for (auto texture = std::cbegin(textureDictionary); texture != std::cend(textureDictionary); ++texture)
    {
        model->textureNames[texture->second] = texture->first;
    }

    return model;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
std::unique_ptr<Model> DirectX::Model::CreateFromSDKMESH( const wchar_t* szFileName )
{
    size_t dataSize = 0;
    std::unique_ptr<uint8_t[]> data;
    HRESULT hr = BinaryReader::ReadEntireFile( szFileName, data, &dataSize );
    if ( FAILED(hr) )
    {
        DebugTrace( "CreateFromSDKMESH failed (%08X) loading '%ls'\n", hr, szFileName );
        throw std::exception( "CreateFromSDKMESH" );
    }

    auto model = CreateFromSDKMESH( data.get(), dataSize );

    model->name = szFileName;

    return model;
}
