#include "SlipModelExtract.h"

#include <iostream>
#include <fstream>
#include <filesystem>

#include "io.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "stb_image.h"
#include "stb_image_write.h"

std::string SlipModelExtract::writeMeshToCache(std::string& filepath, std::vector<SlipMesh::Mesh> meshes, std::vector<SlipMesh::Material> materials)
{
    std::replace(filepath.begin(), filepath.end(), '\\', '/');

    std::string mystr = filepath.substr(filepath.find("/assets/") + 8);

    std::filesystem::path path(mystr);
    std::string fileloc = path.parent_path().string() + "/" + path.stem().string();

    std::string filelocMat = "cache/" + path.parent_path().string() + "/materials/" + path.stem().string();
    char outPathMat[192] = "";

    std::strcpy(outPathMat, filelocMat.c_str());

    std::filesystem::create_directories("cache/" + path.parent_path().string());

    std::ofstream outCache("cache/"+ fileloc +".model_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE MODEL: " << "cache/" << fileloc.c_str() << ".model_cache" << std::endl;

    size_t meshesCount = meshes.size();
    IO::write(outCache, meshesCount);

    for (int i = 0; i < meshes.size(); i++)
    {
        size_t vertexCount = meshes[i].vertices.size();
        size_t indicesCount = meshes[i].indices.size();

        IO::write(outCache, meshes[i].materialIndex);
        IO::write(outCache, vertexCount);
        IO::write(outCache, indicesCount);

        for (int j = 0; j < vertexCount; j++)
        {
            float v3[3];
            float v2[2];

            v3[0] = meshes[i].vertices[j].Position.x;
            v3[1] = meshes[i].vertices[j].Position.y;
            v3[2] = meshes[i].vertices[j].Position.z;
            IO::write(outCache, v3);

            v3[0] = meshes[i].vertices[j].Normal.x;
            v3[1] = meshes[i].vertices[j].Normal.y;
            v3[2] = meshes[i].vertices[j].Normal.z;
            IO::write(outCache, v3);

            v2[0] = meshes[i].vertices[j].TexCoords.x;
            v2[1] = meshes[i].vertices[j].TexCoords.y;
            IO::write(outCache, v2);
        }

        for (int k = 0; k < indicesCount; k++)
        {
            IO::write(outCache, meshes[i].indices[k]);
        }
    }

    /*size_t vertexCount = vertices.size();
    size_t indicesCount = indices.size();

    IO::write(outCache, vertexCount);
    IO::write(outCache, indicesCount);

    for (int i = 0; i < vertexCount; i++)
    {
        float v3[3];
        float v2[2];

        v3[0] = vertices[i].Position.x;
        v3[1] = vertices[i].Position.y;
        v3[2] = vertices[i].Position.z;
        IO::write(outCache, v3);

        v3[0] = vertices[i].Normal.x;
        v3[1] = vertices[i].Normal.y;
        v3[2] = vertices[i].Normal.z;
        IO::write(outCache, v3);

        v2[0] = vertices[i].TexCoords.x;
        v2[1] = vertices[i].TexCoords.y;
        IO::write(outCache, v2);
    }

    for (int j = 0; j < indicesCount; j++)
    {
        IO::write(outCache, indices[j]);
    }*/

    char outPathCol[192] = "";
    IO::write(outCache, outPathCol);

    /*size_t matCount = 0;
    IO::write(outCache, matCount);*/

    size_t materialCount = materials.size();
    IO::write(outCache, materialCount);
    
    for (int i = 0; i < materials.size(); i++)
    {
        IO::write(outCache, materials[i].matPath);
    }

    outCache.close();

    return "cache/" + fileloc + ".model_cache";
}

std::string SlipModelExtract::writeMatToCache(std::string& filepath, const char* name)
{
    std::replace(filepath.begin(), filepath.end(), '\\', '/');

    std::string mystr = filepath.substr(filepath.find("/assets/") + 8);

    std::filesystem::path path(mystr);
    std::string fileloc = path.parent_path().string() + "/materials/" + name;

    std::filesystem::create_directories("cache/" + path.parent_path().string() + "/materials");

    std::ofstream outCache("cache/" + fileloc + ".material_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE MODEL: " << "cache/" << fileloc.c_str() << ".material_cache" << std::endl;

    float v3[3] = {1.f, 1.f, 1.f};

    IO::write(outCache, v3);

    v3[0] = 0.2f;
    v3[1] = 0.2f;
    v3[2] = 0.2f;
    IO::write(outCache, v3);

    v3[0] = 1.f;
    v3[1] = 1.f;
    v3[2] = 1.f;
    IO::write(outCache, v3);

    char shaderTemp[4] = "mvp";
    IO::write(outCache, shaderTemp);

    float shinTemp = 3.f;
    IO::write(outCache, shinTemp);

    char pathTxtTemp[192] = "";
    IO::write(outCache, pathTxtTemp);

    outCache.close();

    return fileloc;
}

void SlipModelExtract::save(SlipMesh& ms, std::string filepath)
{
    std::ofstream outCache(filepath, std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE MODEL: " << filepath << ".model_cache" << std::endl;

    size_t meshesCount = ms.meshes.size();
    IO::write(outCache, meshesCount);

    for (int i = 0; i < ms.meshes.size(); i++)
    {
        size_t vertexCount = ms.meshes[i].vertices.size();
        size_t indicesCount = ms.meshes[i].indices.size();

        IO::write(outCache, ms.meshes[i].materialIndex);
        IO::write(outCache, vertexCount);
        IO::write(outCache, indicesCount);

        for (int j = 0; j < vertexCount; j++)
        {
            float v3[3];
            float v2[2];

            v3[0] = ms.meshes[i].vertices[j].Position.x;
            v3[1] = ms.meshes[i].vertices[j].Position.y;
            v3[2] = ms.meshes[i].vertices[j].Position.z;
            IO::write(outCache, v3);

            v3[0] = ms.meshes[i].vertices[j].Normal.x;
            v3[1] = ms.meshes[i].vertices[j].Normal.y;
            v3[2] = ms.meshes[i].vertices[j].Normal.z;
            IO::write(outCache, v3);

            v2[0] = ms.meshes[i].vertices[j].TexCoords.x;
            v2[1] = ms.meshes[i].vertices[j].TexCoords.y;
            IO::write(outCache, v2);
        }

        for (int k = 0; k < indicesCount; k++)
        {
            IO::write(outCache, ms.meshes[i].indices[k]);
        }
    }

    IO::write(outCache, ms.colPath);

    size_t materialCount = ms.materials.size();
    IO::write(outCache, materialCount);
    
    for (int i = 0; i < ms.materials.size(); i++)
    {
        IO::write(outCache, ms.materials[i].matPath);
    }

    outCache.close();
}

void SlipModelExtract::save(SlipCollision ms, std::string filepath)
{
    std::ofstream outCache(filepath + ".physics_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE MODEL: " << filepath << ".model_cache" << std::endl;

    IO::write(outCache, ms.mass);

    size_t vertexCount = ms.vertices.size();
    size_t indicesCount = ms.indices.size();

    IO::write(outCache, vertexCount);
    IO::write(outCache, indicesCount);

    for (int j = 0; j < vertexCount; j++)
    {
        float v3[3];

        v3[0] = ms.vertices[j].Position.x;
        v3[1] = ms.vertices[j].Position.y;
        v3[2] = ms.vertices[j].Position.z;
        IO::write(outCache, v3);
    }

    for (int k = 0; k < indicesCount; k++)
    {
        IO::write(outCache, ms.indices[k]);
    }

    outCache.close();
}

void SlipModelExtract::save(SlipMaterial ms, std::string filepath)
{
    std::ofstream outCache(filepath + ".material_cache", std::ofstream::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE MODEL: " << "cache/" << filepath << ".material_cache" << std::endl;

    float v3[3];

    v3[0] = ms.color.x;
    v3[1] = ms.color.y;
    v3[2] = ms.color.z;
    IO::write(outCache, v3);

    v3[0] = ms.ambient.x;
    v3[1] = ms.ambient.y;
    v3[2] = ms.ambient.z;
    IO::write(outCache, v3);

    v3[0] = ms.specular.x;
    v3[1] = ms.specular.y;
    v3[2] = ms.specular.z;
    IO::write(outCache, v3);

    IO::write(outCache, ms.shaderType);

    IO::write(outCache, ms.shininess);

    IO::write(outCache, ms.diffPath);

    outCache.close();
}

void SlipModelExtract::save(SlipBsp bsp, std::string filepath)
{
    std::ofstream outCache(filepath + ".bsp_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE BSP: " << filepath << ".bsp_cache" << std::endl;

    unsigned int vertexCount = bsp.mesh.vertices.size();
    unsigned int indicesCount = bsp.mesh.indices.size();

    IO::write(outCache, vertexCount);
    IO::write(outCache, indicesCount);

    for (int i = 0; i < vertexCount; i++)
    {
        float vx, vy, vz;

        vx = bsp.mesh.vertices[i].Position.x;
        vy = bsp.mesh.vertices[i].Position.y;
        vz = bsp.mesh.vertices[i].Position.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);

        vx = bsp.mesh.vertices[i].Normal.x;
        vy = bsp.mesh.vertices[i].Normal.y;
        vz = bsp.mesh.vertices[i].Normal.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);

        vx = bsp.mesh.vertices[i].TexCoords.x;
        vy = bsp.mesh.vertices[i].TexCoords.y;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
    }

    for (int j = 0; j < indicesCount; j++)
    {
        IO::write(outCache, bsp.mesh.indices[j]);
    }

    unsigned int vertexColCount = bsp.coll.vertices.size();
    unsigned int indicesColCount = bsp.coll.indices.size();

    IO::write(outCache, vertexColCount);
    IO::write(outCache, indicesColCount);

    for (int h = 0; h < vertexColCount; h++)
    {
        float vx, vy, vz;

        vx = bsp.coll.vertices[h].Position.x;
        vy = bsp.coll.vertices[h].Position.y;
        vz = bsp.coll.vertices[h].Position.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);
    }

    for (int z = 0; z < indicesColCount; z++)
    {
        IO::write(outCache, bsp.coll.indices[z]);
    }

    IO::write(outCache, bsp.skyboxPath);

    for (int k = 0; k < 3; k++)
    {
        IO::write(outCache, bsp.mesh.mat.lightmaps[k].width);
        IO::write(outCache, bsp.mesh.mat.lightmaps[k].height);
        IO::write(outCache, bsp.mesh.mat.lightmaps[k].nrComponents);

        IO::write(outCache, bsp.mesh.mat.lightmaps[k].format);

        IO::write(outCache, bsp.mesh.mat.lightmaps[k].imgData.size);
        outCache.write(reinterpret_cast<char*>(&bsp.mesh.mat.lightmaps[k].imgData.data[0]), bsp.mesh.mat.lightmaps[k].imgData.size);
    }

    IO::write(outCache, bsp.mesh.mat.combineTexture);
    IO::write(outCache, bsp.mesh.mat.baseTextureR);
    IO::write(outCache, bsp.mesh.mat.baseTextureG);
    IO::write(outCache, bsp.mesh.mat.baseTextureB);

    outCache.close();
}

void SlipModelExtract::extract(std::string filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath, aiProcess_OptimizeMeshes | aiProcess_JoinIdenticalVertices | aiProcess_Triangulate);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        printf("ERROR::ASSIMP::%s", importer.GetErrorString());
    }
    
    vertices.clear();
    indices.clear();

    std::vector<SlipMesh::Mesh> meshes;
    std::vector<SlipMesh::Material> materials;

    for (int j = 0; j < scene->mNumMeshes; j++)
    {
        aiMesh* mesh = scene->mMeshes[j];

        SlipMesh::Mesh tempMesh;

        tempMesh.materialIndex = mesh->mMaterialIndex;

        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            glm::vec3 vector;
            // positions
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;
            // normals
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            // texture coordinates
            if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
            }
            else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            tempMesh.vertices.push_back(vertex);
        }
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // retrieve all indices of the face and store them in the indices vector
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                tempMesh.indices.push_back(face.mIndices[j]);
        }

        meshes.push_back(tempMesh);
    }

    for (int j = 0; j < scene->mNumMaterials; j++)
    {
        std::string matName = "mat_" + std::to_string(j);

        char matPath[192] = "";
        std::strcpy(matPath, writeMatToCache(filepath, matName.c_str()).c_str());

        materials.emplace_back(matPath);
    }

    writeMeshToCache(filepath, meshes, materials);
}

std::vector<Vertex> getVertices(std::string filepath)
{
    std::vector<Vertex> outVertices;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        printf("ERROR::ASSIMP::%s", importer.GetErrorString());
    }

    //for (aiMesh* mesh : scene->mMeshes)

    aiMesh* mesh = scene->mMeshes[0];

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        glm::vec3 vector;
        // positions
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.Position = vector;
        // normals
        if (mesh->HasNormals())
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.Normal = vector;
        }
        // texture coordinates
        if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
        {
            glm::vec2 vec;
            // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TexCoords = vec;
        }
        else
            vertex.TexCoords = glm::vec2(0.0f, 0.0f);

        outVertices.push_back(vertex);
    }

    return outVertices;
}

std::vector<unsigned int> getIndices(std::string filepath)
{
    std::vector<unsigned int> outIndices;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        printf("ERROR::ASSIMP::%s", importer.GetErrorString());
    }

    aiMesh* mesh = scene->mMeshes[0];

    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        // retrieve all indices of the face and store them in the indices vector
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            outIndices.push_back(face.mIndices[j]);
    }

    return outIndices;
}

void SlipModelExtract::extractCol(std::string filepath)
{
    vertices.clear();
    indices.clear();

    std::string another = filepath;

    std::replace(another.begin(), another.end(), '\\', '/');

    std::string mystr = another.substr(another.find("/assets/") + 8);

    std::filesystem::path path(mystr);
    std::string fileloc = path.parent_path().string() + "/" + path.stem().string();

    std::filesystem::create_directories("cache/" + path.parent_path().string());

    std::ofstream outCache("cache/" + fileloc + ".physics_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE PHYSICS: " << "cache/" << fileloc.c_str() << ".physics_cache" << std::endl;

    int mass = 3;
    IO::write(outCache, mass);

    vertices = getVertices(filepath);
    indices = getIndices(filepath);

    size_t vertexCount = vertices.size();
    size_t indicesCount = indices.size();

    IO::write(outCache, vertexCount);
    IO::write(outCache, indicesCount);

    for (int i = 0; i < vertexCount; i++)
    {
        float vx, vy, vz;

        vx = vertices[i].Position.x;
        vy = vertices[i].Position.y;
        vz = vertices[i].Position.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);
    }

    for (int j = 0; j < indicesCount; j++)
    {
        IO::write(outCache, indices[j]);
    }

    vertices.clear();
    indices.clear();

    outCache.close();
}

void SlipModelExtract::extractFromPreCache(SlipLevel* lvl, std::string filepath)
{
    std::string another = filepath;

    std::replace(another.begin(), another.end(), '\\', '/');

    std::string mystr = another.substr(another.find("/cache/") + 7);

    std::filesystem::path path(mystr);

    std::string file = path.parent_path().string() + "/" + path.stem().string();

    if (path.extension().string() == ".model_cache")
    {
        SlipMesh* outMesh = new SlipMesh{ file.c_str() };
        lvl->models.push_back(outMesh);
    }
}

void dummy_write(void* context, void* data, int len)
{
    ImageData* imgData = (ImageData*)context;

    imgData->size = len;
    imgData->data = new unsigned char[imgData->size];
    memcpy(imgData->data, data, imgData->size);
}

void SlipModelExtract::extractBsp(std::string filepath)
{
    vertices.clear();
    indices.clear();

    std::string another = filepath;

    std::replace(another.begin(), another.end(), '\\', '/');

    std::string mystr = another.substr(another.find("/assets/"));
    std::filesystem::path path(mystr);

    std::string filelocCol = path.parent_path().string() + "/" + path.stem().string() + "_collision" + path.extension().string();

    std::string filelocLightmap = path.parent_path().string() + "/textures/lightmap";

    std::string mystr2 = another.substr(another.find("/assets/") + 8);
    std::filesystem::path path2(mystr2);

    std::string fileloc = path2.parent_path().string() + "/" + path2.stem().string();
    
    std::filesystem::create_directories("cache/" + path2.parent_path().string());
    std::ofstream outCache("cache/" + fileloc + ".bsp_cache", std::ios::binary);

    if (!outCache.is_open())
        std::cout << "ERROR CREATING CACHE BSP: " << "cache/" << fileloc.c_str() << ".bsp_cache" << std::endl;

    vertices = getVertices(filepath);
    indices = getIndices(filepath);

    unsigned int vertexCount = vertices.size();
    unsigned int indicesCount = indices.size();

    IO::write(outCache, vertexCount);
    IO::write(outCache, indicesCount);

    for (int i = 0; i < vertexCount; i++)
    {
        float vx, vy, vz;

        vx = vertices[i].Position.x;
        vy = vertices[i].Position.y;
        vz = vertices[i].Position.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);

        vx = vertices[i].Normal.x;
        vy = vertices[i].Normal.y;
        vz = vertices[i].Normal.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);

        vx = vertices[i].TexCoords.x;
        vy = vertices[i].TexCoords.y;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
    }

    for (int j = 0; j < indicesCount; j++)
    {
        IO::write(outCache, indices[j]);
    }

    vertices.clear();
    indices.clear();

    vertices = getVertices(filelocCol);
    indices = getIndices(filelocCol);

    unsigned int vertexColCount = vertices.size();
    unsigned int indicesColCount = indices.size();

    IO::write(outCache, vertexColCount);
    IO::write(outCache, indicesColCount);

    for (int h = 0; h < vertexColCount; h++)
    {
        float vx, vy, vz;

        vx = vertices[h].Position.x;
        vy = vertices[h].Position.y;
        vz = vertices[h].Position.z;
        IO::write(outCache, vx);
        IO::write(outCache, vy);
        IO::write(outCache, vz);
    }

    for (int z = 0; z < indicesColCount; z++)
    {
        IO::write(outCache, indices[z]);
    }

    char pathTxt[192] = "";
    IO::write(outCache, pathTxt);

    for (int k = 0; k < 3; k++)
    {
        SlipTexture texture;

        stbi_set_flip_vertically_on_load(true);

        std::string currentNum = std::to_string(k);
        std::string currentLightmap = filelocLightmap + currentNum + ".png";
        unsigned char* data = stbi_load(currentLightmap.c_str(), &texture.width, &texture.height, &texture.nrComponents, 3);

        if (data)
        {
            if (texture.nrComponents == 3)
                texture.format = 0;
            else if (texture.nrComponents == 4)
                texture.format = 1;

            IO::write(outCache, texture.width);
            IO::write(outCache, texture.height);
            IO::write(outCache, texture.nrComponents);

            IO::write(outCache, texture.format);

            stbi_write_png_to_func(dummy_write, &texture.imgData, texture.width, texture.height, 3, data, texture.width * 3);

            IO::write(outCache, texture.imgData.size);
            outCache.write(reinterpret_cast<char*>(&texture.imgData.data[0]), texture.imgData.size);

            stbi_image_free(data);
        }
        else
        {
            printf("Texture failed to load at path: %s", currentLightmap.c_str());
            stbi_image_free(data);
        }
    }

    IO::write(outCache, pathTxt);
    IO::write(outCache, pathTxt);
    IO::write(outCache, pathTxt);
    IO::write(outCache, pathTxt);

    outCache.close();
}

void SlipModelExtract::extractTexture(std::string filepath)
{
    SlipTexture texture;

    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(filepath.c_str(), &texture.width, &texture.height, &texture.nrComponents, 3);

    if (data)
    {
        if (texture.nrComponents == 3)
            texture.format = 0;
        else if (texture.nrComponents == 4)
            texture.format = 1;

        std::string another = filepath;

        std::replace(another.begin(), another.end(), '\\', '/');

        std::string mystr = another.substr(another.find("/assets/") + 8);

        std::filesystem::path path(mystr);
        std::string fileloc = path.parent_path().string() + "/" + path.stem().string();

        std::filesystem::create_directories("cache/" + path.parent_path().string() + "/");

        std::ofstream outCache("cache/" + fileloc + ".texture_cache", std::ios::binary);

        if (!outCache.is_open())
            printf("ERROR CREATING CACHE TEXTURE: %s", "cache/" + fileloc + ".texture_cache");

        IO::write(outCache, texture.width);
        IO::write(outCache, texture.height);
        IO::write(outCache, texture.nrComponents);

        IO::write(outCache, texture.format);

        stbi_write_png_to_func(dummy_write, &texture.imgData, texture.width, texture.height, 3, data, texture.width * 3);

        IO::write(outCache, texture.imgData.size);
        outCache.write(reinterpret_cast<char*>(&texture.imgData.data[0]), texture.imgData.size);

        outCache.close();

        stbi_image_free(data);
    }
    else
    {
        printf("Texture failed to load at path: %s", filepath.c_str());
        stbi_image_free(data);
    }
}

SlipModelExtract::SlipModelExtract()
{
}
