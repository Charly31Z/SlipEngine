#include "SlipCollision.h"

void SlipCollision::init()
{

    /*btTriangleMesh* triangles = new btTriangleMesh();

    for (int j = 0; j < indices.size(); j += 3)
    {
        btVector3 Tri1Pos{ vertices[indices[j]].Position.x, vertices[indices[j]].Position.y, vertices[indices[j]].Position.z };
        btVector3 Tri2Pos{ vertices[indices[j + 1]].Position.x, vertices[indices[j + 1]].Position.y, vertices[indices[j + 1]].Position.z };
        btVector3 Tri3Pos{ vertices[indices[j + 2]].Position.x, vertices[indices[j + 2]].Position.y, vertices[indices[j + 2]].Position.z };

        triangles->addTriangle(Tri1Pos, Tri2Pos, Tri3Pos);
    }

    collisionModel = new btBvhTriangleMeshShape(triangles, true, true);*/

    switch (col_type)
    {
    case SlipCollision::BOX:
        collisionModel = new btBoxShape(btVector3(scale[0], scale[1], scale[2]));
        break;
    case SlipCollision::SPHERE:
        collisionModel = new btSphereShape(btScalar(scale[0]));
        break;
    case SlipCollision::CAPSULE:
        collisionModel = new btCapsuleShape(btScalar(scale[0]), btScalar(scale[1]));
        break;
    case SlipCollision::CONE:
        collisionModel = new btConeShape(btScalar(scale[0]), btScalar(scale[1]));
        break;
    case SlipCollision::CYLINDER:
        collisionModel = new btCylinderShape(btVector3(scale[0], scale[1], scale[2]));
        break;
    }
}

void SlipCollision::initRigidBody(glm::vec3 pos, glm::vec3 rot)
{
    btTransform transformCol(
        btQuaternion(rot.x, rot.y, rot.z, 1.f),
        btVector3(pos.x, pos.y, pos.z));

    btVector3 localInertia(0, 0, 0);
    collisionModel->calculateLocalInertia(10, localInertia);

    motionstate = new btDefaultMotionState(transformCol);

    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(
        mass,                  // mass, in kg. 0 -> Static object, will never move.
        motionstate,
        collisionModel,  // collision shape of body
        localInertia    // local inertia
    );

    rigidBody = new btRigidBody(rigidBodyCI);
}

SlipCollision::SlipCollision(std::string path) : path(path)
{
    std::ifstream input(path + ".physics_cache", std::ios::binary);

    IO::read(input, mass);
    IO::read(input, col_type);

    IO::read(input, scale);

    input.close();
}
