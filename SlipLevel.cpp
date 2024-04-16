#include "SlipLevel.h"

#include <fstream>
#include <filesystem>
#include <shlwapi.h>

#include "SlipPhysics.h"
#include "SlipGlobals.h"
#include "SlipActor.h"


#include "SlipID.h"
#include "SlipDebug.h"

#ifdef SLIP_EDITOR
#include "SlipEditor.h"
#include "FileDialog.h"
#endif

/*void SlipLevel::newLevel(std::string path)
{
	this->path = path;

	entities.clear();
	spawns.clear();

	std::fill(std::begin(bspPath), std::end(bspPath), '\0');

	models.clear();

	SlipPhysics::Get().clean();
	SlipID::Get().reset();

#ifdef SLIP_EDITOR
	SlipEditor::Get().print("Loaded level: " + path);
#endif

	canDraw = true;
}

void SlipLevel::saveLevel()
{
#ifdef SLIP_EDITOR
	std::string savingPathFolder = IO::FileDialog::SaveFile("Level Cache (*.level_cache)\0 *.level_cache;");
#else
	std::string savingPathFolder = "NULL";
#endif

	std::ofstream out(savingPathFolder, std::ios::binary);

	IO::write(out, bspPath);

	size_t modelCount = models.size();

	IO::write(out, modelCount);

	if (!models.empty())
	{
		for (int i = 0; i < modelCount; i++)
		{
			char cachePathMesh[192];
			strncpy(cachePathMesh, models[i]->path.c_str(), sizeof(cachePathMesh));
			IO::write(out, cachePathMesh);
		}
	}

	size_t spawnCount = spawns.size();

	IO::write(out, spawnCount);

	if (!spawns.empty())
	{
		for (int i = 0; i < spawnCount; i++)
		{
			float juas[3];

			juas[0] = spawns[i]->position.x;
			juas[1] = spawns[i]->position.y;
			juas[2] = spawns[i]->position.z;
			IO::write(out, juas);

			juas[0] = spawns[i]->rotation.x;
			juas[1] = spawns[i]->rotation.y;
			juas[2] = spawns[i]->rotation.z;
			IO::write(out, juas);
			IO::write(out, spawns[i]->team);
		}
	}

	size_t entitiesCount = entities.size();
	IO::write(out, entitiesCount);

	if (!entities.empty())
	{
		for (int i = 0; i < entitiesCount; i++)
		{
			float juas[3];

			juas[0] = entities[i]->position.x;
			juas[1] = entities[i]->position.y;
			juas[2] = entities[i]->position.z;
			IO::write(out, juas);

			juas[0] = entities[i]->rotation.x;
			juas[1] = entities[i]->rotation.y;
			juas[2] = entities[i]->rotation.z;
			IO::write(out, juas);

			juas[0] = entities[i]->scale.x;
			juas[1] = entities[i]->scale.y;
			juas[2] = entities[i]->scale.z;
			IO::write(out, juas);

			if (SlipActor* p = dynamic_cast<SlipActor*>(entities[i]))
			{
				for (int n = 0; n < modelCount; n++)
				{
					if (p->modelPath == models[n]->path)
					{
						IO::write(out, n);
					}
				}
			}
		}
	}

	IO::write(out, terrain->heightfieldIMG);
	IO::write(out, terrain->collisionPath);

	out.close();
}

int SlipLevel::openLevel(std::string path)
{
	if (path.empty())
		return 0;

	canDraw = false;
	models.clear();
	entities.clear();
	spawns.clear();

	SlipPhysics::Get().clean();
	SlipID::Get().reset();

	//entities.push_back(m_Camera);

	this->path = path;

	std::ifstream input(path, std::ios::binary);

	IO::read(input, bspPath);

	if (std::strlen(bspPath) != 0)
	{
		bsp = new SlipBsp(bspPath);
	}

	size_t modelCount;

	IO::read(input, modelCount);

	for (int i = 0; i < modelCount; i++)
	{
		char cachePathMesh[192];
		IO::read(input, cachePathMesh);
		SlipMesh* cacheMesh = new SlipMesh{ cachePathMesh };
		cacheMesh->init();
		cacheMesh->initColl();

		models.push_back(cacheMesh);
	}

	size_t spawnCount;
	IO::read(input, spawnCount);

	for (int i = 0; i < spawnCount; i++)
	{
		SlipSpawn* spa = new SlipSpawn(0, glm::vec3(), glm::vec3());
		spa->model = &debugModels[0];

		float juas[3];
		IO::read(input, juas);
		spa->position = glm::make_vec3(juas);

		IO::read(input, juas);
		spa->rotation = glm::make_vec3(juas);

		IO::read(input, spa->team);

		spawns.push_back(spa);
	}

	size_t entitieCount;
	IO::read(input, entitieCount);

	for (int i = 0; i < entitieCount; i++)
	{
		glm::vec3 pos, rot, sca;

		float juas[3];
		IO::read(input, juas);
		pos = glm::vec3(juas[0], juas[1], juas[2]);

		IO::read(input, juas);
		rot = glm::vec3(juas[0], juas[1], juas[2]);

		IO::read(input, juas);
		sca = glm::vec3(juas[0], juas[1], juas[2]);

		int model;
		IO::read(input, model);

		SlipActor* act = new SlipActor();
		strcpy(act->modelPath, models[model]->path.c_str());

		act->position = pos;
		act->rotation = rot;
		act->scale = sca;
		act->model = models[model];

		entities.push_back(act);
	}

	//IO::read(input, terrain->heightfieldIMG);
	//IO::read(input, terrain->collisionPath);

	//terrain->apply();

	input.close();

#ifdef SLIP_EDITOR
	SlipEditor::Get().print("Loaded level: " + path);
#endif

	canDraw = true;

	return 1;
}*/

void SlipLevel::apply()
{
	canDraw = false;

	//entities.push_back(m_Camera);

	this->path = path;

	if (std::strlen(bspPath) != 0)
	{
		bsp = new SlipBsp(bspPath);
	}

	terrain->apply();

	canDraw = true;
}

void SlipLevel::scripts()
{
	if (bsp != nullptr)
	{
		if (!bsp->initialized)
		{
			bsp->initCol();
			SlipPhysics::Get().getWorld().addRigidBody(bsp->coll.rigidBody);

			for (int n = 0; n < models.size(); n++)
			{
				std::string tempResult = std::string(bsp->skyboxPath);
				if (models[n]->path == tempResult)
					bsp->init(*models[n]);
			}
		}
	}

	for (int e = 0; e < entities.size(); e++)
	{
		entities[e]->ejecStart();
		entities[e]->Update();
	}

#ifdef SLIP_EDITOR
	if (SlipEditor::Get().isPlayMode()) {
#endif
		SlipPhysics::Get().update();
#ifdef SLIP_EDITOR
	}
#endif
}

void SlipLevel::draw()
{
	if (canDraw)
	{
		if (bsp != nullptr)
		{
			bsp->draw();

			SlipPhysics::Get().getWorld().debugDrawObject(bsp->coll.rigidBody->getWorldTransform(), bsp->coll.collisionShape, btVector3(0.f, 1.f, 0.f));
		}

		if (terrain->isInitialized())
		{
			terrain->draw();

			//SlipPhysics::Get().getWorld().debugDrawObject(terrain->collision->rigidBody->getWorldTransform(), terrain->collision->collisionShape, btVector3(0.f, 1.f, 0.f));
			//SlipPhysics::Get().getWorld().debugDrawObject(terrain->rigidBody->getWorldTransform(), terrain->heightfield, btVector3(0.f, 1.f, 0.f));
		}

		for (int e = 0; e < entities.size(); e++)
		{
			if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
			{
				p->draw();

				if (p->model->collision != nullptr)
				{
					btTransform tempTrans{ btQuaternion(p->rotation.x, p->rotation.y, p->rotation.z), btVector3(p->position.x, p->position.y, p->position.z) };
					SlipPhysics::Get().getWorld().debugDrawObject(p->model->collision->rigidBody->getWorldTransform(), p->model->collision->collisionShape, btVector3(1.f, 0.f, 0.f));
				}
			}
		}

		for (int e = 0; e < spawns.size(); e++)
		{
			if (SlipSpawn* p = dynamic_cast<SlipSpawn*>(spawns[e]))
			{
				p->model = &debugModels[0];
				p->draw();
			}
		}

		SlipDebug::Get().draw();
		SlipPhysics::Get().debugDraw();

		/*if (!playing)
		{
			
		}
		else
		{*/
			/*if (!mBsp->initialized)
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

			dynamicsWorld->stepSimulation(SlipGlobals::Get().GetDeltaTime());*/
		//}
	}
}

void SlipLevel::clean()
{
	if (bsp != nullptr)
	{
		bsp->clean();

		delete bsp;

		bsp = nullptr;
	}

	for (int e = 0; e < entities.size(); e++)
	{
		/*if (SlipBsp* b = dynamic_cast<SlipBsp*>(entities[e]))
		{
			b->clean();

			delete b;
		}*/

		if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
		{
			p->model->clean();

			delete p;
		}
	}

	if (terrain != nullptr)
	{
		terrain->clean();

		//delete terrain;
	}

	//SlipDebug::Get().clean();
}

int SlipLevel::searchModel(std::string path)
{
	for (int i = 0; i < models.size(); i++)
	{
		std::filesystem::path root(models[i]->path);
		std::string loc = root.parent_path().string() + "/" + root.stem().string();

		if (path == loc)
			return i;
	}
	return 9999;
}

SlipLevel::SlipLevel()
{
	assert(!m_Instance && "Level has initialized...");
	m_Instance = this;

	terrain = new SlipTerrain();

	SlipMesh spawnModel{ "models/debug/spawn/spawn" };
	spawnModel.init();
	debugModels.push_back(spawnModel);
}