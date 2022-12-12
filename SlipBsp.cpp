#include "SlipBsp.h"

#include "io.h"

#include "Engine.h"

void SlipBsp::initCol()
{
    btTriangleMesh *triangles = new btTriangleMesh();

    for (int j = 0; j < coll.indices.size(); j += 3)
    {
        btVector3 Tri1Pos{ coll.vertices[coll.indices[j]].Position.x, coll.vertices[coll.indices[j]].Position.y, coll.vertices[coll.indices[j]].Position.z };
        btVector3 Tri2Pos{ coll.vertices[coll.indices[j + 1]].Position.x, coll.vertices[coll.indices[j + 1]].Position.y, coll.vertices[coll.indices[j + 1]].Position.z };
        btVector3 Tri3Pos{ coll.vertices[coll.indices[j + 2]].Position.x, coll.vertices[coll.indices[j + 2]].Position.y, coll.vertices[coll.indices[j + 2]].Position.z };

        triangles->addTriangle(Tri1Pos, Tri2Pos, Tri3Pos);
    }

    coll.collisionShape = new btBvhTriangleMeshShape(triangles, true, true);

    btDefaultMotionState* motionstate = new btDefaultMotionState(btTransform(
        btQuaternion(0.f, 0.f, 0.f, 1.f),
        btVector3(0.f, 0.f, 0.f)
    ));

    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(
        0,                  // mass, in kg. 0 -> Static object, will never move.
        motionstate,
        coll.collisionShape,  // collision shape of body
        btVector3(0, 0, 0)    // local inertia
    );
    coll.rigidBody = new btRigidBody(rigidBodyCI);
}

SlipBsp::SlipBsp(std::string path) : path(path)
{
	std::ifstream input(path + ".bsp_cache", std::ios::binary);

    if (!input.is_open())
        std::cout << "ERROR READING BSP: " << path.c_str() << ".bsp_cache" << std::endl;

	unsigned int vertexCount;
    unsigned int indicesCount;

	IO::read(input, vertexCount);
	IO::read(input, indicesCount);

    for (int i = 0; i < vertexCount; i++)
    {
        std::streamoff pos = input.tellg();
        std::cout << pos << std::endl;

        Vertex vertex;

        UCHAR b[4];

        float vx, vy, vz;

        IO::read(input, b);
        memcpy(&vx, &b, sizeof(vx));

        pos = input.tellg();
        std::cout << pos << std::endl;

        IO::read(input, b);
        memcpy(&vy, &b, sizeof(vy));

        pos = input.tellg();
        std::cout << pos << std::endl;

        IO::read(input, b);
        memcpy(&vz, &b, sizeof(vz));

        pos = input.tellg();
        std::cout << pos << std::endl;

        vertex.Position = glm::vec3(vx, vy, vz);

        IO::read(input, b);
        memcpy(&vx, &b, sizeof(vx));

        IO::read(input, b);
        memcpy(&vy, &b, sizeof(vy));

        IO::read(input, b);
        memcpy(&vz, &b, sizeof(vz));

        vertex.Normal = glm::vec3(vx, vy, vz);

        IO::read(input, b);
        memcpy(&vx, &b, sizeof(vx));

        IO::read(input, b);
        memcpy(&vy, &b, sizeof(vy));

        vertex.TexCoords = glm::vec2(vx, vy);

        mesh.vertices.push_back(vertex);
    }

    for (int j = 0; j < indicesCount; j++)
    {
        std::streamoff pos = input.tellg();
        std::cout << pos << std::endl;

        unsigned int index;

        IO::read(input, index);
        mesh.indices.push_back(index);
    }

    IO::read(input, vertexCount);
    IO::read(input, indicesCount);

    for (int h = 0; h < vertexCount; h++)
    {
        std::streamoff pos = input.tellg();
        std::cout << pos << std::endl;

        Vertex vertex;

        float vx, vy, vz;

        IO::read(input, vx);
        IO::read(input, vy);
        IO::read(input, vz);
        vertex.Position = glm::vec3(vx, vy, vz);

        coll.vertices.push_back(vertex);
    }

    for (int z = 0; z < indicesCount; z++)
    {
        std::streamoff pos = input.tellg();
        std::cout << pos << std::endl;

        unsigned int index;

        IO::read(input, index);
        coll.indices.push_back(index);
    }

    IO::read(input, skyboxPath);

    for (int k = 0; k < 3; k++)
    {
        std::streamoff pos = input.tellg();
        std::cout << pos << std::endl;

        SlipTexture texture;

        IO::read(input, texture.width);
        IO::read(input, texture.height);
        IO::read(input, texture.nrComponents);
        IO::read(input, texture.format);

        IO::read(input, texture.imgData.size);

        texture.imgData.data = new unsigned char[texture.imgData.size];
        input.read((char*)&texture.imgData.data[0], texture.imgData.size);

        mesh.mat.lightmaps[k] = texture;
    }

    IO::read(input, mesh.mat.pathCombineTxt);
    IO::read(input, mesh.mat.pathBaseTxtR);
    IO::read(input, mesh.mat.pathBaseTxtG);
    IO::read(input, mesh.mat.pathBaseTxtB);

	input.close();
}

void SlipBsp::init(SlipMesh skymesh)
{
    // create buffers/arrays
    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);
    // load data into vertex buffers
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    // A great thing about structs is that their memory layout is sequential for all its items.
    // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
    // again translates to 3/2 floats which translates to a byte array.
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), &mesh.vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), &mesh.indices[0], GL_STATIC_DRAW);

    // set the vertex attribute pointers
    // vertex Positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    // vertex normals
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
    // vertex texture coords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
    // vertex tangent
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
    // vertex bitangent
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
    // ids
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

    // weights
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
    glBindVertexArray(0);

    shader = new SlipShader("assets/shaders/bsp.vert", "assets/shaders/bsp.frag");

    // textures
    mesh.mat.lightmaps[0].init();

    shader->use();

    shader->setInt("lightmapTexture", 0);

    skybox = new SlipMesh(skymesh.path.c_str());
    skybox->init();

    initialized = true;
}

void SlipBsp::draw()
{
    if (skybox != nullptr)
    {
        glm::mat4 model = glm::mat4(1.0f);
        skybox->draw(model);
    }

    // draw mesh
    shader->use();

    shader->setInt("lightmapTexture", 0);
    glActiveTexture(GL_TEXTURE0);
    mesh.mat.lightmaps[0].draw();

    glm::mat4 proj = Engine::Get().GetPrimaryCamera().GetProjectionMatrix();
    glm::mat4 view = Engine::Get().GetPrimaryCamera().GetViewMatrix();

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);

    glm::mat4 model = glm::mat4(1.0f);
    shader->setMat4("model", model);

    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);

    btScalar transform[16];

    btTransform trans;
    trans = coll.rigidBody->getWorldTransform();
    trans.getOpenGLMatrix(transform);
}

void SlipBsp::clean()
{
    if (skybox != nullptr)
        skybox->clean();

    glDeleteVertexArrays(1, &mesh.VAO);
    glDeleteBuffers(1, &mesh.VBO);
    glDeleteBuffers(1, &mesh.EBO);

    shader->destroy();
}
