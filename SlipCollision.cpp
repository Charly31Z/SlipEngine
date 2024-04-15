#include "SlipCollision.h"

void SlipCollision::init()
{
    btTriangleMesh* triangles = new btTriangleMesh();

    for (int j = 0; j < indices.size(); j += 3)
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
    btTransform transformCol(
        btQuaternion(rot.x, rot.y, rot.z, 1.f),
        btVector3(pos.x, pos.y, pos.z));

    motionstate = new btDefaultMotionState(transformCol);

    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(
        mass,                  // mass, in kg. 0 -> Static object, will never move.
        motionstate,
        collisionShape,  // collision shape of body
        btVector3(0, 0, 0)    // local inertia
    );

    rigidBody = new btRigidBody(rigidBodyCI);

    SlipPhysics::Get().getWorld().addRigidBody(rigidBody);
}

SlipCollision::SlipCollision(std::string path) : path(path)
{
    std::ifstream input("cache/" + path + ".physics_cache", std::ios::binary);

    IO::read(input, mass);

    size_t vertexCount;
    size_t indicesCount;

    IO::read(input, vertexCount);
    IO::read(input, indicesCount);

    for (int j = 0; j < vertexCount; j++)
    {
        Vertex vertex;
        
        float v3[3];

        IO::read(input, v3);
        vertex.Position = glm::vec3(v3[0], v3[1], v3[2]);

        vertices.push_back(vertex);
    }

    for (int k = 0; k < indicesCount; k++)
    {
        unsigned int index;

        IO::read(input, index);
        indices.push_back(index);
    }

    input.close();
}
