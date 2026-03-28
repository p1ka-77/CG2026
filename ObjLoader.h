#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

using namespace DirectX;

struct ObjVertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
};

struct ObjMaterial
{
    std::string Name;
    XMFLOAT4 DiffuseAlbedo  = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT3 FresnelR0      = { 0.04f, 0.04f, 0.04f };
    float    Roughness      = 0.5f;
    std::wstring DiffuseMapPath;
    int          SrvHeapIndex = -1;
};

struct ObjSubMesh
{
    std::string              MaterialName;
    std::vector<uint32_t>    Indices;
};

struct ObjMesh
{
    std::vector<ObjVertex>                          Vertices;
    std::vector<ObjSubMesh>                         SubMeshes;
    std::unordered_map<std::string, ObjMaterial>    Materials;
};

class ObjLoader
{
public:
    static ObjMesh Load(const std::string& filePath, const std::string& baseDir = "");

private:
    static void ParseMtl(const std::string& mtlPath,
                         const std::string& baseDir,
                         std::unordered_map<std::string, ObjMaterial>& materials);
};
