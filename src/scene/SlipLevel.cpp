#include "SlipLevel.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <filesystem>
#ifdef _WIN32
#include <shlwapi.h>
#endif

#include "SlipPhysics.h"
#include "SlipGlobals.h"
#include "SlipActor.h"
#include "SlipID.h"
#include "SlipDebug.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipComponent.h"
#include "SlipTerrainComponent.h"

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

	if (std::strlen(bspPath) != 0 && bsp == nullptr)
	{
		bsp = new SlipBsp(bspPath);
	}

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

	if (SlipPhysics::IsInitialized() && SlipPhysics::Get().IsSimulating()) {
		SlipPhysics::Get().update();
		for (size_t i = 0; i < entities.size(); ++i) {
			if (entities[i]) {
				if (auto col = entities[i]->GetComponent<ColliderComponent>()) {
					col->SyncTransformFromEntity();
				}
				if (auto rb2d = entities[i]->GetComponent<RigidBody2DComponent>()) {
					if (rb2d->bodyType == BodyType2D::Dynamic) {
						rb2d->SyncTransformFromPhysics();
					}
				}
			}
		}
	}
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

void SlipLevel::ClearLevel(bool preservePersistent)
{
	std::vector<SlipEntity*> survivors;
	if (preservePersistent) {
		for (SlipEntity* ent : entities) {
			if (ent && ent->IsEffectivelyPersistent()) {
				survivors.push_back(ent);
			} else {
				delete ent;
			}
		}
	} else {
		for (SlipEntity* ent : entities) {
			delete ent;
		}
	}
	entities = survivors;
}

bool SlipLevel::ProcessPendingSceneLoad()
{
	if (!m_HasPendingSceneLoad) return false;
	std::string target = m_PendingScenePath;
	m_PendingScenePath.clear();
	m_HasPendingSceneLoad = false;
	return LoadScene(target, true);
}

std::string SlipLevel::GetCurrentSceneName() const
{
	if (path.empty()) return "Untitled";
	return SlipFileSystem::GetFileNameWithoutExtension(path);
}

void SlipLevel::clean()
{
	if (bsp != nullptr)
	{
		bsp->clean();

		delete bsp;

		bsp = nullptr;
	}

	for (size_t e = 0; e < entities.size(); e++)
	{
		if (SlipActor* p = dynamic_cast<SlipActor*>(entities[e]))
		{
			if (p->model) p->model->clean();
		}
		delete entities[e];
	}
	entities.clear();
	//SlipDebug::Get().clean();
}

bool SlipLevel::SaveScene(const std::string& filePath)
{
	std::string targetPath = filePath.empty() ? this->path : filePath;
	if (targetPath.empty()) return false;

	std::stringstream ss;
	ss << "# SlipEngine Scene File\n";
	ss << "Version: 2\n";
	if (!lightmapPath.empty()) {
		ss << "LightmapAsset: " << lightmapPath << "\n";
	}
	ss << "EntitiesCount: " << entities.size() << "\n\n";

	for (SlipEntity* ent : entities)
	{
		if (!ent) continue;

		glm::vec3 entPos = ent->position;
		glm::vec3 entRot = ent->rotation;
		glm::vec3 entScale = ent->scale;
		if (auto tc = ent->GetComponent<TransformComponent>()) {
			entPos = tc->position;
			entRot = tc->rotation;
			entScale = tc->scale;
		}

		ss << "Entity:\n";
		ss << "  Id: " << ent->GetId() << "\n";
		ss << "  Name: " << ent->GetName() << "\n";
		ss << "  Static: " << (ent->IsStatic() ? "true" : "false") << "\n";
		ss << "  Position: " << entPos.x << " " << entPos.y << " " << entPos.z << "\n";
		ss << "  Rotation: " << entRot.x << " " << entRot.y << " " << entRot.z << "\n";
		ss << "  Scale: " << entScale.x << " " << entScale.y << " " << entScale.z << "\n";
		if (auto tc = ent->GetComponent<TransformComponent>()) {
			if (!tc->inheritScale) {
				ss << "  InheritScale: 0\n";
			}
		}

		if (ent->GetParent()) {
			ss << "  ParentId: " << ent->GetParent()->GetId() << "\n";
			ss << "  Parent: " << ent->GetParent()->GetName() << "\n";
		}

		// MeshComponent
		if (auto meshComp = ent->GetComponent<MeshComponent>()) {
			ss << "  MeshComponent:\n";
			ss << "    ModelPath: " << meshComp->modelPath << "\n";
			std::string modelUid = SlipFileSystem::GetGuidForAsset(meshComp->modelPath);
			if (!modelUid.empty()) {
				ss << "    ModelUID: " << modelUid << "\n";
			}
			if (meshComp->material && meshComp->materialPath.empty()) {
				std::string mDir = SlipFileSystem::CombinePath(SlipFileSystem::GetProjectAssetsPath(), "materials");
				SlipFileSystem::CreateDirectoryPath(mDir);
				meshComp->materialPath = SlipFileSystem::CombinePath(mDir, ent->GetName() + "_Mat.mat");
			}
			ss << "    MaterialPath: " << meshComp->materialPath << "\n";
			std::string matUid = SlipFileSystem::GetGuidForAsset(meshComp->materialPath);
			if (!matUid.empty()) {
				ss << "    MaterialUID: " << matUid << "\n";
			}
			if (meshComp->material && !meshComp->materialPath.empty()) {
				meshComp->material->Save(meshComp->materialPath);
			}
			for (size_t sIdx = 0; sIdx < meshComp->GetMaterialSlotCount(); ++sIdx) {
				ss << "    SubMaterial: " << sIdx << " " << meshComp->GetSubmeshMaterialPath(sIdx) << "\n";
				if (auto subMat = meshComp->GetSubmeshMaterial(sIdx)) {
					std::string smPath = meshComp->GetSubmeshMaterialPath(sIdx);
					if (!smPath.empty()) {
						subMat->Save(smPath);
					}
				}
			}
			if (meshComp->lightmapIndex >= 0) {
				ss << "    LightmapIndex: " << meshComp->lightmapIndex << "\n";
				ss << "    LightmapScaleOffset: " << meshComp->lightmapScaleOffset.x << " " << meshComp->lightmapScaleOffset.y << " " << meshComp->lightmapScaleOffset.z << " " << meshComp->lightmapScaleOffset.w << "\n";
			}
		}

		// SkinnedMeshComponent
		if (auto skinnedComp = ent->GetComponent<SkinnedMeshComponent>()) {
			ss << "  SkinnedMeshComponent:\n";
			ss << "    ModelPath: " << skinnedComp->modelPath << "\n";
			std::string modelUid = SlipFileSystem::GetGuidForAsset(skinnedComp->modelPath);
			if (!modelUid.empty()) {
				ss << "    ModelUID: " << modelUid << "\n";
			}
			ss << "    MaterialPath: " << skinnedComp->materialPath << "\n";
			std::string matUid = SlipFileSystem::GetGuidForAsset(skinnedComp->materialPath);
			if (!matUid.empty()) {
				ss << "    MaterialUID: " << matUid << "\n";
			}
			ss << "    CastShadows: " << (skinnedComp->castShadows ? "1" : "0") << "\n";
			for (size_t sIdx = 0; sIdx < skinnedComp->GetMaterialSlotCount(); ++sIdx) {
				ss << "    SubMaterial: " << sIdx << " " << skinnedComp->GetSubmeshMaterialPath(sIdx) << "\n";
			}
		}

		// AnimatorComponent
		if (auto animComp = ent->GetComponent<AnimatorComponent>()) {
			ss << "  AnimatorComponent:\n";
			ss << "    CurrentClipIndex: " << animComp->currentClipIndex << "\n";
			AnimationClip* curClip = animComp->GetCurrentClip();
			ss << "    CurrentClipName: " << (curClip ? curClip->name : "") << "\n";
			ss << "    PlaybackSpeed: " << animComp->playbackSpeed << "\n";
			ss << "    IsPlaying: " << (animComp->isPlaying ? "1" : "0") << "\n";
			ss << "    IsLooping: " << (animComp->isLooping ? "1" : "0") << "\n";
			ss << "    CurrentTime: " << animComp->currentTime << "\n";
			ss << "    ControllerPath: " << animComp->controllerPath << "\n";
			if (animComp->controller && !animComp->controllerPath.empty()) {
				animComp->controller->SaveToFile(animComp->controllerPath);
			}
		}

		// BoneComponent
		if (auto boneComp = ent->GetComponent<BoneComponent>()) {
			ss << "  BoneComponent:\n";
			ss << "    BoneIndex: " << boneComp->boneIndex << "\n";
			ss << "    BoneName: " << boneComp->boneName << "\n";
		}

		// LightComponent
		if (auto lightComp = ent->GetComponent<LightComponent>()) {
			ss << "  LightComponent:\n";
			std::string lTypeStr = "Point";
			if (lightComp->type == LightType::Directional) lTypeStr = "Directional";
			else if (lightComp->type == LightType::Spot) lTypeStr = "Spot";
			ss << "    Type: " << lTypeStr << "\n";
			std::string lMobStr = "Dynamic";
			if (lightComp->mobility == LightMobility::Static) lMobStr = "Static";
			else if (lightComp->mobility == LightMobility::Mixed) lMobStr = "Mixed";
			ss << "    Mobility: " << lMobStr << "\n";
			ss << "    Color: " << lightComp->color.r << " " << lightComp->color.g << " " << lightComp->color.b << "\n";
			ss << "    Intensity: " << lightComp->intensity << "\n";
			ss << "    Range: " << lightComp->range << "\n";
			ss << "    SpotAngle: " << lightComp->spotAngle << "\n";
			ss << "    CastShadows: " << (lightComp->castShadows ? "1" : "0") << "\n";
		}

		if (auto ddgi = ent->GetComponent<DDGIVolumeComponent>()) {
			ss << "  DDGIVolumeComponent:\n";
			ss << "    Size: " << ddgi->size.x << " " << ddgi->size.y << " " << ddgi->size.z << "\n";
			ss << "    ProbesX: " << ddgi->probesX << "\n";
			ss << "    ProbesY: " << ddgi->probesY << "\n";
			ss << "    ProbesZ: " << ddgi->probesZ << "\n";
			ss << "    Preview: " << (ddgi->previewProbes ? 1 : 0) << "\n";
		}

		if (auto probe = ent->GetComponent<ReflectionProbeComponent>()) {
			ss << "  ReflectionProbeComponent:\n";
			ss << "    Size: " << probe->size.x << " " << probe->size.y << " " << probe->size.z << "\n";
			ss << "    Resolution: " << probe->resolution << "\n";
			ss << "    Intensity: " << probe->intensity << "\n";
			ss << "    RoughnessCutoff: " << probe->roughnessCutoff << "\n";
			ss << "    BoxProjection: " << (probe->boxProjection ? 1 : 0) << "\n";
			ss << "    IncludeDynamic: " << (probe->includeDynamic ? 1 : 0) << "\n";
		}

		if (auto post = ent->GetComponent<PostProcessVolumeComponent>()) {
			post->SerializeTo(ss);
		}

		if (auto sky = ent->GetComponent<SlipSkyComponent>()) {
			sky->SerializeTo(ss);
		}

		// SlipTerrainComponent
		if (auto terrainComp = ent->GetComponent<SlipTerrainComponent>()) {
			ss << "  SlipTerrainComponent:\n";
			ss << "    Width: " << terrainComp->width << "\n";
			ss << "    Length: " << terrainComp->length << "\n";
			ss << "    ResolutionX: " << terrainComp->resolutionX << "\n";
			ss << "    ResolutionZ: " << terrainComp->resolutionZ << "\n";
			ss << "    HeightScale: " << terrainComp->heightScale << "\n";
			ss << "    UVScale: " << terrainComp->uvScale.x << " " << terrainComp->uvScale.y << "\n";
			ss << "    HeightmapPath: " << terrainComp->heightmapPath << "\n";
			ss << "    MaterialPath: " << terrainComp->materialPath << "\n";
			ss << "    EnableCollision: " << (terrainComp->enableCollision ? 1 : 0) << "\n";
			ss << "    Wireframe: " << (terrainComp->wireframe ? 1 : 0) << "\n";
			ss << "    CastShadows: " << (terrainComp->castShadows ? 1 : 0) << "\n";
		}

		// BoxColliderComponent
		if (auto boxCol = ent->GetComponent<BoxColliderComponent>()) {
			ss << "  BoxColliderComponent:\n";
			ss << "    Size: " << boxCol->size.x << " " << boxCol->size.y << " " << boxCol->size.z << "\n";
			ss << "    CenterOffset: " << boxCol->centerOffset.x << " " << boxCol->centerOffset.y << " " << boxCol->centerOffset.z << "\n";
			ss << "    IsTrigger: " << (boxCol->isTrigger ? 1 : 0) << "\n";
		}

		// SphereColliderComponent
		if (auto sphereCol = ent->GetComponent<SphereColliderComponent>()) {
			ss << "  SphereColliderComponent:\n";
			ss << "    Radius: " << sphereCol->radius << "\n";
			ss << "    CenterOffset: " << sphereCol->centerOffset.x << " " << sphereCol->centerOffset.y << " " << sphereCol->centerOffset.z << "\n";
			ss << "    IsTrigger: " << (sphereCol->isTrigger ? 1 : 0) << "\n";
		}

		// CapsuleColliderComponent
		if (auto capCol = ent->GetComponent<CapsuleColliderComponent>()) {
			ss << "  CapsuleColliderComponent:\n";
			ss << "    Radius: " << capCol->radius << "\n";
			ss << "    Height: " << capCol->height << "\n";
			ss << "    CenterOffset: " << capCol->centerOffset.x << " " << capCol->centerOffset.y << " " << capCol->centerOffset.z << "\n";
			ss << "    IsTrigger: " << (capCol->isTrigger ? 1 : 0) << "\n";
		}

		// MeshColliderComponent
		if (auto meshCol = ent->GetComponent<MeshColliderComponent>()) {
			ss << "  MeshColliderComponent:\n";
			ss << "    IsConvex: " << (meshCol->isConvex ? 1 : 0) << "\n";
			ss << "    CenterOffset: " << meshCol->centerOffset.x << " " << meshCol->centerOffset.y << " " << meshCol->centerOffset.z << "\n";
			ss << "    IsTrigger: " << (meshCol->isTrigger ? 1 : 0) << "\n";
		}

		// RigidBodyComponent
		if (auto rbComp = ent->GetComponent<RigidBodyComponent>()) {
			ss << "  RigidBodyComponent:\n";
			ss << "    Mass: " << rbComp->mass << "\n";
			ss << "    LinearDrag: " << rbComp->linearDrag << "\n";
			ss << "    AngularDrag: " << rbComp->angularDrag << "\n";
			ss << "    IsKinematic: " << (rbComp->isKinematic ? 1 : 0) << "\n";
			ss << "    UseGravity: " << (rbComp->useGravity ? 1 : 0) << "\n";
			ss << "    Friction: " << rbComp->friction << "\n";
			ss << "    RollingFriction: " << rbComp->rollingFriction << "\n";
			ss << "    Restitution: " << rbComp->restitution << "\n";
			ss << "    FreezePosition: " << (rbComp->freezePositionX ? 1 : 0) << " " << (rbComp->freezePositionY ? 1 : 0) << " " << (rbComp->freezePositionZ ? 1 : 0) << "\n";
			ss << "    FreezeRotation: " << (rbComp->freezeRotationX ? 1 : 0) << " " << (rbComp->freezeRotationY ? 1 : 0) << " " << (rbComp->freezeRotationZ ? 1 : 0) << "\n";
		}

		// VehicleComponent
		if (auto vehComp = ent->GetComponent<VehicleComponent>()) {
			ss << "  VehicleComponent:\n";
			ss << "    SuspensionStiffness: " << vehComp->suspensionStiffness << "\n";
			ss << "    SuspensionDamping: " << vehComp->suspensionDamping << "\n";
			ss << "    SuspensionCompression: " << vehComp->suspensionCompression << "\n";
			ss << "    MaxSuspensionTravelCm: " << vehComp->maxSuspensionTravelCm << "\n";
			ss << "    FrictionSlip: " << vehComp->frictionSlip << "\n";
			ss << "    MaxSuspensionForce: " << vehComp->maxSuspensionForce << "\n";
			ss << "    RollInfluence: " << vehComp->rollInfluence << "\n";
			ss << "    ChassisDimensions: " << vehComp->chassisWidth << " " << vehComp->chassisHeight << " " << vehComp->chassisLength << " " << vehComp->defaultWheelRadius << "\n";
		}

		// ScriptComponents (soporte de múltiples scripts y variables públicas)
		for (auto scriptComp : ent->GetScripts()) {
			if (!scriptComp) continue;
			ss << "  ScriptComponent:\n";
			ss << "    ScriptPath: " << scriptComp->scriptPath << "\n";
			ss << "    Enabled: " << (scriptComp->IsEnabled() ? 1 : 0) << "\n";
			for (const auto& prop : scriptComp->GetProperties()) {
				ss << "    Property: " << prop.name << " ";
				switch (prop.type) {
					case ScriptPropertyType::Float: ss << "float " << prop.floatValue << "\n"; break;
					case ScriptPropertyType::Int: ss << "int " << prop.intValue << "\n"; break;
					case ScriptPropertyType::Bool: ss << "bool " << (prop.boolValue ? 1 : 0) << "\n"; break;
					case ScriptPropertyType::String: ss << "string " << prop.stringValue << "\n"; break;
					case ScriptPropertyType::Vec3: ss << "vec3 " << prop.vec3Value.x << " " << prop.vec3Value.y << " " << prop.vec3Value.z << "\n"; break;
				}
			}
		}

		// CameraComponent
		if (auto camComp = ent->GetComponent<CameraComponent>()) {
			ss << "  CameraComponent:\n";
			ss << "    Projection: " << (camComp->projectionType == CameraProjection::Orthographic ? "Orthographic" : "Perspective") << "\n";
			ss << "    FOV: " << camComp->fov << "\n";
			ss << "    NearClip: " << camComp->nearClip << "\n";
			ss << "    FarClip: " << camComp->farClip << "\n";
			ss << "    OrthoSize: " << camComp->orthoSize << "\n";
			ss << "    IsPrimary: " << (camComp->isPrimary ? 1 : 0) << "\n";
			ss << "    ClearColor: " << camComp->clearColor.r << " " << camComp->clearColor.g << " " << camComp->clearColor.b << " " << camComp->clearColor.a << "\n";
			ss << "    Letterbox: " << camComp->letterbox << "\n";
			ss << "    Pillarbox: " << camComp->pillarbox << "\n";
		}

		// CinematicCameraComponent
		if (auto ccamComp = ent->GetComponent<CinematicCameraComponent>()) {
			ss << "  CinematicCameraComponent:\n";
			ss << "    Target: " << ccamComp->targetEntityName << "\n";
			ss << "    Distance: " << ccamComp->targetDistance << "\n";
			ss << "    Height: " << ccamComp->targetHeight << "\n";
			ss << "    Pitch: " << ccamComp->targetPitch << "\n";
			ss << "    PositionLag: " << ccamComp->positionLag << "\n";
			ss << "    RotationLag: " << ccamComp->rotationLag << "\n";
			ss << "    DriftCompensation: " << ccamComp->driftCompensation << "\n";
			ss << "    DynamicFOV: " << (ccamComp->enableDynamicFOV ? 1 : 0) << "\n";
			ss << "    BaseFOV: " << ccamComp->baseFOV << "\n";
			ss << "    BoostFOV: " << ccamComp->boostFOV << "\n";
		}

		// KeyframeAnimationComponent
		if (auto kfaComp = ent->GetComponent<KeyframeAnimationComponent>()) {
			ss << "  KeyframeAnimationComponent:\n";
			ss << "    ClipPath: " << kfaComp->clipPath << "\n";
			ss << "    PlayOnStart: " << (kfaComp->playOnStart ? 1 : 0) << "\n";
			ss << "    Loop: " << (kfaComp->loop ? 1 : 0) << "\n";
			ss << "    Speed: " << kfaComp->playbackSpeed << "\n";
		}

		// SpriteRendererComponent
		if (auto spriteComp = ent->GetComponent<SpriteRendererComponent>()) {
			ss << "  SpriteRendererComponent:\n";
			ss << "    TexturePath: " << spriteComp->texturePath << "\n";
			ss << "    Color: " << spriteComp->color.r << " " << spriteComp->color.g << " " << spriteComp->color.b << " " << spriteComp->color.a << "\n";
			ss << "    FlipX: " << (spriteComp->flipX ? 1 : 0) << "\n";
			ss << "    FlipY: " << (spriteComp->flipY ? 1 : 0) << "\n";
			ss << "    SortingOrder: " << spriteComp->sortingOrder << "\n";
			ss << "    SortingLayer: " << spriteComp->sortingLayerName << "\n";
			ss << "    PixelsPerUnit: " << spriteComp->pixelsPerUnit << "\n";
			ss << "    HFrames: " << spriteComp->hFrames << "\n";
			ss << "    VFrames: " << spriteComp->vFrames << "\n";
			ss << "    CurrentFrame: " << spriteComp->currentFrame << "\n";
			ss << "    IsLit: " << (spriteComp->isLit ? 1 : 0) << "\n";
			ss << "    IsPixelArt: " << (spriteComp->isPixelArt ? 1 : 0) << "\n";
			ss << "    NormalMapPath: " << spriteComp->normalMapPath << "\n";
		}

		// SpriteAnimatorComponent
		if (auto saComp = ent->GetComponent<SpriteAnimatorComponent>()) {
			ss << "  SpriteAnimatorComponent:\n";
			ss << "    PlayOnStart: " << (saComp->playOnStart ? 1 : 0) << "\n";
			ss << "    DefaultClip: " << saComp->defaultClip << "\n";
			ss << "    ClipCount: " << saComp->clips.size() << "\n";
			for (size_t ci = 0; ci < saComp->clips.size(); ++ci) {
				const auto& c = saComp->clips[ci];
				ss << "    ClipData: " << c.name << " " << c.startFrame << " " << c.endFrame << " " << c.frameRate << " " << (c.loop ? 1 : 0) << "\n";
			}
		}

		// BoxCollider2DComponent
		if (auto box2d = ent->GetComponent<BoxCollider2DComponent>()) {
			ss << "  BoxCollider2DComponent:\n";
			ss << "    Size: " << box2d->size.x << " " << box2d->size.y << "\n";
			ss << "    Offset: " << box2d->offset.x << " " << box2d->offset.y << "\n";
			ss << "    IsTrigger: " << (box2d->isTrigger ? 1 : 0) << "\n";
			ss << "    Density: " << box2d->density << "\n";
			ss << "    Friction: " << box2d->friction << "\n";
			ss << "    Restitution: " << box2d->restitution << "\n";
		}

		// CircleCollider2DComponent
		if (auto circ2d = ent->GetComponent<CircleCollider2DComponent>()) {
			ss << "  CircleCollider2DComponent:\n";
			ss << "    Radius: " << circ2d->radius << "\n";
			ss << "    Offset: " << circ2d->offset.x << " " << circ2d->offset.y << "\n";
			ss << "    IsTrigger: " << (circ2d->isTrigger ? 1 : 0) << "\n";
			ss << "    Density: " << circ2d->density << "\n";
			ss << "    Friction: " << circ2d->friction << "\n";
			ss << "    Restitution: " << circ2d->restitution << "\n";
		}

		// RigidBody2DComponent
		if (auto rb2d = ent->GetComponent<RigidBody2DComponent>()) {
			ss << "  RigidBody2DComponent:\n";
			ss << "    BodyType: " << static_cast<int>(rb2d->bodyType) << "\n";
			ss << "    Mass: " << rb2d->mass << "\n";
			ss << "    GravityScale: " << rb2d->gravityScale << "\n";
			ss << "    FreezeRotation: " << (rb2d->freezeRotation ? 1 : 0) << "\n";
			ss << "    LinearDamping: " << rb2d->linearDamping << "\n";
			ss << "    AngularDamping: " << rb2d->angularDamping << "\n";
		}

		// Light2DComponent
		if (auto l2d = ent->GetComponent<Light2DComponent>()) {
			ss << "  Light2DComponent:\n";
			ss << "    LightType: " << static_cast<int>(l2d->lightType) << "\n";
			ss << "    Color: " << l2d->color.r << " " << l2d->color.g << " " << l2d->color.b << "\n";
			ss << "    Intensity: " << l2d->intensity << "\n";
			ss << "    Radius: " << l2d->radius << "\n";
			ss << "    InnerRadius: " << l2d->innerRadius << "\n";
			ss << "    SpotAngle: " << l2d->spotAngle << "\n";
			ss << "    CastShadows: " << (l2d->castShadows ? 1 : 0) << "\n";
			ss << "    ShadowIntensity: " << l2d->shadowIntensity << "\n";
		}

		// ShadowCaster2DComponent
		if (auto sc2d = ent->GetComponent<ShadowCaster2DComponent>()) {
			ss << "  ShadowCaster2DComponent:\n";
			ss << "    Shape: " << static_cast<int>(sc2d->shape) << "\n";
			ss << "    Size: " << sc2d->size.x << " " << sc2d->size.y << "\n";
			ss << "    Radius: " << sc2d->radius << "\n";
			ss << "    Offset: " << sc2d->offset.x << " " << sc2d->offset.y << "\n";
			ss << "    CastShadows: " << (sc2d->castShadows ? 1 : 0) << "\n";
		}

		// TileGridComponent
		if (auto tgComp = ent->GetComponent<TileGridComponent>()) {
			ss << "  TileGridComponent:\n";
			ss << "    CellSize: " << tgComp->cellSize.x << " " << tgComp->cellSize.y << "\n";
			ss << "    Anchor: " << static_cast<int>(tgComp->anchor) << "\n";
			ss << "    TileSheetPath: " << tgComp->tileSheetPath << "\n";
			ss << "    SheetHFrames: " << tgComp->sheetHFrames << "\n";
			ss << "    SheetVFrames: " << tgComp->sheetVFrames << "\n";
			ss << "    SortingLayer: " << tgComp->sortingLayerName << "\n";
			ss << "    SortingOrder: " << tgComp->orderInLayer << "\n";
			ss << "    IsPixelArt: " << (tgComp->isPixelArt ? 1 : 0) << "\n";
			ss << "    TileCount: " << tgComp->tiles.size() << "\n";
			for (const auto& kv : tgComp->tiles) {
				ss << "    TileData: " << kv.first.first << " " << kv.first.second << " " << kv.second << "\n";
			}
		}

		// CanvasComponent
		if (auto canvasComp = ent->GetComponent<CanvasComponent>()) {
			ss << "  CanvasComponent:\n";
			ss << "    RenderMode: " << (canvasComp->renderMode == CanvasRenderMode::WorldSpace ? "WorldSpace" : "ScreenSpaceOverlay") << "\n";
			ss << "    ReferenceResolution: " << canvasComp->referenceResolution.x << " " << canvasComp->referenceResolution.y << "\n";
			ss << "    ScaleWithScreenSize: " << (canvasComp->scaleWithScreenSize ? 1 : 0) << "\n";
			ss << "    SortingOrder: " << canvasComp->sortingOrder << "\n";
		}

		// RectTransformComponent
		if (auto rectComp = ent->GetComponent<RectTransformComponent>()) {
			ss << "  RectTransformComponent:\n";
			ss << "    AnchorPreset: " << rectComp->GetAnchorPresetName() << "\n";
			ss << "    AnchoredPosition: " << rectComp->anchoredPosition.x << " " << rectComp->anchoredPosition.y << "\n";
			ss << "    Size: " << rectComp->size.x << " " << rectComp->size.y << "\n";
			ss << "    Pivot: " << rectComp->pivot.x << " " << rectComp->pivot.y << "\n";
		}

		// UIImageComponent
		if (auto uiImg = ent->GetComponent<UIImageComponent>()) {
			ss << "  UIImageComponent:\n";
			ss << "    TexturePath: " << uiImg->texturePath << "\n";
			ss << "    Color: " << uiImg->color.r << " " << uiImg->color.g << " " << uiImg->color.b << " " << uiImg->color.a << "\n";
			ss << "    PreserveAspect: " << (uiImg->preserveAspect ? 1 : 0) << "\n";
		}

		// UITextComponent
		if (auto uiTxt = ent->GetComponent<UITextComponent>()) {
			ss << "  UITextComponent:\n";
			ss << "    Text: " << uiTxt->text << "\n";
			ss << "    FontPath: " << uiTxt->fontPath << "\n";
			ss << "    FontSize: " << uiTxt->fontSize << "\n";
			ss << "    Color: " << uiTxt->color.r << " " << uiTxt->color.g << " " << uiTxt->color.b << " " << uiTxt->color.a << "\n";
			ss << "    AlignH: " << static_cast<int>(uiTxt->alignH) << "\n";
			ss << "    AlignV: " << static_cast<int>(uiTxt->alignV) << "\n";
		}

		// UIButtonComponent
		if (auto uiBtn = ent->GetComponent<UIButtonComponent>()) {
			ss << "  UIButtonComponent:\n";
			ss << "    Interactable: " << (uiBtn->interactable ? 1 : 0) << "\n";
			ss << "    NormalColor: " << uiBtn->normalColor.r << " " << uiBtn->normalColor.g << " " << uiBtn->normalColor.b << " " << uiBtn->normalColor.a << "\n";
			ss << "    HoverColor: " << uiBtn->hoverColor.r << " " << uiBtn->hoverColor.g << " " << uiBtn->hoverColor.b << " " << uiBtn->hoverColor.a << "\n";
			ss << "    PressedColor: " << uiBtn->pressedColor.r << " " << uiBtn->pressedColor.g << " " << uiBtn->pressedColor.b << " " << uiBtn->pressedColor.a << "\n";
			ss << "    DisabledColor: " << uiBtn->disabledColor.r << " " << uiBtn->disabledColor.g << " " << uiBtn->disabledColor.b << " " << uiBtn->disabledColor.a << "\n";
			ss << "    TargetEntity: " << uiBtn->targetEntityName << "\n";
			ss << "    OnClickFunction: " << uiBtn->onClickFunction << "\n";
		}

		// UISliderComponent
		if (auto uiSld = ent->GetComponent<UISliderComponent>()) {
			ss << "  UISliderComponent:\n";
			ss << "    Interactable: " << (uiSld->interactable ? 1 : 0) << "\n";
			ss << "    MinValue: " << uiSld->minValue << "\n";
			ss << "    MaxValue: " << uiSld->maxValue << "\n";
			ss << "    Value: " << uiSld->value << "\n";
			ss << "    WholeNumbers: " << (uiSld->wholeNumbers ? 1 : 0) << "\n";
			ss << "    BackgroundColor: " << uiSld->backgroundColor.r << " " << uiSld->backgroundColor.g << " " << uiSld->backgroundColor.b << " " << uiSld->backgroundColor.a << "\n";
			ss << "    FillColor: " << uiSld->fillColor.r << " " << uiSld->fillColor.g << " " << uiSld->fillColor.b << " " << uiSld->fillColor.a << "\n";
			ss << "    HandleColor: " << uiSld->handleColor.r << " " << uiSld->handleColor.g << " " << uiSld->handleColor.b << " " << uiSld->handleColor.a << "\n";
			ss << "    TargetEntity: " << uiSld->targetEntityName << "\n";
			ss << "    OnValueChangedFunction: " << uiSld->onValueChangedFunction << "\n";
		}

		// UIProgressBarComponent
		if (auto uiPb = ent->GetComponent<UIProgressBarComponent>()) {
			ss << "  UIProgressBarComponent:\n";
			ss << "    MinValue: " << uiPb->minValue << "\n";
			ss << "    MaxValue: " << uiPb->maxValue << "\n";
			ss << "    Value: " << uiPb->value << "\n";
			ss << "    BackgroundColor: " << uiPb->backgroundColor.r << " " << uiPb->backgroundColor.g << " " << uiPb->backgroundColor.b << " " << uiPb->backgroundColor.a << "\n";
			ss << "    FillColor: " << uiPb->fillColor.r << " " << uiPb->fillColor.g << " " << uiPb->fillColor.b << " " << uiPb->fillColor.a << "\n";
			ss << "    ShowPercentageText: " << (uiPb->showPercentageText ? 1 : 0) << "\n";
		}

		if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
			ss << "  UIJoystickComponent:\n";
			ss << "    StickName: " << joy->stickName << "\n";
			ss << "    Mode: " << static_cast<int>(joy->mode) << "\n";
			ss << "    TravelRadius: " << joy->travelRadius << "\n";
			ss << "    ActivationRadius: " << joy->activationRadius << "\n";
			ss << "    BackgroundColor: " << joy->backgroundColor.r << " " << joy->backgroundColor.g << " " << joy->backgroundColor.b << " " << joy->backgroundColor.a << "\n";
			ss << "    KnobColor: " << joy->knobColor.r << " " << joy->knobColor.g << " " << joy->knobColor.b << " " << joy->knobColor.a << "\n";
			ss << "    Interactable: " << (joy->interactable ? 1 : 0) << "\n";
		}

		if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
			ss << "  UIActionButtonComponent:\n";
			ss << "    ActionName: " << actionBtn->actionName << "\n";
			ss << "    Interactable: " << (actionBtn->interactable ? 1 : 0) << "\n";
			ss << "    NormalColor: " << actionBtn->normalColor.r << " " << actionBtn->normalColor.g << " " << actionBtn->normalColor.b << " " << actionBtn->normalColor.a << "\n";
			ss << "    PressedColor: " << actionBtn->pressedColor.r << " " << actionBtn->pressedColor.g << " " << actionBtn->pressedColor.b << " " << actionBtn->pressedColor.a << "\n";
		}

		// ParticleSystemComponent
		if (auto ps = ent->GetComponent<ParticleSystemComponent>()) {
			ps->SerializeTo(ss);
		}

		ss << "\n";
	}

	bool ok = SlipFileSystem::WriteAllText(targetPath, ss.str());
	if (ok)
	{
		this->path = targetPath;
	}
	return ok;
}

bool SlipLevel::LoadScene(const std::string& filePath, bool preservePersistent)
{
	if (filePath.empty() || !SlipFileSystem::Exists(filePath)) return false;

	std::string content;
	if (!SlipFileSystem::ReadAllText(filePath, content)) return false;

	ClearLevel(preservePersistent);

	this->path = filePath;

	std::stringstream ss(content);
	std::string line;

	struct EntityData {
		uint32_t id = 0;
		uint32_t parentId = 0;
		std::string name = "Entity";
		std::string type = "Entity";
		std::string primType = "";
		std::string parentName = "";
		glm::vec3 pos{ 0.0f };
		glm::vec3 rot{ 0.0f };
		glm::vec3 scale{ 1.0f };
		glm::vec4 color{ 0.75f, 0.78f, 0.82f, 1.0f };
		bool isStatic = false;
		bool wireframe = false;
		bool inheritScale = true;

		// Mesh
		bool hasMesh = false;
		std::string modelPath;
		std::string modelUid;
		std::string materialPath;
		std::string materialUid;
		std::vector<std::pair<size_t, std::string>> subMaterials;
		int lightmapIndex = -1;
		glm::vec4 lightmapScaleOffset{ 1.0f, 1.0f, 0.0f, 0.0f };

		// SkinnedMesh
		bool hasSkinned = false;
		std::string skinnedModelPath;
		std::string skinnedModelUid;
		std::string skinnedMatPath;
		std::string skinnedMatUid;
		bool skinnedCastShadows = true;
		std::vector<std::pair<size_t, std::string>> skinnedSubMaterials;

		// Animator
		bool hasAnimator = false;
		int animClipIdx = 0;
		std::string animClipName;
		float animSpeed = 1.0f;
		bool animPlaying = true;
		bool animLooping = true;
		float animTime = 0.0f;
		std::string animControllerPath;

		// Bone
		bool hasBone = false;
		int boneIdx = -1;
		std::string boneName;

		// Light
		bool hasLight = false;
		std::string lightType = "Point";
		std::string lightMobility = "Dynamic";
		glm::vec3 lightColor{ 1.0f };
		float lightIntensity = 1.0f;
		float lightRange = 10.0f;
		float lightSpotAngle = 30.0f;
		bool lightCastShadows = true;

		bool hasDDGI = false;
		glm::vec3 ddgiSize{ 24.0f, 12.0f, 24.0f };
		int ddgiProbesX = 8;
		int ddgiProbesY = 4;
		int ddgiProbesZ = 8;
		bool ddgiPreview = true;

		bool hasReflectionProbe = false;
		glm::vec3 probeSize{ 16.0f, 8.0f, 16.0f };
		int probeResolution = 256;
		float probeIntensity = 1.0f;
		float probeRoughnessCutoff = 0.5f;
		bool probeBoxProjection = true;
		bool probeIncludeDynamic = false;

		bool hasPost = false;
		std::vector<std::pair<std::string, std::string>> postKeys;

		bool hasSky = false;
		std::vector<std::pair<std::string, std::string>> skyKeys;

		// Terrain
		bool hasTerrain = false;
		float terrainWidth = 64.0f;
		float terrainLength = 64.0f;
		uint32_t terrainResX = 64;
		uint32_t terrainResZ = 64;
		float terrainHeightScale = 15.0f;
		glm::vec2 terrainUVScale = glm::vec2(8.0f, 8.0f);
		std::string terrainHeightmapPath;
		std::string terrainMaterialPath;
		bool terrainCollision = true;
		bool terrainWireframe = false;
		bool terrainCastShadows = false;

		// BoxCollider
		bool hasBoxCol = false;
		glm::vec3 boxSize{ 1.0f };
		glm::vec3 boxOffset{ 0.0f };
		bool boxTrigger = false;

		// SphereCollider
		bool hasSphereCol = false;
		float sphereRadius = 0.5f;
		glm::vec3 sphereOffset{ 0.0f };
		bool sphereTrigger = false;

		// CapsuleCollider
		bool hasCapCol = false;
		float capRadius = 0.5f;
		float capHeight = 1.0f;
		glm::vec3 capOffset{ 0.0f };
		bool capTrigger = false;

		// MeshCollider
		bool hasMeshCol = false;
		bool meshConvex = false;
		glm::vec3 meshOffset{ 0.0f };
		bool meshTrigger = false;

		// RigidBody
		bool hasRb = false;
		float rbMass = 1.0f;
		float rbLinearDrag = 0.0f;
		float rbAngularDrag = 0.05f;
		bool rbKinematic = false;
		bool rbGravity = true;
		float rbFriction = 0.5f;
		float rbRollingFriction = 0.0f;
		float rbRestitution = 0.0f;
		bool rbFreezePosX = false;
		bool rbFreezePosY = false;
		bool rbFreezePosZ = false;
		bool rbFreezeRotX = false;
		bool rbFreezeRotY = false;
		bool rbFreezeRotZ = false;

		// Vehicle
		bool hasVehicle = false;
		float vehStiffness = 20.0f;
		float vehDamping = 2.3f;
		float vehCompression = 4.4f;
		float vehMaxTravel = 500.0f;
		float vehFrictionSlip = 10.5f;
		float vehMaxForce = 6000.0f;
		float vehRollInfluence = 0.1f;
		float vehWidth = 1.8f;
		float vehHeight = 0.4f;
		float vehLength = 3.6f;
		float vehWheelRadius = 0.35f;

		// Script
		struct SerializedScript {
			std::string path;
			bool enabled = true;
			std::vector<ScriptProperty> properties;
		};
		bool hasScript = false;
		std::string scriptPath;
		bool scriptEnabled = true;
		std::vector<SerializedScript> scripts;

		// Camera
		bool hasCam = false;
		std::string camProj = "Perspective";
		float camFov = 60.0f;
		float camNear = 0.1f;
		float camFar = 1000.0f;
		float camOrtho = 5.0f;
		bool camPrimary = true;
		glm::vec4 camClearColor{ 0.12f, 0.14f, 0.18f, 1.0f };
		float camLetterbox = 0.0f;
		float camPillarbox = 0.0f;

		// CinematicCamera
		bool hasCinematicCam = false;
		std::string cinematicCamTarget = "";
		float cinematicCamDist = 5.8f;
		float cinematicCamHeight = 1.5f;
		float cinematicCamPitch = 9.0f;
		float cinematicCamPosLag = 8.5f;
		float cinematicCamRotLag = 6.5f;
		float cinematicCamDriftComp = 0.40f;
		bool cinematicCamDynamicFOV = true;
		float cinematicCamBaseFOV = 60.0f;
		float cinematicCamBoostFOV = 76.0f;

		// KeyframeAnimation
		bool hasKeyframeAnim = false;
		std::string keyframeClipPath;
		bool keyframePlayOnStart = true;
		bool keyframeLoop = false;
		float keyframeSpeed = 1.0f;

		// SpriteRenderer
		bool hasSprite = false;
		std::string spriteTexPath;
		glm::vec4 spriteColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool spriteFlipX = false;
		bool spriteFlipY = false;
		int spriteSortingOrder = 0;
		std::string spriteSortingLayer = "Default";
		float spritePPU = 100.0f;
		int spriteHFrames = 1;
		int spriteVFrames = 1;
		int spriteCurrentFrame = 0;
		bool spriteIsLit = false;
		bool spriteIsPixelArt = true;
		std::string spriteNormalMapPath;

		// SpriteAnimator
		bool hasSpriteAnimator = false;
		bool saPlayOnStart = true;
		std::string saDefaultClip;
		std::vector<SpriteAnimationClip> saClips;

		// BoxCollider2D
		bool hasBoxCollider2D = false;
		glm::vec2 box2dSize{ 1.0f, 1.0f };
		glm::vec2 box2dOffset{ 0.0f, 0.0f };
		bool box2dIsTrigger = false;
		float box2dDensity = 1.0f;
		float box2dFriction = 0.2f;
		float box2dRestitution = 0.0f;

		// CircleCollider2D
		bool hasCircleCollider2D = false;
		float circ2dRadius = 0.5f;
		glm::vec2 circ2dOffset{ 0.0f, 0.0f };
		bool circ2dIsTrigger = false;
		float circ2dDensity = 1.0f;
		float circ2dFriction = 0.2f;
		float circ2dRestitution = 0.0f;

		// RigidBody2D
		bool hasRigidBody2D = false;
		int rb2dBodyType = 0;
		float rb2dMass = 1.0f;
		float rb2dGravityScale = 1.0f;
		bool rb2dFreezeRotation = false;
		float rb2dLinearDamping = 0.0f;
		float rb2dAngularDamping = 0.05f;

		// Light2D
		bool hasLight2D = false;
		int l2dType = 0;
		glm::vec3 l2dColor{ 1.0f, 1.0f, 0.9f };
		float l2dIntensity = 1.0f;
		float l2dRadius = 5.0f;
		float l2dInnerRadius = 0.0f;
		float l2dSpotAngle = 45.0f;
		bool l2dCastShadows = true;
		float l2dShadowIntensity = 0.85f;

		// ShadowCaster2D
		bool hasShadowCaster2D = false;
		int sc2dShape = 0;
		glm::vec2 sc2dSize{ 1.0f, 1.0f };
		float sc2dRadius = 0.5f;
		glm::vec2 sc2dOffset{ 0.0f, 0.0f };
		bool sc2dCastShadows = true;

		// TileGrid
		bool hasTileGrid = false;
		glm::vec2 tgCellSize{ 1.0f, 1.0f };
		int tgAnchor = 0;
		std::string tgTileSheetPath;
		int tgSheetHFrames = 1;
		int tgSheetVFrames = 1;
		std::string tgSortingLayer = "Default";
		int tgSortingOrder = 0;
		bool tgIsPixelArt = true;
		std::map<std::pair<int, int>, int> tgTiles;

		// Canvas
		bool hasCanvas = false;
		std::string canvasRenderMode = "ScreenSpaceOverlay";
		glm::vec2 canvasRefRes{ 1920.0f, 1080.0f };
		bool canvasScaleWithScreen = true;
		int canvasSortingOrder = 0;

		// RectTransform
		bool hasRect = false;
		std::string rectAnchorPreset = "Center";
		glm::vec2 rectAnchoredPos{ 0.0f, 0.0f };
		glm::vec2 rectSize{ 160.0f, 50.0f };
		glm::vec2 rectPivot{ 0.5f, 0.5f };

		// UIImage
		bool hasUIImage = false;
		std::string uiImgTexPath;
		glm::vec4 uiImgColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool uiImgPreserveAspect = false;

		// UIText
		bool hasUIText = false;
		std::string uiTxtText = "Texto UI";
		std::string uiTxtFontPath;
		float uiTxtFontSize = 24.0f;
		glm::vec4 uiTxtColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		int uiTxtAlignH = 1;
		int uiTxtAlignV = 1;

		// UIButton
		bool hasUIButton = false;
		bool uiBtnInteractable = true;
		glm::vec4 uiBtnNormalCol{ 0.20f, 0.45f, 0.82f, 1.0f };
		glm::vec4 uiBtnHoverCol{ 0.28f, 0.56f, 0.95f, 1.0f };
		glm::vec4 uiBtnPressedCol{ 0.14f, 0.32f, 0.62f, 1.0f };
		glm::vec4 uiBtnDisabledCol{ 0.40f, 0.42f, 0.45f, 0.65f };
		std::string uiBtnTargetEnt;
		std::string uiBtnOnClickFn;

		// UISlider
		bool hasUISlider = false;
		bool uiSldInteractable = true;
		float uiSldMin = 0.0f;
		float uiSldMax = 1.0f;
		float uiSldVal = 0.5f;
		bool uiSldWhole = false;
		glm::vec4 uiSldBgCol{ 0.16f, 0.18f, 0.22f, 1.0f };
		glm::vec4 uiSldFillCol{ 0.22f, 0.62f, 0.96f, 1.0f };
		glm::vec4 uiSldHandleCol{ 0.95f, 0.96f, 0.98f, 1.0f };
		std::string uiSldTargetEnt;
		std::string uiSldOnValChangedFn;

		// UIProgressBar
		bool hasUIProgressBar = false;
		float uiPbMin = 0.0f;
		float uiPbMax = 100.0f;
		float uiPbVal = 75.0f;
		glm::vec4 uiPbBgCol{ 0.15f, 0.17f, 0.21f, 1.0f };
		glm::vec4 uiPbFillCol{ 0.22f, 0.78f, 0.40f, 1.0f };
		bool uiPbShowPct = true;

		bool hasUIJoystick = false;
		std::string uiJoyStickName = "Move";
		int uiJoyMode = 0;
		float uiJoyTravel = 90.0f;
		float uiJoyActivation = 120.0f;
		glm::vec4 uiJoyBg{ 1.0f, 1.0f, 1.0f, 0.28f };
		glm::vec4 uiJoyKnob{ 1.0f, 1.0f, 1.0f, 0.85f };
		bool uiJoyInteractable = true;

		bool hasUIActionButton = false;
		std::string uiActName = "Jump";
		bool uiActInteractable = true;
		glm::vec4 uiActNormal{ 0.20f, 0.45f, 0.82f, 0.85f };
		glm::vec4 uiActPressed{ 0.14f, 0.32f, 0.62f, 1.0f };

		// ParticleSystem
		bool hasParticle = false;
		std::vector<std::pair<std::string, std::string>> particleKeys;
	};

	EntityData currentData;
	bool inEntity = false;
	std::string currentSubBlock = "";
	struct ParentLink {
		SlipEntity* child = nullptr;
		uint32_t parentId = 0;
		std::string parentName;
	};
	std::vector<ParentLink> parentLinks;
	std::unordered_map<uint32_t, SlipEntity*> idToEntity;

	auto finishEntity = [&]() {
		if (!inEntity) return;

		SlipEntity* ent = new SlipEntity(currentData.name);
		ent->isStatic = currentData.isStatic;
		ent->position = currentData.pos;
		ent->rotation = currentData.rot;
		ent->scale = currentData.scale;
		if (auto tc = ent->GetComponent<TransformComponent>()) {
			tc->position = ent->position;
			tc->rotation = ent->rotation;
			tc->scale = ent->scale;
			tc->inheritScale = currentData.inheritScale;
		}

		// Primitivo legacy (Version 1):
		if (currentData.type == "Primitive" || !currentData.primType.empty()) {
			PrimitiveType pt = PrimitiveType::Cube;
			if (currentData.primType == "Sphere") pt = PrimitiveType::Sphere;
			else if (currentData.primType == "Plane") pt = PrimitiveType::Plane;

			auto meshComp = ent->AddComponent<MeshComponent>();
			if (pt == PrimitiveType::Cube) {
				meshComp->SetModel("primitive:cube");
				ent->AddComponent<BoxColliderComponent>(glm::vec3(1.0f));
			} else if (pt == PrimitiveType::Sphere) {
				meshComp->SetModel("primitive:sphere");
				ent->AddComponent<SphereColliderComponent>(0.5f);
			} else if (pt == PrimitiveType::Plane) {
				meshComp->SetModel("primitive:plane");
				ent->AddComponent<BoxColliderComponent>(glm::vec3(4.0f, 0.1f, 4.0f));
			}
		}

		// MeshComponent
		if (currentData.hasMesh) {
			auto meshComp = ent->GetComponent<MeshComponent>();
			if (!meshComp) {
				meshComp = ent->AddComponent<MeshComponent>();
			}

			// Resolver ModelPath por UID o fallback
			std::string resolvedModel = currentData.modelPath;
			if (!currentData.modelUid.empty()) {
				std::string byUid = SlipFileSystem::FindAssetByGuid(currentData.modelUid);
				if (!byUid.empty()) resolvedModel = byUid;
			}
			if (!resolvedModel.empty()) {
				meshComp->SetModel(resolvedModel);
			}

			// Resolver MaterialPath por UID o fallback
			std::string resolvedMat = currentData.materialPath;
			if (!currentData.materialUid.empty()) {
				std::string byUid = SlipFileSystem::FindAssetByGuid(currentData.materialUid);
				if (!byUid.empty()) resolvedMat = byUid;
			}
			if (!resolvedMat.empty()) {
				meshComp->SetMaterial(resolvedMat);
			}
			for (const auto& sm : currentData.subMaterials) {
				if (!sm.second.empty()) {
					meshComp->SetSubmeshMaterial(sm.first, sm.second);
				}
			}
			meshComp->lightmapIndex = currentData.lightmapIndex;
			meshComp->lightmapScaleOffset = currentData.lightmapScaleOffset;
		}

		// SkinnedMeshComponent
		if (currentData.hasSkinned) {
			auto skinnedComp = ent->GetComponent<SkinnedMeshComponent>();
			if (!skinnedComp) {
				skinnedComp = ent->AddComponent<SkinnedMeshComponent>();
			}

			std::string resolvedModel = currentData.skinnedModelPath;
			if (!currentData.skinnedModelUid.empty()) {
				std::string byUid = SlipFileSystem::FindAssetByGuid(currentData.skinnedModelUid);
				if (!byUid.empty()) resolvedModel = byUid;
			}
			if (!resolvedModel.empty()) {
				skinnedComp->SetModel(resolvedModel);
			}

			std::string resolvedMat = currentData.skinnedMatPath;
			if (!currentData.skinnedMatUid.empty()) {
				std::string byUid = SlipFileSystem::FindAssetByGuid(currentData.skinnedMatUid);
				if (!byUid.empty()) resolvedMat = byUid;
			}
			if (!resolvedMat.empty()) {
				skinnedComp->SetMaterial(resolvedMat);
			}

			for (const auto& sm : currentData.skinnedSubMaterials) {
				if (!sm.second.empty()) {
					skinnedComp->SetSubmeshMaterial(sm.first, sm.second);
				}
			}
			skinnedComp->castShadows = currentData.skinnedCastShadows;
			skinnedComp->BuildBoneHierarchy(this);
		}

		// AnimatorComponent
		if (currentData.hasAnimator) {
			auto animComp = ent->GetComponent<AnimatorComponent>();
			if (!animComp) {
				animComp = ent->AddComponent<AnimatorComponent>();
			}
			animComp->playbackSpeed = currentData.animSpeed;
			animComp->isLooping = currentData.animLooping;
			animComp->isPlaying = currentData.animPlaying;
			animComp->currentTime = currentData.animTime;
			if (!currentData.animControllerPath.empty()) {
				animComp->SetController(currentData.animControllerPath);
			}
			if (currentData.animClipIdx >= 0) {
				animComp->currentClipIndex = currentData.animClipIdx;
			}
			if (!currentData.animClipName.empty()) {
				for (size_t c = 0; c < animComp->clips.size(); ++c) {
					if (animComp->clips[c] && animComp->clips[c]->name == currentData.animClipName) {
						animComp->currentClipIndex = static_cast<int>(c);
						break;
					}
				}
			}
		}

		// BoneComponent
		if (currentData.hasBone) {
			auto boneComp = ent->GetComponent<BoneComponent>();
			if (!boneComp) {
				boneComp = ent->AddComponent<BoneComponent>(currentData.boneIdx, currentData.boneName);
			} else {
				boneComp->boneIndex = currentData.boneIdx;
				boneComp->boneName = currentData.boneName;
			}
		}

		// LightComponent
		if (currentData.hasLight) {
			LightType lt = LightType::Point;
			if (currentData.lightType == "Directional") lt = LightType::Directional;
			else if (currentData.lightType == "Spot") lt = LightType::Spot;

			auto light = ent->AddComponent<LightComponent>(lt);
			if (currentData.lightMobility == "Static") light->mobility = LightMobility::Static;
			else if (currentData.lightMobility == "Mixed") light->mobility = LightMobility::Mixed;
			else light->mobility = LightMobility::Dynamic;
			light->color = currentData.lightColor;
			light->intensity = currentData.lightIntensity;
			light->range = currentData.lightRange;
			light->spotAngle = currentData.lightSpotAngle;
			light->castShadows = currentData.lightCastShadows;
		}

		if (currentData.hasDDGI) {
			auto volume = ent->AddComponent<DDGIVolumeComponent>();
			volume->size = currentData.ddgiSize;
			volume->probesX = currentData.ddgiProbesX;
			volume->probesY = currentData.ddgiProbesY;
			volume->probesZ = currentData.ddgiProbesZ;
			volume->previewProbes = currentData.ddgiPreview;
		}

		if (currentData.hasReflectionProbe) {
			auto probe = ent->AddComponent<ReflectionProbeComponent>();
			probe->size = currentData.probeSize;
			probe->resolution = currentData.probeResolution;
			probe->intensity = currentData.probeIntensity;
			probe->roughnessCutoff = currentData.probeRoughnessCutoff;
			probe->boxProjection = currentData.probeBoxProjection;
			probe->includeDynamic = currentData.probeIncludeDynamic;
		}

		if (currentData.hasPost) {
			auto post = ent->AddComponent<PostProcessVolumeComponent>();
			for (const auto& kv : currentData.postKeys) {
				post->DeserializeKey(kv.first, kv.second);
			}
		}

		if (currentData.hasSky) {
			auto sky = ent->AddComponent<SlipSkyComponent>();
			for (const auto& kv : currentData.skyKeys) {
				sky->DeserializeKey(kv.first, kv.second);
			}
		}

		if (currentData.hasTerrain) {
			auto tc = ent->AddComponent<SlipTerrainComponent>();
			tc->width = currentData.terrainWidth;
			tc->length = currentData.terrainLength;
			tc->resolutionX = currentData.terrainResX;
			tc->resolutionZ = currentData.terrainResZ;
			tc->heightScale = currentData.terrainHeightScale;
			tc->uvScale = currentData.terrainUVScale;
			tc->heightmapPath = currentData.terrainHeightmapPath;
			tc->materialPath = currentData.terrainMaterialPath;
			tc->enableCollision = currentData.terrainCollision;
			tc->wireframe = currentData.terrainWireframe;
			tc->castShadows = currentData.terrainCastShadows;
			tc->RebuildTerrain();
		}

		// BoxCollider
		if (currentData.hasBoxCol) {
			auto bc = ent->GetComponent<BoxColliderComponent>();
			if (!bc) bc = ent->AddComponent<BoxColliderComponent>(currentData.boxSize);
			bc->size = currentData.boxSize;
			bc->centerOffset = currentData.boxOffset;
			bc->isTrigger = currentData.boxTrigger;
			bc->CreateOrUpdateShape();
		}

		// SphereCollider
		if (currentData.hasSphereCol) {
			auto sc = ent->GetComponent<SphereColliderComponent>();
			if (!sc) sc = ent->AddComponent<SphereColliderComponent>(currentData.sphereRadius);
			sc->radius = currentData.sphereRadius;
			sc->centerOffset = currentData.sphereOffset;
			sc->isTrigger = currentData.sphereTrigger;
			sc->CreateOrUpdateShape();
		}

		// CapsuleCollider
		if (currentData.hasCapCol) {
			auto cc = ent->GetComponent<CapsuleColliderComponent>();
			if (!cc) cc = ent->AddComponent<CapsuleColliderComponent>(currentData.capRadius, currentData.capHeight);
			cc->radius = currentData.capRadius;
			cc->height = currentData.capHeight;
			cc->centerOffset = currentData.capOffset;
			cc->isTrigger = currentData.capTrigger;
			cc->CreateOrUpdateShape();
		}

		// MeshCollider
		if (currentData.hasMeshCol) {
			auto mc = ent->GetComponent<MeshColliderComponent>();
			if (!mc) mc = ent->AddComponent<MeshColliderComponent>();
			mc->isConvex = currentData.meshConvex;
			mc->centerOffset = currentData.meshOffset;
			mc->isTrigger = currentData.meshTrigger;
			mc->CreateOrUpdateShape();
			if (!mc->rigidBody) {
				mc->RecreateRigidBody(0.0f, false);
			}
		}

		// RigidBody
		if (currentData.hasRb) {
			auto rb = ent->AddComponent<RigidBodyComponent>();
			rb->mass = currentData.rbMass;
			rb->linearDrag = currentData.rbLinearDrag;
			rb->angularDrag = currentData.rbAngularDrag;
			rb->isKinematic = currentData.rbKinematic;
			rb->useGravity = currentData.rbGravity;
			rb->friction = currentData.rbFriction;
			rb->rollingFriction = currentData.rbRollingFriction;
			rb->restitution = currentData.rbRestitution;
			rb->freezePositionX = currentData.rbFreezePosX;
			rb->freezePositionY = currentData.rbFreezePosY;
			rb->freezePositionZ = currentData.rbFreezePosZ;
			rb->freezeRotationX = currentData.rbFreezeRotX;
			rb->freezeRotationY = currentData.rbFreezeRotY;
			rb->freezeRotationZ = currentData.rbFreezeRotZ;
			rb->ApplyToCollider();
		}

		// Vehicle
		if (currentData.hasVehicle) {
			auto veh = ent->AddComponent<VehicleComponent>();
			veh->suspensionStiffness = currentData.vehStiffness;
			veh->suspensionDamping = currentData.vehDamping;
			veh->suspensionCompression = currentData.vehCompression;
			veh->maxSuspensionTravelCm = currentData.vehMaxTravel;
			veh->frictionSlip = currentData.vehFrictionSlip;
			veh->maxSuspensionForce = currentData.vehMaxForce;
			veh->rollInfluence = currentData.vehRollInfluence;
			veh->chassisWidth = currentData.vehWidth;
			veh->chassisHeight = currentData.vehHeight;
			veh->chassisLength = currentData.vehLength;
			veh->defaultWheelRadius = currentData.vehWheelRadius;
			veh->SetupDefaultCarWheels();
		}

		// Scripts (soporte de múltiples scripts y variables públicas)
		for (const auto& scData : currentData.scripts) {
			if (scData.path.empty()) continue;
			auto sc = ent->AddComponent<ScriptComponent>(scData.path);
			sc->SetEnabled(scData.enabled);
			for (const auto& prop : scData.properties) {
				switch (prop.type) {
					case ScriptPropertyType::Float: sc->SetPropertyValue(prop.name, prop.floatValue); break;
					case ScriptPropertyType::Int: sc->SetPropertyValue(prop.name, prop.intValue); break;
					case ScriptPropertyType::Bool: sc->SetPropertyValue(prop.name, prop.boolValue); break;
					case ScriptPropertyType::String: sc->SetPropertyValue(prop.name, prop.stringValue); break;
					case ScriptPropertyType::Vec3: sc->SetPropertyValue(prop.name, prop.vec3Value); break;
				}
			}
			sc->PushPropertiesToLua();
		}
		if (currentData.scripts.empty() && currentData.hasScript && !currentData.scriptPath.empty()) {
			auto sc = ent->AddComponent<ScriptComponent>(currentData.scriptPath);
			sc->SetEnabled(currentData.scriptEnabled);
		}

		// CameraComponent
		if (currentData.hasCam) {
			auto cam = ent->AddComponent<CameraComponent>();
			cam->projectionType = (currentData.camProj == "Orthographic") ? CameraProjection::Orthographic : CameraProjection::Perspective;
			cam->fov = currentData.camFov;
			cam->nearClip = currentData.camNear;
			cam->farClip = currentData.camFar;
			cam->orthoSize = currentData.camOrtho;
			cam->isPrimary = currentData.camPrimary;
			cam->clearColor = currentData.camClearColor;
			cam->letterbox = currentData.camLetterbox;
			cam->pillarbox = currentData.camPillarbox;
		}

		// CinematicCameraComponent
		if (currentData.hasCinematicCam) {
			auto ccam = ent->AddComponent<CinematicCameraComponent>();
			ccam->targetEntityName = currentData.cinematicCamTarget;
			ccam->targetDistance = currentData.cinematicCamDist;
			ccam->targetHeight = currentData.cinematicCamHeight;
			ccam->targetPitch = currentData.cinematicCamPitch;
			ccam->positionLag = currentData.cinematicCamPosLag;
			ccam->rotationLag = currentData.cinematicCamRotLag;
			ccam->driftCompensation = currentData.cinematicCamDriftComp;
			ccam->enableDynamicFOV = currentData.cinematicCamDynamicFOV;
			ccam->baseFOV = currentData.cinematicCamBaseFOV;
			ccam->boostFOV = currentData.cinematicCamBoostFOV;
		}

		// KeyframeAnimationComponent
		if (currentData.hasKeyframeAnim) {
			auto kfa = ent->AddComponent<KeyframeAnimationComponent>();
			kfa->playOnStart = currentData.keyframePlayOnStart;
			kfa->loop = currentData.keyframeLoop;
			kfa->playbackSpeed = currentData.keyframeSpeed;
			if (!currentData.keyframeClipPath.empty()) {
				kfa->LoadClip(currentData.keyframeClipPath);
			}
		}

		// SpriteRendererComponent
		if (currentData.hasSprite) {
			auto sr = ent->AddComponent<SpriteRendererComponent>();
			sr->color = currentData.spriteColor;
			sr->flipX = currentData.spriteFlipX;
			sr->flipY = currentData.spriteFlipY;
			sr->sortingOrder = currentData.spriteSortingOrder;
			sr->orderInLayer = currentData.spriteSortingOrder;
			sr->sortingLayerName = currentData.spriteSortingLayer;
			sr->pixelsPerUnit = currentData.spritePPU;
			sr->hFrames = currentData.spriteHFrames;
			sr->vFrames = currentData.spriteVFrames;
			sr->currentFrame = currentData.spriteCurrentFrame;
			sr->isLit = currentData.spriteIsLit;
			sr->isPixelArt = currentData.spriteIsPixelArt;
			if (!currentData.spriteTexPath.empty()) {
				sr->SetTexture(currentData.spriteTexPath);
			}
			if (!currentData.spriteNormalMapPath.empty()) {
				sr->SetNormalMap(currentData.spriteNormalMapPath);
			}
		}

		// SpriteAnimatorComponent
		if (currentData.hasSpriteAnimator) {
			auto sa = ent->AddComponent<SpriteAnimatorComponent>();
			sa->playOnStart = currentData.saPlayOnStart;
			sa->defaultClip = currentData.saDefaultClip;
			sa->clips = currentData.saClips;
			if (sa->playOnStart && !sa->defaultClip.empty()) {
				sa->Play(sa->defaultClip);
			}
		}

		// BoxCollider2DComponent
		if (currentData.hasBoxCollider2D) {
			auto box2d = ent->AddComponent<BoxCollider2DComponent>();
			box2d->size = currentData.box2dSize;
			box2d->offset = currentData.box2dOffset;
			box2d->isTrigger = currentData.box2dIsTrigger;
			box2d->density = currentData.box2dDensity;
			box2d->friction = currentData.box2dFriction;
			box2d->restitution = currentData.box2dRestitution;
			if (!currentData.hasRigidBody2D) {
				box2d->RecreateStaticBody();
			}
		}

		// CircleCollider2DComponent
		if (currentData.hasCircleCollider2D) {
			auto circ2d = ent->AddComponent<CircleCollider2DComponent>();
			circ2d->radius = currentData.circ2dRadius;
			circ2d->offset = currentData.circ2dOffset;
			circ2d->isTrigger = currentData.circ2dIsTrigger;
			circ2d->density = currentData.circ2dDensity;
			circ2d->friction = currentData.circ2dFriction;
			circ2d->restitution = currentData.circ2dRestitution;
			if (!currentData.hasRigidBody2D) {
				circ2d->RecreateStaticBody();
			}
		}

		// RigidBody2DComponent
		if (currentData.hasRigidBody2D) {
			auto rb2d = ent->AddComponent<RigidBody2DComponent>();
			rb2d->bodyType = static_cast<BodyType2D>(currentData.rb2dBodyType);
			rb2d->mass = currentData.rb2dMass;
			rb2d->gravityScale = currentData.rb2dGravityScale;
			rb2d->freezeRotation = currentData.rb2dFreezeRotation;
			rb2d->linearDamping = currentData.rb2dLinearDamping;
			rb2d->angularDamping = currentData.rb2dAngularDamping;
			rb2d->RebuildPhysicsBody();
		}

		// Light2DComponent
		if (currentData.hasLight2D) {
			auto l2d = ent->AddComponent<Light2DComponent>();
			l2d->lightType = static_cast<Light2DType>(currentData.l2dType);
			l2d->color = currentData.l2dColor;
			l2d->intensity = currentData.l2dIntensity;
			l2d->radius = currentData.l2dRadius;
			l2d->innerRadius = currentData.l2dInnerRadius;
			l2d->spotAngle = currentData.l2dSpotAngle;
			l2d->castShadows = currentData.l2dCastShadows;
			l2d->shadowIntensity = currentData.l2dShadowIntensity;
		}

		// ShadowCaster2DComponent
		if (currentData.hasShadowCaster2D) {
			auto sc2d = ent->AddComponent<ShadowCaster2DComponent>();
			sc2d->shape = static_cast<ShadowCasterShape2D>(currentData.sc2dShape);
			sc2d->size = currentData.sc2dSize;
			sc2d->radius = currentData.sc2dRadius;
			sc2d->offset = currentData.sc2dOffset;
			sc2d->castShadows = currentData.sc2dCastShadows;
		}

		// TileGridComponent
		if (currentData.hasTileGrid) {
			auto tg = ent->AddComponent<TileGridComponent>();
			tg->cellSize = currentData.tgCellSize;
			tg->anchor = static_cast<TileGridAnchor>(currentData.tgAnchor);
			tg->sheetHFrames = currentData.tgSheetHFrames;
			tg->sheetVFrames = currentData.tgSheetVFrames;
			tg->sortingLayerName = currentData.tgSortingLayer;
			tg->orderInLayer = currentData.tgSortingOrder;
			tg->tiles = currentData.tgTiles;
			tg->isPixelArt = currentData.tgIsPixelArt;
			if (!currentData.tgTileSheetPath.empty()) {
				tg->SetTileSheet(currentData.tgTileSheetPath);
			}
		}

		// CanvasComponent
		if (currentData.hasCanvas) {
			auto cv = ent->AddComponent<CanvasComponent>();
			cv->renderMode = (currentData.canvasRenderMode == "WorldSpace") ? CanvasRenderMode::WorldSpace : CanvasRenderMode::ScreenSpaceOverlay;
			cv->referenceResolution = currentData.canvasRefRes;
			cv->scaleWithScreenSize = currentData.canvasScaleWithScreen;
			cv->sortingOrder = currentData.canvasSortingOrder;
		}

		// RectTransformComponent
		if (currentData.hasRect) {
			auto rt = ent->AddComponent<RectTransformComponent>();
			rt->SetAnchorPresetByName(currentData.rectAnchorPreset);
			rt->anchoredPosition = currentData.rectAnchoredPos;
			rt->size = currentData.rectSize;
			rt->pivot = currentData.rectPivot;
		}

		// UIImageComponent
		if (currentData.hasUIImage) {
			auto img = ent->AddComponent<UIImageComponent>();
			img->color = currentData.uiImgColor;
			img->preserveAspect = currentData.uiImgPreserveAspect;
			if (!currentData.uiImgTexPath.empty()) {
				img->SetTexture(currentData.uiImgTexPath);
			}
		}

		// UITextComponent
		if (currentData.hasUIText) {
			auto txt = ent->AddComponent<UITextComponent>(currentData.uiTxtText);
			txt->fontPath = currentData.uiTxtFontPath;
			txt->fontSize = currentData.uiTxtFontSize;
			txt->color = currentData.uiTxtColor;
			txt->alignH = static_cast<UITextAlignH>(std::clamp(currentData.uiTxtAlignH, 0, 2));
			txt->alignV = static_cast<UITextAlignV>(std::clamp(currentData.uiTxtAlignV, 0, 2));
		}

		// UIButtonComponent
		if (currentData.hasUIButton) {
			auto btn = ent->AddComponent<UIButtonComponent>();
			btn->interactable = currentData.uiBtnInteractable;
			btn->normalColor = currentData.uiBtnNormalCol;
			btn->hoverColor = currentData.uiBtnHoverCol;
			btn->pressedColor = currentData.uiBtnPressedCol;
			btn->disabledColor = currentData.uiBtnDisabledCol;
			btn->targetEntityName = currentData.uiBtnTargetEnt;
			btn->onClickFunction = currentData.uiBtnOnClickFn;
		}

		// UISliderComponent
		if (currentData.hasUISlider) {
			auto sld = ent->AddComponent<UISliderComponent>();
			sld->interactable = currentData.uiSldInteractable;
			sld->minValue = currentData.uiSldMin;
			sld->maxValue = currentData.uiSldMax;
			sld->value = currentData.uiSldVal;
			sld->wholeNumbers = currentData.uiSldWhole;
			sld->backgroundColor = currentData.uiSldBgCol;
			sld->fillColor = currentData.uiSldFillCol;
			sld->handleColor = currentData.uiSldHandleCol;
			sld->targetEntityName = currentData.uiSldTargetEnt;
			sld->onValueChangedFunction = currentData.uiSldOnValChangedFn;
		}

		// UIProgressBarComponent
		if (currentData.hasUIProgressBar) {
			auto pb = ent->AddComponent<UIProgressBarComponent>();
			pb->minValue = currentData.uiPbMin;
			pb->maxValue = currentData.uiPbMax;
			pb->value = currentData.uiPbVal;
			pb->backgroundColor = currentData.uiPbBgCol;
			pb->fillColor = currentData.uiPbFillCol;
			pb->showPercentageText = currentData.uiPbShowPct;
		}

		if (currentData.hasUIJoystick) {
			auto joy = ent->AddComponent<UIJoystickComponent>();
			joy->stickName = currentData.uiJoyStickName;
			joy->mode = static_cast<UIJoystickMode>(std::clamp(currentData.uiJoyMode, 0, 2));
			joy->travelRadius = currentData.uiJoyTravel;
			joy->activationRadius = currentData.uiJoyActivation;
			joy->backgroundColor = currentData.uiJoyBg;
			joy->knobColor = currentData.uiJoyKnob;
			joy->interactable = currentData.uiJoyInteractable;
		}

		if (currentData.hasUIActionButton) {
			auto actionBtn = ent->AddComponent<UIActionButtonComponent>();
			actionBtn->actionName = currentData.uiActName;
			actionBtn->interactable = currentData.uiActInteractable;
			actionBtn->normalColor = currentData.uiActNormal;
			actionBtn->pressedColor = currentData.uiActPressed;
		}

		// ParticleSystemComponent
		if (currentData.hasParticle) {
			auto ps = ent->AddComponent<ParticleSystemComponent>();
			for (const auto& kv : currentData.particleKeys) {
				ps->DeserializeKey(kv.first, kv.second);
			}
			ps->OnInit();
		}

		if (currentData.id != 0) {
			idToEntity[currentData.id] = ent;
		}

		if (currentData.parentId != 0 || !currentData.parentName.empty()) {
			parentLinks.push_back({ ent, currentData.parentId, currentData.parentName });
		}

		// Asegurar que los componentes y física sincronicen exactamente la posición cargada
		if (auto tc = ent->GetComponent<TransformComponent>()) {
			tc->position = currentData.pos;
			tc->rotation = currentData.rot;
			tc->scale = currentData.scale;
			tc->inheritScale = currentData.inheritScale;
		}
		ent->position = currentData.pos;
		ent->rotation = currentData.rot;
		ent->scale = currentData.scale;
		if (auto col = ent->GetComponent<ColliderComponent>()) {
			col->SyncTransformFromEntity();
		}

		AddEntity(ent);
	};

	while (std::getline(ss, line))
	{
		size_t first = line.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) continue;
		size_t last = line.find_last_not_of(" \t\r\n");
		std::string trimmed = line.substr(first, (last - first + 1));

		if (trimmed.empty() || trimmed[0] == '#') continue;

		if (trimmed == "Entity:")
		{
			finishEntity();
			inEntity = true;
			currentData = EntityData();
			currentSubBlock = "";
			continue;
		}

		if (!inEntity) {
			if (trimmed.rfind("LightmapAsset:", 0) == 0) {
				std::string p = trimmed.substr(14);
				size_t f = p.find_first_not_of(" \t\r\n");
				size_t l = p.find_last_not_of(" \t\r\n");
				this->lightmapPath = (f != std::string::npos && l != std::string::npos) ? p.substr(f, l - f + 1) : "";
			}
			continue;
		}

		if (trimmed == "MeshComponent:") { currentSubBlock = "Mesh"; currentData.hasMesh = true; continue; }
		if (trimmed == "SkinnedMeshComponent:") { currentSubBlock = "SkinnedMesh"; currentData.hasSkinned = true; continue; }
		if (trimmed == "AnimatorComponent:") { currentSubBlock = "Animator"; currentData.hasAnimator = true; continue; }
		if (trimmed == "BoneComponent:") { currentSubBlock = "Bone"; currentData.hasBone = true; continue; }
		if (trimmed == "LightComponent:") { currentSubBlock = "Light"; currentData.hasLight = true; continue; }
		if (trimmed == "DDGIVolumeComponent:") { currentSubBlock = "DDGI"; currentData.hasDDGI = true; continue; }
		if (trimmed == "ReflectionProbeComponent:") { currentSubBlock = "ReflectionProbe"; currentData.hasReflectionProbe = true; continue; }
		if (trimmed == "PostProcessVolumeComponent:") { currentSubBlock = "Post"; currentData.hasPost = true; continue; }
		if (trimmed == "SlipSkyComponent:") { currentSubBlock = "SlipSky"; currentData.hasSky = true; continue; }
		if (trimmed == "SlipTerrainComponent:") { currentSubBlock = "SlipTerrain"; currentData.hasTerrain = true; continue; }
		if (trimmed == "BoxColliderComponent:") { currentSubBlock = "BoxCol"; currentData.hasBoxCol = true; continue; }
		if (trimmed == "SphereColliderComponent:") { currentSubBlock = "SphereCol"; currentData.hasSphereCol = true; continue; }
		if (trimmed == "CapsuleColliderComponent:") { currentSubBlock = "CapCol"; currentData.hasCapCol = true; continue; }
		if (trimmed == "MeshColliderComponent:") { currentSubBlock = "MeshCol"; currentData.hasMeshCol = true; continue; }
		if (trimmed == "RigidBodyComponent:") { currentSubBlock = "Rb"; currentData.hasRb = true; continue; }
		if (trimmed == "VehicleComponent:") { currentSubBlock = "Vehicle"; currentData.hasVehicle = true; continue; }
		if (trimmed == "ScriptComponent:") {
			currentSubBlock = "Script";
			currentData.hasScript = true;
			EntityData::SerializedScript sc;
			currentData.scripts.push_back(sc);
			continue;
		}
		if (trimmed == "CameraComponent:") { currentSubBlock = "Cam"; currentData.hasCam = true; continue; }
		if (trimmed == "CinematicCameraComponent:" || trimmed == "CinematicCamera:") { currentSubBlock = "CinematicCam"; currentData.hasCinematicCam = true; continue; }
		if (trimmed == "KeyframeAnimationComponent:" || trimmed == "KeyframeAnimation:") { currentSubBlock = "KeyframeAnim"; currentData.hasKeyframeAnim = true; continue; }
		if (trimmed == "SpriteRendererComponent:") { currentSubBlock = "Sprite"; currentData.hasSprite = true; continue; }
		if (trimmed == "SpriteAnimatorComponent:") { currentSubBlock = "SpriteAnimator"; currentData.hasSpriteAnimator = true; continue; }
		if (trimmed == "BoxCollider2DComponent:") { currentSubBlock = "Box2D"; currentData.hasBoxCollider2D = true; continue; }
		if (trimmed == "CircleCollider2DComponent:") { currentSubBlock = "Circle2D"; currentData.hasCircleCollider2D = true; continue; }
		if (trimmed == "RigidBody2DComponent:") { currentSubBlock = "RigidBody2D"; currentData.hasRigidBody2D = true; continue; }
		if (trimmed == "Light2DComponent:") { currentSubBlock = "Light2D"; currentData.hasLight2D = true; continue; }
		if (trimmed == "ShadowCaster2DComponent:") { currentSubBlock = "ShadowCaster2D"; currentData.hasShadowCaster2D = true; continue; }
		if (trimmed == "TileGridComponent:") { currentSubBlock = "TileGrid"; currentData.hasTileGrid = true; continue; }
		if (trimmed == "CanvasComponent:") { currentSubBlock = "Canvas"; currentData.hasCanvas = true; continue; }
		if (trimmed == "RectTransformComponent:") { currentSubBlock = "Rect"; currentData.hasRect = true; continue; }
		if (trimmed == "UIImageComponent:") { currentSubBlock = "UIImage"; currentData.hasUIImage = true; continue; }
		if (trimmed == "UITextComponent:") { currentSubBlock = "UIText"; currentData.hasUIText = true; continue; }
		if (trimmed == "UIButtonComponent:") { currentSubBlock = "UIButton"; currentData.hasUIButton = true; continue; }
		if (trimmed == "UISliderComponent:") { currentSubBlock = "UISlider"; currentData.hasUISlider = true; continue; }
		if (trimmed == "UIProgressBarComponent:") { currentSubBlock = "UIProgressBar"; currentData.hasUIProgressBar = true; continue; }
		if (trimmed == "UIJoystickComponent:") { currentSubBlock = "UIJoystick"; currentData.hasUIJoystick = true; continue; }
		if (trimmed == "UIActionButtonComponent:") { currentSubBlock = "UIActionButton"; currentData.hasUIActionButton = true; continue; }
		if (trimmed == "ParticleSystemComponent:") { currentSubBlock = "Particle"; currentData.hasParticle = true; continue; }

		size_t colon = trimmed.find(':');
		if (colon == std::string::npos) continue;

		std::string key = trimmed.substr(0, colon);
		std::string val = trimmed.substr(colon + 1);
		size_t vf = val.find_first_not_of(" \t\r\n");
		if (vf != std::string::npos) val = val.substr(vf); else val = "";
		size_t vl = val.find_last_not_of(" \t\r\n");
		if (vl != std::string::npos) val = val.substr(0, vl + 1);

		if (currentSubBlock == "Mesh") {
			if (key == "ModelPath") currentData.modelPath = val;
			else if (key == "ModelUID") currentData.modelUid = val;
			else if (key == "MaterialPath") currentData.materialPath = val;
			else if (key == "MaterialUID") currentData.materialUid = val;
			else if (key == "SubMaterial") {
				std::stringstream vs(val);
				size_t slotIdx = 0;
				if (vs >> slotIdx) {
					std::string smPath;
					std::getline(vs, smPath);
					size_t sf = smPath.find_first_not_of(" \t\r\n");
					if (sf != std::string::npos) smPath = smPath.substr(sf);
					else smPath.clear();
					size_t sl = smPath.find_last_not_of(" \t\r\n");
					if (sl != std::string::npos) smPath = smPath.substr(0, sl + 1);
					currentData.subMaterials.push_back({ slotIdx, smPath });
				}
			}
			else if (key == "LightmapIndex") std::stringstream(val) >> currentData.lightmapIndex;
			else if (key == "LightmapScaleOffset") {
				std::stringstream vs(val);
				vs >> currentData.lightmapScaleOffset.x >> currentData.lightmapScaleOffset.y >> currentData.lightmapScaleOffset.z >> currentData.lightmapScaleOffset.w;
			}
		}
		else if (currentSubBlock == "SkinnedMesh") {
			if (key == "ModelPath") currentData.skinnedModelPath = val;
			else if (key == "ModelUID") currentData.skinnedModelUid = val;
			else if (key == "MaterialPath") currentData.skinnedMatPath = val;
			else if (key == "MaterialUID") currentData.skinnedMatUid = val;
			else if (key == "CastShadows") currentData.skinnedCastShadows = (val == "1" || val == "true");
			else if (key == "SubMaterial") {
				std::stringstream vs(val);
				size_t slotIdx = 0;
				if (vs >> slotIdx) {
					std::string smPath;
					std::getline(vs, smPath);
					size_t sf = smPath.find_first_not_of(" \t\r\n");
					if (sf != std::string::npos) smPath = smPath.substr(sf);
					else smPath.clear();
					size_t sl = smPath.find_last_not_of(" \t\r\n");
					if (sl != std::string::npos) smPath = smPath.substr(0, sl + 1);
					currentData.skinnedSubMaterials.push_back({ slotIdx, smPath });
				}
			}
		}
		else if (currentSubBlock == "Animator") {
			if (key == "CurrentClipIndex") std::stringstream(val) >> currentData.animClipIdx;
			else if (key == "CurrentClipName") currentData.animClipName = val;
			else if (key == "PlaybackSpeed") std::stringstream(val) >> currentData.animSpeed;
			else if (key == "IsPlaying") currentData.animPlaying = (val == "1" || val == "true");
			else if (key == "IsLooping") currentData.animLooping = (val == "1" || val == "true");
			else if (key == "CurrentTime") std::stringstream(val) >> currentData.animTime;
			else if (key == "ControllerPath") currentData.animControllerPath = val;
		}
		else if (currentSubBlock == "Bone") {
			if (key == "BoneIndex") std::stringstream(val) >> currentData.boneIdx;
			else if (key == "BoneName") currentData.boneName = val;
		}
		else if (currentSubBlock == "Light") {
			if (key == "Type") currentData.lightType = val;
			else if (key == "Mobility") currentData.lightMobility = val;
			else if (key == "Color") {
				std::stringstream valStream(val);
				valStream >> currentData.lightColor.r >> currentData.lightColor.g >> currentData.lightColor.b;
			}
			else if (key == "Intensity") {
				std::stringstream(val) >> currentData.lightIntensity;
			}
			else if (key == "Range") {
				std::stringstream(val) >> currentData.lightRange;
			}
			else if (key == "SpotAngle") {
				std::stringstream(val) >> currentData.lightSpotAngle;
			}
			else if (key == "CastShadows") {
				currentData.lightCastShadows = (val == "1" || val == "true");
			}
		}
		else if (currentSubBlock == "DDGI") {
			if (key == "Size") {
				std::stringstream valStream(val);
				valStream >> currentData.ddgiSize.x >> currentData.ddgiSize.y >> currentData.ddgiSize.z;
			}
			else if (key == "ProbesX") std::stringstream(val) >> currentData.ddgiProbesX;
			else if (key == "ProbesY") std::stringstream(val) >> currentData.ddgiProbesY;
			else if (key == "ProbesZ") std::stringstream(val) >> currentData.ddgiProbesZ;
			else if (key == "Preview") currentData.ddgiPreview = (val == "1" || val == "true");
		}
		else if (currentSubBlock == "ReflectionProbe") {
			if (key == "Size") {
				std::stringstream valStream(val);
				valStream >> currentData.probeSize.x >> currentData.probeSize.y >> currentData.probeSize.z;
			}
			else if (key == "Resolution") std::stringstream(val) >> currentData.probeResolution;
			else if (key == "Intensity") std::stringstream(val) >> currentData.probeIntensity;
			else if (key == "RoughnessCutoff") std::stringstream(val) >> currentData.probeRoughnessCutoff;
			else if (key == "BoxProjection") currentData.probeBoxProjection = (val == "1" || val == "true");
			else if (key == "IncludeDynamic") currentData.probeIncludeDynamic = (val == "1" || val == "true");
		}
		else if (currentSubBlock == "Post") {
			currentData.postKeys.push_back({ key, val });
		}
		else if (currentSubBlock == "SlipSky") {
			currentData.skyKeys.push_back({ key, val });
		}
		else if (currentSubBlock == "SlipTerrain") {
			if (key == "Width") { std::stringstream(val) >> currentData.terrainWidth; }
			else if (key == "Length") { std::stringstream(val) >> currentData.terrainLength; }
			else if (key == "ResolutionX") { std::stringstream(val) >> currentData.terrainResX; }
			else if (key == "ResolutionZ") { std::stringstream(val) >> currentData.terrainResZ; }
			else if (key == "HeightScale") { std::stringstream(val) >> currentData.terrainHeightScale; }
			else if (key == "UVScale") {
				std::stringstream vss(val);
				vss >> currentData.terrainUVScale.x >> currentData.terrainUVScale.y;
			}
			else if (key == "HeightmapPath") currentData.terrainHeightmapPath = val;
			else if (key == "MaterialPath") currentData.terrainMaterialPath = val;
			else if (key == "EnableCollision") {
				int c = 1; std::stringstream(val) >> c; currentData.terrainCollision = (c != 0);
			}
			else if (key == "Wireframe") {
				int w = 0; std::stringstream(val) >> w; currentData.terrainWireframe = (w != 0);
			}
			else if (key == "CastShadows") {
				int s = 0; std::stringstream(val) >> s; currentData.terrainCastShadows = (s != 0);
			}
		}
		else if (currentSubBlock == "Particle") {
			currentData.particleKeys.push_back({ key, val });
		}
		else if (currentSubBlock == "BoxCol") {
			if (key == "Size") {
				std::stringstream valStream(val);
				valStream >> currentData.boxSize.x >> currentData.boxSize.y >> currentData.boxSize.z;
			}
			else if (key == "CenterOffset") {
				std::stringstream valStream(val);
				valStream >> currentData.boxOffset.x >> currentData.boxOffset.y >> currentData.boxOffset.z;
			}
			else if (key == "IsTrigger") {
				int t = 0; std::stringstream(val) >> t; currentData.boxTrigger = (t != 0);
			}
		}
		else if (currentSubBlock == "SphereCol") {
			if (key == "Radius") {
				std::stringstream(val) >> currentData.sphereRadius;
			}
			else if (key == "CenterOffset") {
				std::stringstream valStream(val);
				valStream >> currentData.sphereOffset.x >> currentData.sphereOffset.y >> currentData.sphereOffset.z;
			}
			else if (key == "IsTrigger") {
				int t = 0; std::stringstream(val) >> t; currentData.sphereTrigger = (t != 0);
			}
		}
		else if (currentSubBlock == "CapCol") {
			if (key == "Radius") {
				std::stringstream(val) >> currentData.capRadius;
			}
			else if (key == "Height") {
				std::stringstream(val) >> currentData.capHeight;
			}
			else if (key == "CenterOffset") {
				std::stringstream valStream(val);
				valStream >> currentData.capOffset.x >> currentData.capOffset.y >> currentData.capOffset.z;
			}
			else if (key == "IsTrigger") {
				int t = 0; std::stringstream(val) >> t; currentData.capTrigger = (t != 0);
			}
		}
		else if (currentSubBlock == "MeshCol") {
			if (key == "IsConvex") {
				int c = 0; std::stringstream(val) >> c; currentData.meshConvex = (c != 0);
			}
			else if (key == "CenterOffset") {
				std::stringstream valStream(val);
				valStream >> currentData.meshOffset.x >> currentData.meshOffset.y >> currentData.meshOffset.z;
			}
			else if (key == "IsTrigger") {
				int t = 0; std::stringstream(val) >> t; currentData.meshTrigger = (t != 0);
			}
		}
		else if (currentSubBlock == "Rb") {
			if (key == "Mass") {
				std::stringstream(val) >> currentData.rbMass;
			}
			else if (key == "LinearDrag") {
				std::stringstream(val) >> currentData.rbLinearDrag;
			}
			else if (key == "AngularDrag") {
				std::stringstream(val) >> currentData.rbAngularDrag;
			}
			else if (key == "IsKinematic") {
				int k = 0; std::stringstream(val) >> k; currentData.rbKinematic = (k != 0);
			}
			else if (key == "UseGravity") {
				int g = 1; std::stringstream(val) >> g; currentData.rbGravity = (g != 0);
			}
			else if (key == "Friction") {
				std::stringstream(val) >> currentData.rbFriction;
			}
			else if (key == "RollingFriction") {
				std::stringstream(val) >> currentData.rbRollingFriction;
			}
			else if (key == "Restitution") {
				std::stringstream(val) >> currentData.rbRestitution;
			}
			else if (key == "FreezePosition") {
				std::stringstream valStream(val);
				int x = 0, y = 0, z = 0;
				valStream >> x >> y >> z;
				currentData.rbFreezePosX = (x != 0);
				currentData.rbFreezePosY = (y != 0);
				currentData.rbFreezePosZ = (z != 0);
			}
			else if (key == "FreezeRotation") {
				std::stringstream valStream(val);
				int x = 0, y = 0, z = 0;
				valStream >> x >> y >> z;
				currentData.rbFreezeRotX = (x != 0);
				currentData.rbFreezeRotY = (y != 0);
				currentData.rbFreezeRotZ = (z != 0);
			}
		}
		else if (currentSubBlock == "Vehicle") {
			if (key == "SuspensionStiffness") std::stringstream(val) >> currentData.vehStiffness;
			else if (key == "SuspensionDamping") std::stringstream(val) >> currentData.vehDamping;
			else if (key == "SuspensionCompression") std::stringstream(val) >> currentData.vehCompression;
			else if (key == "MaxSuspensionTravelCm") std::stringstream(val) >> currentData.vehMaxTravel;
			else if (key == "FrictionSlip") std::stringstream(val) >> currentData.vehFrictionSlip;
			else if (key == "MaxSuspensionForce") std::stringstream(val) >> currentData.vehMaxForce;
			else if (key == "RollInfluence") std::stringstream(val) >> currentData.vehRollInfluence;
			else if (key == "ChassisDimensions") {
				std::stringstream vs(val);
				vs >> currentData.vehWidth >> currentData.vehHeight >> currentData.vehLength >> currentData.vehWheelRadius;
			}
		}
		else if (currentSubBlock == "Script") {
			if (!currentData.scripts.empty()) {
				auto& sc = currentData.scripts.back();
				if (key == "ScriptPath") {
					sc.path = val;
				}
				else if (key == "Enabled") {
					int en = 1; std::stringstream(val) >> en;
					sc.enabled = (en != 0);
				}
				else if (key == "Property") {
					std::stringstream vs(val);
					std::string pName, pType;
					vs >> pName >> pType;
					ScriptProperty prop;
					prop.name = pName;
					if (pType == "float") {
						prop.type = ScriptPropertyType::Float;
						vs >> prop.floatValue;
					} else if (pType == "int") {
						prop.type = ScriptPropertyType::Int;
						vs >> prop.intValue;
						prop.floatValue = static_cast<float>(prop.intValue);
					} else if (pType == "bool") {
						prop.type = ScriptPropertyType::Bool;
						int bVal = 0; vs >> bVal;
						prop.boolValue = (bVal != 0);
					} else if (pType == "string") {
						prop.type = ScriptPropertyType::String;
						vs >> prop.stringValue;
					} else if (pType == "vec3") {
						prop.type = ScriptPropertyType::Vec3;
						vs >> prop.vec3Value.x >> prop.vec3Value.y >> prop.vec3Value.z;
					}
					sc.properties.push_back(prop);
				}
			}
			if (key == "ScriptPath") {
				currentData.scriptPath = val;
			}
			else if (key == "Enabled") {
				int en = 1;
				std::stringstream(val) >> en;
				currentData.scriptEnabled = (en != 0);
			}
		}
		else if (currentSubBlock == "Cam") {
			if (key == "Projection") {
				currentData.camProj = val;
			}
			else if (key == "FOV") {
				std::stringstream(val) >> currentData.camFov;
			}
			else if (key == "NearClip") {
				std::stringstream(val) >> currentData.camNear;
			}
			else if (key == "FarClip") {
				std::stringstream(val) >> currentData.camFar;
			}
			else if (key == "OrthoSize") {
				std::stringstream(val) >> currentData.camOrtho;
			}
			else if (key == "IsPrimary") {
				int p = 1;
				std::stringstream(val) >> p;
				currentData.camPrimary = (p != 0);
			}
			else if (key == "ClearColor") {
				std::stringstream valStream(val);
				valStream >> currentData.camClearColor.r >> currentData.camClearColor.g >> currentData.camClearColor.b >> currentData.camClearColor.a;
			}
			else if (key == "Letterbox") {
				std::stringstream(val) >> currentData.camLetterbox;
			}
			else if (key == "Pillarbox") {
				std::stringstream(val) >> currentData.camPillarbox;
			}
		}
		else if (currentSubBlock == "CinematicCam") {
			if (key == "Target") currentData.cinematicCamTarget = val;
			else if (key == "Distance") std::stringstream(val) >> currentData.cinematicCamDist;
			else if (key == "Height") std::stringstream(val) >> currentData.cinematicCamHeight;
			else if (key == "Pitch") std::stringstream(val) >> currentData.cinematicCamPitch;
			else if (key == "PositionLag") std::stringstream(val) >> currentData.cinematicCamPosLag;
			else if (key == "RotationLag") std::stringstream(val) >> currentData.cinematicCamRotLag;
			else if (key == "DriftCompensation") std::stringstream(val) >> currentData.cinematicCamDriftComp;
			else if (key == "DynamicFOV") {
				int d = 1;
				std::stringstream(val) >> d;
				currentData.cinematicCamDynamicFOV = (d != 0);
			}
			else if (key == "BaseFOV") std::stringstream(val) >> currentData.cinematicCamBaseFOV;
			else if (key == "BoostFOV") std::stringstream(val) >> currentData.cinematicCamBoostFOV;
		}
		else if (currentSubBlock == "KeyframeAnim") {
			if (key == "ClipPath") currentData.keyframeClipPath = val;
			else if (key == "PlayOnStart") { int p = 1; std::stringstream(val) >> p; currentData.keyframePlayOnStart = (p != 0); }
			else if (key == "Loop") { int l = 0; std::stringstream(val) >> l; currentData.keyframeLoop = (l != 0); }
			else if (key == "Speed") std::stringstream(val) >> currentData.keyframeSpeed;
		}
		else if (currentSubBlock == "Sprite") {
			if (key == "TexturePath") currentData.spriteTexPath = val;
			else if (key == "Color") {
				std::stringstream vs(val);
				vs >> currentData.spriteColor.r >> currentData.spriteColor.g >> currentData.spriteColor.b >> currentData.spriteColor.a;
			}
			else if (key == "FlipX") { int v = 0; std::stringstream(val) >> v; currentData.spriteFlipX = (v != 0); }
			else if (key == "FlipY") { int v = 0; std::stringstream(val) >> v; currentData.spriteFlipY = (v != 0); }
			else if (key == "SortingOrder") std::stringstream(val) >> currentData.spriteSortingOrder;
			else if (key == "SortingLayer") currentData.spriteSortingLayer = val;
			else if (key == "PixelsPerUnit") std::stringstream(val) >> currentData.spritePPU;
			else if (key == "HFrames") std::stringstream(val) >> currentData.spriteHFrames;
			else if (key == "VFrames") std::stringstream(val) >> currentData.spriteVFrames;
			else if (key == "CurrentFrame") std::stringstream(val) >> currentData.spriteCurrentFrame;
			else if (key == "IsLit") { int l = 0; std::stringstream(val) >> l; currentData.spriteIsLit = (l != 0); }
			else if (key == "IsPixelArt") { int p = 1; std::stringstream(val) >> p; currentData.spriteIsPixelArt = (p != 0); }
			else if (key == "NormalMapPath") currentData.spriteNormalMapPath = val;
		}
		else if (currentSubBlock == "SpriteAnimator") {
			if (key == "PlayOnStart") { int p = 1; std::stringstream(val) >> p; currentData.saPlayOnStart = (p != 0); }
			else if (key == "DefaultClip") currentData.saDefaultClip = val;
			else if (key == "ClipData") {
				std::stringstream vs(val);
				std::string cname;
				int sFrame = 0, eFrame = 0;
				float fps = 12.0f;
				int loop = 1;
				vs >> cname >> sFrame >> eFrame >> fps >> loop;
				if (!cname.empty()) {
					SpriteAnimationClip c;
					c.name = cname;
					c.startFrame = sFrame;
					c.endFrame = eFrame;
					c.frameRate = fps;
					c.loop = (loop != 0);
					currentData.saClips.push_back(c);
				}
			}
		}
		else if (currentSubBlock == "Box2D") {
			if (key == "Size") { std::stringstream vs(val); vs >> currentData.box2dSize.x >> currentData.box2dSize.y; }
			else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.box2dOffset.x >> currentData.box2dOffset.y; }
			else if (key == "IsTrigger") { int t = 0; std::stringstream(val) >> t; currentData.box2dIsTrigger = (t != 0); }
			else if (key == "Density") std::stringstream(val) >> currentData.box2dDensity;
			else if (key == "Friction") std::stringstream(val) >> currentData.box2dFriction;
			else if (key == "Restitution") std::stringstream(val) >> currentData.box2dRestitution;
		}
		else if (currentSubBlock == "Circle2D") {
			if (key == "Radius") std::stringstream(val) >> currentData.circ2dRadius;
			else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.circ2dOffset.x >> currentData.circ2dOffset.y; }
			else if (key == "IsTrigger") { int t = 0; std::stringstream(val) >> t; currentData.circ2dIsTrigger = (t != 0); }
			else if (key == "Density") std::stringstream(val) >> currentData.circ2dDensity;
			else if (key == "Friction") std::stringstream(val) >> currentData.circ2dFriction;
			else if (key == "Restitution") std::stringstream(val) >> currentData.circ2dRestitution;
		}
		else if (currentSubBlock == "RigidBody2D") {
			if (key == "BodyType") std::stringstream(val) >> currentData.rb2dBodyType;
			else if (key == "Mass") std::stringstream(val) >> currentData.rb2dMass;
			else if (key == "GravityScale") std::stringstream(val) >> currentData.rb2dGravityScale;
			else if (key == "FreezeRotation") { int f = 0; std::stringstream(val) >> f; currentData.rb2dFreezeRotation = (f != 0); }
			else if (key == "LinearDamping") std::stringstream(val) >> currentData.rb2dLinearDamping;
			else if (key == "AngularDamping") std::stringstream(val) >> currentData.rb2dAngularDamping;
		}
		else if (currentSubBlock == "Light2D") {
			if (key == "LightType") std::stringstream(val) >> currentData.l2dType;
			else if (key == "Color") { std::stringstream vs(val); vs >> currentData.l2dColor.r >> currentData.l2dColor.g >> currentData.l2dColor.b; }
			else if (key == "Intensity") std::stringstream(val) >> currentData.l2dIntensity;
			else if (key == "Radius") std::stringstream(val) >> currentData.l2dRadius;
			else if (key == "InnerRadius") std::stringstream(val) >> currentData.l2dInnerRadius;
			else if (key == "SpotAngle") std::stringstream(val) >> currentData.l2dSpotAngle;
			else if (key == "CastShadows") { int s = 1; std::stringstream(val) >> s; currentData.l2dCastShadows = (s != 0); }
			else if (key == "ShadowIntensity") std::stringstream(val) >> currentData.l2dShadowIntensity;
		}
		else if (currentSubBlock == "ShadowCaster2D") {
			if (key == "Shape") std::stringstream(val) >> currentData.sc2dShape;
			else if (key == "Size") { std::stringstream vs(val); vs >> currentData.sc2dSize.x >> currentData.sc2dSize.y; }
			else if (key == "Radius") std::stringstream(val) >> currentData.sc2dRadius;
			else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.sc2dOffset.x >> currentData.sc2dOffset.y; }
			else if (key == "CastShadows") { int s = 1; std::stringstream(val) >> s; currentData.sc2dCastShadows = (s != 0); }
		}
		else if (currentSubBlock == "TileGrid") {
			if (key == "CellSize") { std::stringstream vs(val); vs >> currentData.tgCellSize.x >> currentData.tgCellSize.y; }
			else if (key == "Anchor") std::stringstream(val) >> currentData.tgAnchor;
			else if (key == "TileSheetPath") currentData.tgTileSheetPath = val;
			else if (key == "SheetHFrames") std::stringstream(val) >> currentData.tgSheetHFrames;
			else if (key == "SheetVFrames") std::stringstream(val) >> currentData.tgSheetVFrames;
			else if (key == "SortingLayer") currentData.tgSortingLayer = val;
			else if (key == "SortingOrder") std::stringstream(val) >> currentData.tgSortingOrder;
			else if (key == "IsPixelArt") { int p = 1; std::stringstream(val) >> p; currentData.tgIsPixelArt = (p != 0); }
			else if (key == "TileData") {
				std::stringstream vs(val);
				int tx = 0, ty = 0, tid = 0;
				if (vs >> tx >> ty >> tid) {
					currentData.tgTiles[std::make_pair(tx, ty)] = tid;
				}
			}
		}
		else if (currentSubBlock == "Canvas") {
			if (key == "RenderMode") currentData.canvasRenderMode = val;
			else if (key == "ReferenceResolution") {
				std::stringstream vs(val);
				vs >> currentData.canvasRefRes.x >> currentData.canvasRefRes.y;
			}
			else if (key == "ScaleWithScreenSize") { int v = 1; std::stringstream(val) >> v; currentData.canvasScaleWithScreen = (v != 0); }
			else if (key == "SortingOrder") std::stringstream(val) >> currentData.canvasSortingOrder;
		}
		else if (currentSubBlock == "Rect") {
			if (key == "AnchorPreset") currentData.rectAnchorPreset = val;
			else if (key == "AnchoredPosition") {
				std::stringstream vs(val);
				vs >> currentData.rectAnchoredPos.x >> currentData.rectAnchoredPos.y;
			}
			else if (key == "Size") {
				std::stringstream vs(val);
				vs >> currentData.rectSize.x >> currentData.rectSize.y;
			}
			else if (key == "Pivot") {
				std::stringstream vs(val);
				vs >> currentData.rectPivot.x >> currentData.rectPivot.y;
			}
		}
		else if (currentSubBlock == "UIImage") {
			if (key == "TexturePath") currentData.uiImgTexPath = val;
			else if (key == "Color") {
				std::stringstream vs(val);
				vs >> currentData.uiImgColor.r >> currentData.uiImgColor.g >> currentData.uiImgColor.b >> currentData.uiImgColor.a;
			}
			else if (key == "PreserveAspect") { int v = 0; std::stringstream(val) >> v; currentData.uiImgPreserveAspect = (v != 0); }
		}
		else if (currentSubBlock == "UIText") {
			if (key == "Text") currentData.uiTxtText = val;
			else if (key == "FontPath") currentData.uiTxtFontPath = val;
			else if (key == "FontSize") std::stringstream(val) >> currentData.uiTxtFontSize;
			else if (key == "Color") {
				std::stringstream vs(val);
				vs >> currentData.uiTxtColor.r >> currentData.uiTxtColor.g >> currentData.uiTxtColor.b >> currentData.uiTxtColor.a;
			}
			else if (key == "AlignH") std::stringstream(val) >> currentData.uiTxtAlignH;
			else if (key == "AlignV") std::stringstream(val) >> currentData.uiTxtAlignV;
		}
		else if (currentSubBlock == "UIButton") {
			if (key == "Interactable") { int v = 1; std::stringstream(val) >> v; currentData.uiBtnInteractable = (v != 0); }
			else if (key == "NormalColor") { std::stringstream vs(val); vs >> currentData.uiBtnNormalCol.r >> currentData.uiBtnNormalCol.g >> currentData.uiBtnNormalCol.b >> currentData.uiBtnNormalCol.a; }
			else if (key == "HoverColor") { std::stringstream vs(val); vs >> currentData.uiBtnHoverCol.r >> currentData.uiBtnHoverCol.g >> currentData.uiBtnHoverCol.b >> currentData.uiBtnHoverCol.a; }
			else if (key == "PressedColor") { std::stringstream vs(val); vs >> currentData.uiBtnPressedCol.r >> currentData.uiBtnPressedCol.g >> currentData.uiBtnPressedCol.b >> currentData.uiBtnPressedCol.a; }
			else if (key == "DisabledColor") { std::stringstream vs(val); vs >> currentData.uiBtnDisabledCol.r >> currentData.uiBtnDisabledCol.g >> currentData.uiBtnDisabledCol.b >> currentData.uiBtnDisabledCol.a; }
			else if (key == "TargetEntity") currentData.uiBtnTargetEnt = val;
			else if (key == "OnClickFunction") currentData.uiBtnOnClickFn = val;
		}
		else if (currentSubBlock == "UISlider") {
			if (key == "Interactable") { int v = 1; std::stringstream(val) >> v; currentData.uiSldInteractable = (v != 0); }
			else if (key == "MinValue") std::stringstream(val) >> currentData.uiSldMin;
			else if (key == "MaxValue") std::stringstream(val) >> currentData.uiSldMax;
			else if (key == "Value") std::stringstream(val) >> currentData.uiSldVal;
			else if (key == "WholeNumbers") { int v = 0; std::stringstream(val) >> v; currentData.uiSldWhole = (v != 0); }
			else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiSldBgCol.r >> currentData.uiSldBgCol.g >> currentData.uiSldBgCol.b >> currentData.uiSldBgCol.a; }
			else if (key == "FillColor") { std::stringstream vs(val); vs >> currentData.uiSldFillCol.r >> currentData.uiSldFillCol.g >> currentData.uiSldFillCol.b >> currentData.uiSldFillCol.a; }
			else if (key == "HandleColor") { std::stringstream vs(val); vs >> currentData.uiSldHandleCol.r >> currentData.uiSldHandleCol.g >> currentData.uiSldHandleCol.b >> currentData.uiSldHandleCol.a; }
			else if (key == "TargetEntity") currentData.uiSldTargetEnt = val;
			else if (key == "OnValueChangedFunction") currentData.uiSldOnValChangedFn = val;
		}
		else if (currentSubBlock == "UIProgressBar") {
			if (key == "MinValue") std::stringstream(val) >> currentData.uiPbMin;
			else if (key == "MaxValue") std::stringstream(val) >> currentData.uiPbMax;
			else if (key == "Value") std::stringstream(val) >> currentData.uiPbVal;
			else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiPbBgCol.r >> currentData.uiPbBgCol.g >> currentData.uiPbBgCol.b >> currentData.uiPbBgCol.a; }
			else if (key == "FillColor") { std::stringstream vs(val); vs >> currentData.uiPbFillCol.r >> currentData.uiPbFillCol.g >> currentData.uiPbFillCol.b >> currentData.uiPbFillCol.a; }
			else if (key == "ShowPercentageText") { int v = 1; std::stringstream(val) >> v; currentData.uiPbShowPct = (v != 0); }
		}
		else if (currentSubBlock == "UIJoystick") {
			if (key == "StickName") currentData.uiJoyStickName = val;
			else if (key == "Mode") std::stringstream(val) >> currentData.uiJoyMode;
			else if (key == "TravelRadius") std::stringstream(val) >> currentData.uiJoyTravel;
			else if (key == "ActivationRadius") std::stringstream(val) >> currentData.uiJoyActivation;
			else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiJoyBg.r >> currentData.uiJoyBg.g >> currentData.uiJoyBg.b >> currentData.uiJoyBg.a; }
			else if (key == "KnobColor") { std::stringstream vs(val); vs >> currentData.uiJoyKnob.r >> currentData.uiJoyKnob.g >> currentData.uiJoyKnob.b >> currentData.uiJoyKnob.a; }
			else if (key == "Interactable") { int v = 1; std::stringstream(val) >> v; currentData.uiJoyInteractable = (v != 0); }
		}
		else if (currentSubBlock == "UIActionButton") {
			if (key == "ActionName") currentData.uiActName = val;
			else if (key == "Interactable") { int v = 1; std::stringstream(val) >> v; currentData.uiActInteractable = (v != 0); }
			else if (key == "NormalColor") { std::stringstream vs(val); vs >> currentData.uiActNormal.r >> currentData.uiActNormal.g >> currentData.uiActNormal.b >> currentData.uiActNormal.a; }
			else if (key == "PressedColor") { std::stringstream vs(val); vs >> currentData.uiActPressed.r >> currentData.uiActPressed.g >> currentData.uiActPressed.b >> currentData.uiActPressed.a; }
		}
		else {
			// Bloque raíz de Entity
			if (key == "Id" || key == "ID") {
				std::stringstream(val) >> currentData.id;
			}
			else if (key == "ParentId" || key == "ParentID") {
				std::stringstream(val) >> currentData.parentId;
			}
			else if (key == "Name") currentData.name = val;
			else if (key == "Static") currentData.isStatic = (val == "1" || val == "true" || val == "True");
			else if (key == "Type") currentData.type = val;
			else if (key == "PrimitiveType") currentData.primType = val;
			else if (key == "Parent") currentData.parentName = val;
			else if (key == "Position") {
				std::stringstream valStream(val);
				valStream >> currentData.pos.x >> currentData.pos.y >> currentData.pos.z;
			}
			else if (key == "Rotation") {
				std::stringstream valStream(val);
				valStream >> currentData.rot.x >> currentData.rot.y >> currentData.rot.z;
			}
			else if (key == "Scale") {
				std::stringstream valStream(val);
				valStream >> currentData.scale.x >> currentData.scale.y >> currentData.scale.z;
			}
			else if (key == "InheritScale") {
				int isVal = 1;
				std::stringstream valStream(val);
				if (valStream >> isVal) {
					currentData.inheritScale = (isVal != 0);
				}
			}
		}
	}

	finishEntity();

	// Enlazar relaciones padre-hijo cargadas
	for (auto& link : parentLinks) {
		SlipEntity* child = link.child;
		if (!child) continue;

		SlipEntity* parent = nullptr;
		// 1. Vinculación directa y exacta por ParentId (garantiza jerarquía incluso si tienen el mismo nombre)
		if (link.parentId != 0) {
			auto it = idToEntity.find(link.parentId);
			if (it != idToEntity.end() && it->second != child) {
				parent = it->second;
			}
		}

		// 2. Fallback por nombre si no hay parentId (compatibilidad hacia atrás con escenas legacy)
		if (!parent && !link.parentName.empty()) {
			for (SlipEntity* candidate : entities) {
				if (candidate && candidate != child && candidate->GetName() == link.parentName) {
					parent = candidate;
					break;
				}
			}
		}

		if (parent) {
			child->SetParent(parent);
		}
	}

	return true;
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
	m_Instance = this;
}

CameraComponent* SlipLevel::GetMainCamera() const
{
	CameraComponent* firstCam = nullptr;
	for (SlipEntity* ent : entities) {
		if (!ent) continue;
		auto cam = ent->GetComponent<CameraComponent>();
		if (cam && cam->IsEnabled()) {
			if (cam->isPrimary) return cam.get();
			if (!firstCam) firstCam = cam.get();
		}
	}
	return firstCam;
}

static SlipLevel::EntityDestroyedCallback s_OnEntityDestroyedCb = nullptr;

void SlipLevel::SetOnEntityDestroyedCallback(EntityDestroyedCallback cb)
{
	s_OnEntityDestroyedCb = cb;
}

void SlipLevel::DestroyEntity(SlipEntity* root)
{
	if (!root) return;

	// 1. Recolectar jerarquía completa (hijos, nietos, etc.)
	std::vector<SlipEntity*> toDestroy;
	root->GetAllChildren(toDestroy);
	toDestroy.push_back(root);

	// 2. Notificar observadores antes de liberar memoria
	for (SlipEntity* ent : toDestroy) {
		if (s_OnEntityDestroyedCb) {
			s_OnEntityDestroyedCb(ent);
		}
	}

	// 3. Desvincular raíz de su padre
	root->SetParent(nullptr);

	// 4. Limpiar cualquier referencia en SkinnedMeshComponents
	for (SlipEntity* e : entities) {
		if (!e) continue;
		if (auto smc = e->GetComponent<SkinnedMeshComponent>()) {
			for (SlipEntity* deadEnt : toDestroy) {
				for (auto& bEnt : smc->boneEntities) {
					if (bEnt == deadEnt) bEnt = nullptr;
				}
			}
		}
	}

	// 5. Remover de la lista de entidades del nivel y liberar memoria
	for (SlipEntity* deadEnt : toDestroy) {
		entities.erase(std::remove(entities.begin(), entities.end(), deadEnt), entities.end());
		delete deadEnt;
	}
}
