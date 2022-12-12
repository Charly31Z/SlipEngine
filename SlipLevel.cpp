#include "SlipLevel.h"

#include <fstream>
#include <filesystem>
#include <shlwapi.h>

#include <assert.h>

#include "SlipGlobals.h"
#include "SlipActor.h"

void SlipLevel::newLevel(std::string levelName)
{
	this->levelName = levelName;

	mBsp = nullptr;

	std::fill(std::begin(bsp), std::end(bsp), '\0');

	models.clear();

	canDraw = true;
}

void SlipLevel::saveLevel()
{
	std::string savingPathFolder = "cache\\levels\\" + levelName;

	if (!std::filesystem::is_directory(savingPathFolder))
		std::filesystem::create_directory(savingPathFolder);

	std::ofstream out(savingPathFolder + "\\" + levelName + ".level_cache");

	IO::write(out, bsp);

	size_t modelCount = models.size();

	IO::write(out, modelCount);

	if (!models.empty())
	{
		for (int i = 0; i < modelCount; i++)
		{
			char cachePathMesh[192];
			strncpy(cachePathMesh, models[i].path.c_str(), sizeof(cachePathMesh));
			IO::write(out, cachePathMesh);
		}
	}

	out.close();
}

void SlipLevel::openLevel(std::string levelName)
{
	canDraw = false;
	mBsp = nullptr;
	models.clear();
	entities.clear();

	entities.push_back(m_Camera);

	std::filesystem::path path(levelName);
	std::string filename = path.stem().string();

	this->levelName = filename;

	std::ifstream input("cache/levels/" + filename + "/" + filename + ".level_cache", std::ios::binary);

	IO::read(input, bsp);

	if (std::strlen(bsp) != 0)
		mBsp = new SlipBsp(bsp);

	size_t modelCount;

	IO::read(input, modelCount);

	for (int i = 0; i < modelCount; i++)
	{
		char cachePathMesh[192];
		IO::read(input, cachePathMesh);
		SlipMesh cacheMesh{ cachePathMesh };

		models.push_back(cacheMesh);
	}

	input.close();

	canDraw = true;
}

void SlipLevel::draw()
{
	if (canDraw && mBsp != nullptr)
	{
		if (!playing)
		{
			if (!mBsp->initialized)
			{
				// Build the broadphase
				broadphase = new btDbvtBroadphase();

				// Set up the collision configuration and dispatcher
				collisionConfiguration = new btDefaultCollisionConfiguration();
				dispatcher = new btCollisionDispatcher(collisionConfiguration);

				// The actual physics solver
				solver = new btSequentialImpulseConstraintSolver;

				// The world.
				dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
				dynamicsWorld->setGravity(btVector3(0, 0, 0));

				debugCol = new DebugCollision();
				debugCol->setDebugMode(debugCol->DBG_DrawWireframe);

				dynamicsWorld->setDebugDrawer(debugCol);

				mBsp->initCol();
				dynamicsWorld->addRigidBody(mBsp->coll.rigidBody);

				for (int n = 0; n < models.size(); n++)
				{
					std::string tempResult = std::string(mBsp->skyboxPath);
					if (models[n].path == tempResult + ".model_cache")
						mBsp->init(models[n]);
				}
			}

			mBsp->draw();

			dynamicsWorld->debugDrawObject(mBsp->coll.rigidBody->getWorldTransform(), mBsp->coll.collisionShape, btVector3(0.f, 1.f, 0.f));

			for (int e = 0; e < entities.size(); e++)
			{
				entities[e]->ejecStart();
				entities[e]->Update();

				if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
				{
					p->draw();

					if (p->model->collision != nullptr)
					{
						btTransform tempTrans{ btQuaternion(p->rotation.x, p->rotation.y, p->rotation.z), btVector3(p->position.x, p->position.y, p->position.z) };
						dynamicsWorld->debugDrawObject(tempTrans, p->model->collision->collisionModel, btVector3(1.f, 0.f, 0.f));
					}
				}
			}

			dynamicsWorld->stepSimulation(SlipGlobals::Get().GetDeltaTime());

			dynamicsWorld->debugDrawWorld();

			debugCol->render(*m_Camera);
		}
		else
		{
			if (!mBsp->initialized)
			{
				// Build the broadphase
				broadphase = new btDbvtBroadphase();

				// Set up the collision configuration and dispatcher
				collisionConfiguration = new btDefaultCollisionConfiguration();
				dispatcher = new btCollisionDispatcher(collisionConfiguration);

				// The actual physics solver
				solver = new btSequentialImpulseConstraintSolver;

				// The world.
				//dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
				dynamicsWorld->setGravity(btVector3(0, -9.81f, 0));

				mBsp->initCol();
				dynamicsWorld->addRigidBody(mBsp->coll.rigidBody);

				for (int n = 0; n < models.size(); n++)
				{
					std::string tempResult = std::string(mBsp->skyboxPath);
					if (models[n].path == tempResult + ".model_cache")
						mBsp->init(models[n]);
				}
			}

			mBsp->draw();

			for (int e = 0; e < entities.size(); e++)
			{
				entities[e]->ejecStart();
				entities[e]->Update();

				if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
				{
					p->init();

					if (!p->initializedRigidBody)
					{
						p->initRigiedBody();
						if (p->model->collision != nullptr)
						{
							dynamicsWorld->addRigidBody(p->model->collision->rigidBody);
						}
					}

					p->draw();
				}
			}

			dynamicsWorld->stepSimulation(SlipGlobals::Get().GetDeltaTime());
		}
	}
}

void SlipLevel::clean()
{
	if (mBsp != nullptr)
	{
		mBsp->clean();

		for (int e = 0; e < entities.size(); e++)
		{
			if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
			{
				p->model->clean();
			}
		}

		debugCol->clean();
	}
}

int SlipLevel::searchModel(std::string path)
{
	for (int i = 0; i < models.size(); i++)
	{
		std::filesystem::path root(models[i].path);
		std::string loc = root.parent_path().string() + "/" + root.stem().string();

		if (path == loc)
			return i;
	}
	return 9999;
}

SlipLevel::SlipLevel(std::string levelName)
{
	assert(!m_Instance && "Level has initialized...");
	m_Instance = this;
	this->levelName = levelName;

	// camera
	m_Camera = new SlipCamera(glm::vec3(-6.f, 10.f, 15.f));
}

void SlipLevel::playMode()
{
	playing = !playing;
	openLevel(levelName);
}

