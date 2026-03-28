#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>
#include <algorithm>

// Хелпер: string -> wstring
static std::wstring ToWide(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

// Хелпер: получить папку из пути
static std::string GetDir(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return path.substr(0, pos + 1);
}

void ObjLoader::ParseMtl(const std::string& mtlPath,
                          const std::string& baseDir,
                          std::unordered_map<std::string, ObjMaterial>& materials)
{
    std::ifstream file(mtlPath);
    if (!file.is_open()) return;

    ObjMaterial* cur = nullptr;
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "newmtl")
        {
            std::string name; ss >> name;
            materials[name] = ObjMaterial{};
            materials[name].Name = name;
            cur = &materials[name];
        }
        else if (cur && token == "Kd")
        {
            ss >> cur->DiffuseAlbedo.x
               >> cur->DiffuseAlbedo.y
               >> cur->DiffuseAlbedo.z;
        }
        else if (cur && token == "d")
        {
            ss >> cur->DiffuseAlbedo.w;
        }
        else if (cur && token == "Ns")
        {
            float ns; ss >> ns;
            float clamped = ns / 1000.0f;
            if (clamped > 1.0f) clamped = 1.0f;
            cur->Roughness = 1.0f - clamped;
        }
        else if (cur && (token == "map_Kd" || token == "map_Ka"))
        {
            std::string texName; ss >> texName;
            std::string texPath = baseDir + texName;
            cur->DiffuseMapPath = ToWide(texPath);
        }
    }
}

ObjMesh ObjLoader::Load(const std::string& filePath, const std::string& baseDir)
{
    std::ifstream file(filePath);
    if (!file.is_open())
        throw std::runtime_error("ObjLoader: cannot open file " + filePath);

    const std::string dir = baseDir.empty() ? GetDir(filePath) : baseDir;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;

    ObjMesh mesh;
    ObjSubMesh* curSubMesh = nullptr;

    std::map<std::tuple<int,int,int>, uint32_t> vertexCache;

    auto getOrAddVertex = [&](int pi, int ti, int ni) -> uint32_t
    {
        auto key = std::make_tuple(pi, ti, ni);
        auto it = vertexCache.find(key);
        if (it != vertexCache.end()) return it->second;

        ObjVertex v{};
        if (pi >= 0 && pi < (int)positions.size())  v.Position = positions[pi];
        if (ni >= 0 && ni < (int)normals.size())     v.Normal   = normals[ni];
        if (ti >= 0 && ti < (int)texcoords.size())   v.TexCoord = texcoords[ti];

        uint32_t idx = (uint32_t)mesh.Vertices.size();
        mesh.Vertices.push_back(v);
        vertexCache[key] = idx;
        return idx;
    };

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "mtllib")
        {
            std::string mtlName; ss >> mtlName;
            ParseMtl(dir + mtlName, dir, mesh.Materials);
        }
        else if (token == "usemtl")
        {
            std::string matName; ss >> matName;
            mesh.SubMeshes.emplace_back();
            curSubMesh = &mesh.SubMeshes.back();
            curSubMesh->MaterialName = matName;
        }
        else if (token == "v")
        {
            XMFLOAT3 p; ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (token == "vn")
        {
            XMFLOAT3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (token == "vt")
        {
            XMFLOAT2 t; ss >> t.x >> t.y;
            t.y = 1.0f - t.y;
            texcoords.push_back(t);
        }
        else if (token == "f")
        {
            if (!curSubMesh)
            {
                mesh.SubMeshes.emplace_back();
                curSubMesh = &mesh.SubMeshes.back();
            }

            std::vector<uint32_t> faceVerts;
            std::string faceToken;
            while (ss >> faceToken)
            {
                int pi = -1, ti = -1, ni = -1;
                if (sscanf_s(faceToken.c_str(), "%d/%d/%d", &pi, &ti, &ni) == 3) {}
                else if (sscanf_s(faceToken.c_str(), "%d//%d", &pi, &ni) == 2) {}
                else if (sscanf_s(faceToken.c_str(), "%d/%d", &pi, &ti) == 2) {}
                else sscanf_s(faceToken.c_str(), "%d", &pi);

                auto fix = [](int idx, int total) -> int {
                    if (idx  > 0) return idx - 1;
                    if (idx  < 0) return total + idx;
                    return -1;
                };
                pi = fix(pi, (int)positions.size());
                ti = fix(ti, (int)texcoords.size());
                ni = fix(ni, (int)normals.size());

                faceVerts.push_back(getOrAddVertex(pi, ti, ni));
            }

            for (size_t i = 1; i + 1 < faceVerts.size(); ++i)
            {
                curSubMesh->Indices.push_back(faceVerts[0]);
                curSubMesh->Indices.push_back(faceVerts[i]);
                curSubMesh->Indices.push_back(faceVerts[i + 1]);
            }
        }
    }

    return mesh;
}
