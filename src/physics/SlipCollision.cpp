#include "SlipCollision.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <cstring>

SlipCollision::SlipCollision()
{
}

SlipCollision::SlipCollision(const std::vector<glm::vec3>& positions, const std::vector<unsigned int>& indices, int mass)
    : mass(mass)
{
    for (const auto& p : positions) {
        Vertex v;
        v.Position = p;
        vertices.push_back(v);
    }
    this->indices = indices;
    init();
}

SlipCollision::~SlipCollision()
{
    if (rigidBody) {
        SlipPhysics::Get().getWorld().removeRigidBody(rigidBody);
        delete rigidBody;
        rigidBody = nullptr;
    }
    if (motionstate) {
        delete motionstate;
        motionstate = nullptr;
    }
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
}

void SlipCollision::init()
{
    if (indices.empty() || vertices.empty()) return;

    btTriangleMesh* triangles = new btTriangleMesh();

    for (size_t j = 0; j + 2 < indices.size(); j += 3)
    {
        btVector3 Tri1Pos{ vertices[indices[j]].Position.x, vertices[indices[j]].Position.y, vertices[indices[j]].Position.z };
        btVector3 Tri2Pos{ vertices[indices[j + 1]].Position.x, vertices[indices[j + 1]].Position.y, vertices[indices[j + 1]].Position.z };
        btVector3 Tri3Pos{ vertices[indices[j + 2]].Position.x, vertices[indices[j + 2]].Position.y, vertices[indices[j + 2]].Position.z };

        triangles->addTriangle(Tri1Pos, Tri2Pos, Tri3Pos);
    }

    collisionShape = new btBvhTriangleMeshShape(triangles, true, true);
}

void SlipCollision::initRigidBody(glm::vec3 pos, glm::vec3 rot)
{
    if (!collisionShape) {
        init();
    }
    if (!collisionShape) return;

    btTransform transformCol(
        btQuaternion(rot.x, rot.y, rot.z, 1.f),
        btVector3(pos.x, pos.y, pos.z));

    motionstate = new btDefaultMotionState(transformCol);

    btVector3 localInertia(0, 0, 0);
    if (mass > 0) {
        collisionShape->calculateLocalInertia(static_cast<btScalar>(mass), localInertia);
    }

    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(
        static_cast<btScalar>(mass),
        motionstate,
        collisionShape,
        localInertia
    );

    rigidBody = new btRigidBody(rigidBodyCI);
    SlipPhysics::Get().getWorld().addRigidBody(rigidBody);
}

SlipCollision::SlipCollision(std::string path) : path(path)
{
    std::string candidatePath;
    std::vector<std::string> searchPaths = {
        path,
        "cache/" + path + ".physics_data",
        "cache/" + path + ".physics_cache",
        "cache/" + path
    };

    std::string resolvedPath;
    for (const auto& sp : searchPaths) {
        if (SlipFileSystem::Exists(sp)) {
            resolvedPath = sp;
            break;
        }
    }

    if (resolvedPath.empty()) return;

    std::vector<uint8_t> bytes;
    if (!SlipFileSystem::ReadAllBytes(resolvedPath, bytes) || bytes.empty()) {
        return;
    }

    size_t cursor = 0;
    auto readBytes = [&bytes, &cursor](void* dest, size_t size) -> bool {
        if (cursor + size > bytes.size()) return false;
        std::memcpy(dest, &bytes[cursor], size);
        cursor += size;
        return true;
    };

    readBytes(&mass, sizeof(mass));

    size_t vertexCount = 0;
    size_t indicesCount = 0;

    readBytes(&vertexCount, sizeof(vertexCount));
    readBytes(&indicesCount, sizeof(indicesCount));

    for (size_t j = 0; j < vertexCount; j++)
    {
        Vertex vertex;
        float v3[3];
        if (readBytes(v3, sizeof(v3))) {
            vertex.Position = glm::vec3(v3[0], v3[1], v3[2]);
            vertices.push_back(vertex);
        }
    }

    for (size_t k = 0; k < indicesCount; k++)
    {
        unsigned int index = 0;
        if (readBytes(&index, sizeof(index))) {
            indices.push_back(index);
        }
    }
}
