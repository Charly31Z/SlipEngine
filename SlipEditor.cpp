#include "SlipEditor.h"

#include <filesystem>
#include <assert.h>

#include "FileDialog.h"

#include "SlipWindow.h"
#include "SlipActor.h"
#include "SlipShadows.h"
#include "SlipGlobals.h"

#include "SlipNavMesh.h"

#include "SlipTexture.h"

#include "SlipID.h"

bool EntitiesGetter(void* data, int index, const char** out_text)
{
    SlipEntity* ents = (SlipEntity*)data;
    SlipEntity& current_ent = ents[index];

    if (SlipActor* p = dynamic_cast<SlipActor*>(&current_ent))
        *out_text = p->modelPath; // not very safe

    return true;
}

SlipEditor::SlipEditor()
{
    assert(!m_Instante && "SlipEditor has initialized...");
    m_Instante = this;
}

void SlipEditor::init()
{
    //SETUP IMGUI
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForOpenGL(&SlipWindow::Get().getWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGui::StyleColorsDark();

    camera = new SlipCamera();
}

void SlipEditor::startRender()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New"))
            {
                prop = SlipEditor::LEVEL;
                newLevel();
            }
            if (ImGui::MenuItem("Open"))
            {
                prop = SlipEditor::LEVEL;
                openLevel();
            }
            if (ImGui::MenuItem("Save"))
            {
                saveLevel();
            }
            if (ImGui::MenuItem("Exit"))
            {
                glfwDestroyWindow(&SlipWindow::Get().getWindow());
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Scene"))
            {
                scene = true;
            }
            if (ImGui::MenuItem("Hierarchy"))
            {
                hierarchy = true;
            }
            if (ImGui::MenuItem("Properties"))
            {
                properties = true;
            }
            if (ImGui::MenuItem("Debug"))
            {
                debug = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::MenuItem("Generate Nav Mesh"))
            {
                SlipNavMesh::Get().init();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("ToolBar", &toolbar, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(0, 18.f));
    ImGui::SetWindowSize(ImVec2(20.f, SlipWindow::Get().getHeight()));

    if (ImGui::Button("T##ImGuizmo"))
    {
        mCurrentGizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
    }
    if (ImGui::Button("R##ImGuizmo"))
    {
        mCurrentGizmoOperation = ImGuizmo::OPERATION::ROTATE;
    }
    if (ImGui::Button("S##ImGuizmo"))
    {
        mCurrentGizmoOperation = ImGuizmo::OPERATION::SCALE;
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().ItemSpacing.y * ImGui::GetStyle().FramePadding.y * ImGui::GetStyle().WindowPadding.y);

    ImGui::Separator();

    if (ImGui::ImageButton((ImTextureID)icons.find("play")->second->getId(), ImVec2(10, 10)))
    {
        playMode = !playMode;
        if (playMode)
        {
            SlipPhysics::Get().setGravity(glm::vec3(0.f, -9.81f, 0.f));
        }
        else {
            SlipPhysics::Get().setGravity(glm::vec3(0.f, 0.f, 0.f));
        }
        
    }

    ImGui::End();
}

void SlipEditor::loadIcons()
{
    SlipTexture* temp = new SlipTexture();

    icons.insert({"play", temp->load("resources/slipspace_icon_play.png")});
}

void SlipEditor::renderGuizmo()
{
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0, 0, SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight());

    
    //ImGuizmo::ViewManipulate((float*)glm::value_ptr(SlipGlobals::Get().getView()), glm::length(SlipGlobals::Get().getCamera().position - SlipLevel::Get().spawns[spawnSelected]->position), ImVec2(SlipWindow::Get().getWidth() - 128, SlipWindow::Get().getHeight()), ImVec2(128, 128), 0x10101010);
}

void SlipEditor::renderHierarchy()
{
    if (hierarchy)
    {
        if (!ImGui::Begin("Hierarchy", &hierarchy))
        {
            ImGui::End();
        }
        else
        {
            if (ImGui::TreeNode("Spawns"))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

                if (ImGui::IsItemClicked()) {
                    existSpawn = false;
                    prop = SlipEditor::SPAWN;
                }

                for (int i = 0; i < SlipLevel::Get().spawns.size(); i++)
                {
                    ImGuiTreeNodeFlags node_flags;

                    if (spawnSelected == i)
                        node_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
                    else
                        node_flags = ImGuiTreeNodeFlags_Leaf;

                    std::string id = std::to_string(SlipLevel::Get().spawns[i]->team);

                    bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, id.c_str());

                    if (ImGui::IsItemClicked()) {
                        spawnSelected = i;
                        existSpawn = true;
                        prop = property_type::SPAWN;
                        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(SlipLevel::Get().spawns[spawnSelected]->position), glm::value_ptr(SlipLevel::Get().spawns[spawnSelected]->rotation), glm::value_ptr(glm::vec3(1.f)), glm::value_ptr(matrix));
                    }

                    if (node_open)
                    {
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
                ImGui::PopStyleVar();
            }

            if (ImGui::TreeNode("Entities"))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

                if (ImGui::IsItemClicked()) {
                    existEnity = false;
                    entity_mesh = NULL;
                    prop = SlipEditor::ENTITY;
                }

                for (int i = 0; i < SlipLevel::Get().entities.size(); i++)
                {
                    ImGuiTreeNodeFlags node_flags;

                    if (entitySelected == i)
                        node_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
                    else
                        node_flags = ImGuiTreeNodeFlags_Leaf;

                    std::string id = std::to_string(SlipLevel::Get().entities[i]->GetId());

                    bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, id.c_str());

                    if (ImGui::IsItemClicked()) {
                        entitySelected = i;
                        existEnity = true;
                        prop = property_type::ENTITY;
                        matrix = SlipLevel::Get().entities[entitySelected]->getMatrix();
                    }

                    if (node_open)
                    {
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
                ImGui::PopStyleVar();
            }

            /*if (ImGui::TreeNode("User Interfaces"))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));
                for (int i = 0; i < uis.size(); i++)
                {
                    ImGuiTreeNodeFlags node_flags;

                    if (uiSelected == i)
                        node_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
                    else
                        node_flags = ImGuiTreeNodeFlags_Leaf;
                    bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, uis[i].name.c_str());

                    if (ImGui::IsItemClicked()) {
                        uiSelected = i;
                        prop = property_type::UI;
                    }

                    if (node_open)
                    {
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
                ImGui::PopStyleVar();
            }*/

            ImGui::End();
        }
    }
}

void SlipEditor::renderProperties()
{
    if (properties)
    {
        if (!ImGui::Begin("Properties", &properties))
        {
            ImGui::End();
        }
        else {
            switch (prop)
            {
            case SlipEditor::LEVEL:
            {
                ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s", SlipLevel::Get().path.c_str());

                ImGui::Text("Bsp path (.bsp_cache)");
                ImGui::InputText("##inputbsppath", SlipLevel::Get().bspPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttonbspopen"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip BSP (*.bsp_cache)\0*.bsp_cache\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 7);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().bspPath, file.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("GoTo##buttonbspgo"))
                {
                    prop = SlipEditor::BSP;
                }

                ImGui::Text("Terrain heightfield path (.png)");
                ImGui::InputText("##inputterrainimg", SlipLevel::Get().terrain->heightfieldIMG, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttonterrainimgopen"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Terrain heightmap (*.png)\0*.png\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 1);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().terrain->heightfieldIMG, file.c_str());
                }

                ImGui::Text("Terrain collision path (.physics_cache)");
                ImGui::InputText("##inputterraincol", SlipLevel::Get().terrain->collisionPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttonterraincolopen"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip Collision (*.physics_cache)\0*.physics_cache\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 7);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().terrain->collisionPath, file.c_str());
                }
                //ImGui::SameLine();
                //if (ImGui::Button("GoTo##buttonterraincolgo"))
                //{
                //    prop = SlipEditor::MODEL;
                //}

                if (ImGui::Button("Apply##buttonlevelapply"))
                {
                    SlipLevel::Get().apply();
                }
            }
            break;
            /*case SlipEditor::BSP:
            {
                ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().levelName.c_str(), ".bsp_cache");

                ImGui::Text("SkyBox path (.model_cache)");
                ImGui::InputText(".sk", SlipLevel::Get().mBsp->skyboxPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...s"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip Model (*.model_cache)\0*.model_cache\0");

                    std::string mystr = selected_path.substr(selected_path.find("cache"));

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().mBsp->skyboxPath, file.c_str());
                }

                if (ImGui::Button("Save"))
                {
                    extractor.save(*SlipLevel::Get().mBsp, SlipLevel::Get().bsp);
                }
            }
            break;*/
            case SlipEditor::MODEL:
            {
                ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType]->path.c_str(), ".model_cache");

                ImGui::Text("Collision path (.physics_cache)");
                ImGui::InputText("##inputcollpath", SlipLevel::Get().models[selectedType]->colPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttoncollopen"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip Physics (*.physics_cache)\0*.physics_cache\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 7);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().models[selectedType]->colPath, file.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("GoTo##buttoncollgo"))
                {
                    prop = SlipEditor::COLLISION;
                    break;
                }

                materials_name = std::filesystem::path(SlipLevel::Get().models[selectedType]->materials[matSelected].matPath).stem().string();

                if (ImGui::BeginCombo("Materials", materials_name.c_str()))
                {
                    for (int n = 0; n < SlipLevel::Get().models[selectedType]->materials.size(); n++)
                    {
                        std::filesystem::path path(SlipLevel::Get().models[selectedType]->materials[n].matPath);

                        bool is_selected = (matSelected == n);
                        if (ImGui::Selectable(path.stem().string().c_str(), is_selected))
                        {
                            matSelected = n;
                            materials_name = path.stem().string();
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Text("Material path (.material_cache)");
                ImGui::InputText("##inputmatpath", SlipLevel::Get().models[selectedType]->materials[matSelected].matPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttonmatopen"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip Material (*.material_cache)\0*.material_cache\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 7);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().models[selectedType]->materials[0].matPath, file.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("GoTo##buttonmatgo"))
                {
                    prop = SlipEditor::MATERIAL;
                    break;
                }

                if (ImGui::Button("Save"))
                {
                    extractor.save(*SlipLevel::Get().models[selectedType], SlipLevel::Get().models[selectedType]->path);
                }
            }
            break;
            case SlipEditor::COLLISION:
            {
                ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType]->colPath, ".physics_cache");
                ImGui::Text("Mass (KG)");
                ImGui::InputInt("##inputcollmass", &SlipLevel::Get().models[selectedType]->collision->mass);

                if (ImGui::Button("Save"))
                {
                    extractor.save(*SlipLevel::Get().models[selectedType]->collision, SlipLevel::Get().models[selectedType]->colPath);
                }
            }
            break;
            case SlipEditor::MATERIAL:
            {
                ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType]->materials[0].matPath, ".material_cache");
                ImGui::Text("Shader type");
                ImGui::InputText("##inputmattype", SlipLevel::Get().models[selectedType]->materials[0].mat->shaderType, 4);
                ImGui::Text("Shininess");
                ImGui::InputFloat("##inputmatshin", &SlipLevel::Get().models[selectedType]->materials[0].mat->shininess);
                ImGui::Text("Diffuse path (.texture_cache)");
                ImGui::InputText("##inputmatdiffpath", SlipLevel::Get().models[selectedType]->materials[0].mat->diffPath, 192);
                ImGui::SameLine();
                if (ImGui::Button("...##buttonmatopen1"))
                {
                    std::string selected_path = IO::FileDialog::OpenFile("Slip Texture (*.texture_cache)\0*.texture_cache\0");

                    std::replace(selected_path.begin(), selected_path.end(), '\\', '/');

                    std::string mystr = selected_path.substr(selected_path.find("/cache/") + 7);

                    std::filesystem::path path(mystr);

                    std::string file = path.parent_path().string() + "/" + path.stem().string();
                    std::strcpy(SlipLevel::Get().models[selectedType]->materials[0].mat->diffPath, file.c_str());
                }

                if (ImGui::Button("Save"))
                {
                    extractor.save(*SlipLevel::Get().models[selectedType]->materials[0].mat, SlipLevel::Get().models[selectedType]->materials[0].mat->path);
                }
            }
            break;
            case SlipEditor::SPAWN:
            {
                if (existSpawn)
                {
                    ImGuizmo::Enable(true);

                    glm::vec3 scale{ 1.f };

                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), glm::value_ptr(posGuizmo), glm::value_ptr(rotGuizmo), glm::value_ptr(scaGuizmo));

                    ImGui::InputFloat3("Translate", glm::value_ptr(posGuizmo));
                    ImGui::InputFloat3("Rotate", glm::value_ptr(rotGuizmo));

                    ImGui::InputInt("Team", &SlipLevel::Get().spawns[spawnSelected]->team);

                    ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(posGuizmo), glm::value_ptr(rotGuizmo), glm::value_ptr(scale), glm::value_ptr(matrix));

                    SlipLevel::Get().spawns[spawnSelected]->position = posGuizmo;
                    SlipLevel::Get().spawns[spawnSelected]->rotation = rotGuizmo;

                    ImGuizmo::Manipulate(glm::value_ptr(SlipGlobals::Get().getView()), glm::value_ptr(SlipGlobals::Get().getProjection()), mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(matrix), NULL, useSnap ? &snap.x : NULL);

                    //ImGuizmo::ViewManipulate((float*)glm::value_ptr(SlipGlobals::Get().getView()), glm::length(SlipGlobals::Get().getCamera().position - SlipLevel::Get().spawns[spawnSelected]->position), ImVec2(SlipWindow::Get().getWidth() - 128, SlipWindow::Get().getHeight()), ImVec2(128, 128), 0x10101010);
                }
                else {
                    ImGui::InputFloat3("Translate", posSpa);
                    ImGui::InputFloat3("Rotate", rotSpa);

                    ImGui::InputInt("Team", &teamSpa);

                    if (ImGui::Button("Create##createspawn"))
                    {
                        SlipSpawn* spa = new SlipSpawn(teamSpa, glm::vec3(posSpa[0], posSpa[1], posSpa[2]), glm::vec3(rotSpa[0], rotSpa[1], rotSpa[2]));
                        SlipLevel::Get().spawns.push_back(spa);
                    }
                }
            }
            break;
            case SlipEditor::ENTITY:
            {
                if (!existEnity)
                {
                    if (ImGui::BeginCombo("modelPath", entity_mesh))
                    {
                        for (int n = 0; n < SlipLevel::Get().models.size(); n++)
                        {
                            bool is_selected = (entity_mesh == SlipLevel::Get().models[n]->path.c_str()); // You can store your selection however you want, outside or inside your objects
                            if (ImGui::Selectable(SlipLevel::Get().models[n]->path.c_str(), is_selected))
                                entity_mesh = SlipLevel::Get().models[n]->path.c_str();
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::InputFloat3("Translate", posEnt);
                    ImGui::InputFloat3("Rotate", rotEnt);
                    ImGui::InputFloat3("Scale", scaEnt);

                    if (ImGui::Button("Create Entitie##createentitie"))
                    {
                        SlipActor* ent = new SlipActor(entity_mesh);
                        ent->position = glm::vec3(posEnt[0], posEnt[1], posEnt[2]);
                        ent->rotation = glm::vec3(rotEnt[0], rotEnt[1], rotEnt[2]);
                        ent->scale = glm::vec3(scaEnt[0], scaEnt[1], scaEnt[2]);

                        ent->init();
                        SlipLevel::Get().entities.push_back(ent);
                    }
                }
                else
                {
                    if (!playMode)
                    {
                        ImGuizmo::Enable(true);

                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), glm::value_ptr(posGuizmo), glm::value_ptr(rotGuizmo), glm::value_ptr(scaGuizmo));

                        ImGui::InputFloat3("Translate", glm::value_ptr(posGuizmo));
                        ImGui::InputFloat3("Rotate", glm::value_ptr(rotGuizmo));
                        ImGui::InputFloat3("Scale", glm::value_ptr(scaGuizmo));

                        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(posGuizmo), glm::value_ptr(rotGuizmo), glm::value_ptr(scaGuizmo), glm::value_ptr(matrix));

                        ImGuizmo::Manipulate(glm::value_ptr(SlipGlobals::Get().getView()), glm::value_ptr(SlipGlobals::Get().getProjection()), mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(matrix), NULL, useSnap ? &snap.x : NULL);

                        SlipLevel::Get().entities[entitySelected]->position = posGuizmo;
                        SlipLevel::Get().entities[entitySelected]->rotation = rotGuizmo;
                        SlipLevel::Get().entities[entitySelected]->scale = scaGuizmo;
                    }
                }
            }
            break;
            /*case SlipEditor::UI:
                {
                    float pos[2] = {
                        uis[uiSelected].position.x,
                        uis[uiSelected].position.y
                    };
                    float rot[2] = {
                        entities[entitySelected].rotation.x,
                        entities[entitySelected].rotation.y
                    };
                    float sca[2] = {
                        uis[uiSelected].scale.x,
                        uis[uiSelected].scale.y
                    };

                    ImGui::InputFloat2("Translate", pos);
                    //ImGui::SliderFloat2("Rotate", rot, 0.f, 3.f);
                    ImGui::SliderFloat2("Scale", sca, 0.f, 3.f);
                    uis[uiSelected].position = glm::vec2(pos[0], pos[1]);
                    //uis[uiSelected].rotation = glm::vec3(rot[0], rot[1], rot[2]);
                    uis[uiSelected].scale = glm::vec2(sca[0], sca[1]);
                }
                break;*/
            }

            ImGui::End();
        }
    }
}

void SlipEditor::renderViewport(SlipFrameBuffer* framebuffer)
{
    ImGui::Begin("Viewport", &game, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(SlipWindow::Get().getWidth() / 4.5, 0));
    ImGui::SetWindowSize(ImVec2(SlipWindow::Get().getWidth() / 1.8, SlipWindow::Get().getHeight() / 1.5));

    if (ImGui::Button(playBtn))
    {
        //playBtn = "Play";
    }

    ImGui::SetCursorPos((ImGui::GetWindowSize() - ImVec2(SlipWindow::Get().getWidth() / 1.85, SlipWindow::Get().getHeight() / 2.2)) * 0.5f);
    ImGui::Image((void*)(intptr_t)framebuffer->getTexture(), ImVec2(SlipWindow::Get().getWidth() / 1.85, SlipWindow::Get().getHeight() / 1.85), ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(1))
        mouseRClicked = true;
    else if (ImGui::IsMouseReleased(1))
        mouseRClicked = false;

    ImGui::End();
}

bool SlipEditor::mouseRPressed()
{
    return mouseRClicked;
}

void SlipEditor::renderScene()
{
    if (scene)
    {
        if (!ImGui::Begin("Scene", &scene, ImGuiWindowFlags_MenuBar))
        {
            ImGui::End();
        }
        else
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Create Bsp..", "Ctrl+I")) { createBsp = true; }
                    if (ImGui::MenuItem("Create Model..", "Ctrl+I")) { createModel = true; }
                    if (ImGui::MenuItem("Create Collision..", "Ctrl+I")) { createCol = true; }
                    if (ImGui::MenuItem("Create Texture..", "Ctrl+I")) { createTexture = true; }
                    if (ImGui::MenuItem("Import Pre-Cache..", "Ctrl+I")) { sceneImport = true; }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Create"))
                {
                    if (ImGui::MenuItem("Create Entity..", "Ctrl+I")) { createBsp = true; }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (createBsp)
            {
                extractor.extractBsp(IO::FileDialog::OpenFile("Objects 3D (*.obj)(*.fbx)\0*.obj;*.fbx\0"));
                createBsp = false;
            }

            if (createModel)
            {
                extractor.extract(IO::FileDialog::OpenFile("Objects 3D (*.obj)(*.fbx)\0*.obj;*.fbx\0"));
                createModel = false;
            }

            if (createCol)
            {
                extractor.extractCol(IO::FileDialog::OpenFile("Objects 3D (*.obj)(*.fbx)\0*.obj;*.fbx\0"));
                createCol = false;
            }

            if (createTexture)
            {
                extractor.extractTexture(IO::FileDialog::OpenFile("Image (*.png)(*.jpg)\0*.png;*.jpg;*.jpeg\0"));
                createTexture = false;
            }

            if (sceneImport)
            {
                extractor.extractFromPreCache(&SlipLevel::Get(), IO::FileDialog::OpenFile("Slip Model (*.model_cache)\0*.model_cache\0"));
                sceneImport = false;
            }

            if (createEntity)
            {

            }

            ImGui::BeginChild("General", ImVec2(SlipWindow::Get().getWidth() / 5.25, SlipWindow::Get().getHeight() / 3.5), true);
            if (ImGui::TreeNode(SlipLevel::Get().path.c_str()))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

                if (ImGui::IsItemClicked()) {
                    prop = SlipEditor::LEVEL;
                }

                ImGuiTreeNodeFlags modelNode_flags;
                ImGuiTreeNodeFlags uiNode_flags;

                if (sceneTypeSelected == 0)
                {
                    modelNode_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
                    uiNode_flags = ImGuiTreeNodeFlags_Leaf;
                }
                else if (sceneTypeSelected == 1)
                {
                    uiNode_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
                    modelNode_flags = ImGuiTreeNodeFlags_Leaf;
                }

                bool modelNode_open = ImGui::TreeNodeEx((void*)(intptr_t)0, modelNode_flags, "Models");

                if (ImGui::IsItemClicked()) {
                    sceneTypeSelected = 0;
                }

                if (modelNode_open)
                {
                    ImGui::TreePop();
                }

                bool uiNode_open = ImGui::TreeNodeEx((void*)(intptr_t)1, uiNode_flags, "User Interfaces");

                if (ImGui::IsItemClicked()) {
                    sceneTypeSelected = 1;
                }

                if (uiNode_open)
                {
                    ImGui::TreePop();
                }

                ImGui::TreePop();
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("In Scene", ImVec2(SlipWindow::Get().getWidth() / 1.75, SlipWindow::Get().getHeight() / 3.5), true);
            switch (sceneTypeSelected)
            {
            case 0:
            {
                for (int i = 0; i < SlipLevel::Get().models.size(); i++)
                {
                    ImGui::Text(SlipLevel::Get().models[i]->path.c_str());
                    if (ImGui::IsItemClicked()) {
                        selectedType = i;
                        prop = SlipEditor::MODEL;
                    }
                }
            }
            break;
            }
            ImGui::EndChild();

            ImGui::End();
        }
    }
}

void SlipEditor::renderDebug()
{
    if (debug)
    {
        if (!ImGui::Begin("Debug", &debug, ImGuiWindowFlags_MenuBar))
        {
            ImGui::End();
        }
        else
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("Options"))
                {
                    if (ImGui::MenuItem("Clear", "")) {
                        textItem.clear();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            std::printf("\r Debug window: %d", (int)ImGui::GetWindowHeight()/19);

            ImGui::PushItemWidth(-1);
            ImGui::ListBox("##listboxDebug", &current_textItem, [](void* data, int index, const char** out_text) {
                auto& items = *static_cast<std::vector<std::string>*>(data);
                *out_text = items[index].c_str();
                return true;
            }, static_cast<void*>(&textItem), textItem.size(), (int)ImGui::GetWindowHeight() / 22);
            ImGui::PopItemWidth();

            ImGui::End();
        }
    }
}

void SlipEditor::processInput()
{
    if (ImGui::IsKeyDown(ImGuiKey_W))
        camera->ProcessKeyboard(Camera_Movement::FORWARD, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_A))
        camera->ProcessKeyboard(Camera_Movement::LEFT, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_S))
        camera->ProcessKeyboard(Camera_Movement::BACKWARD, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_D))
        camera->ProcessKeyboard(Camera_Movement::RIGHT, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_E))
        camera->ProcessKeyboard(Camera_Movement::UP, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_Q))
        camera->ProcessKeyboard(Camera_Movement::DOWN, SlipGlobals::Get().GetDeltaTime());
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
        camera->MovementSpeed = 120.f;
    if (ImGui::IsKeyReleased(ImGuiKey_LeftShift))
        camera->MovementSpeed = 60.f;

    if (ImGui::IsKeyDown(ImGuiKey_F))
        wireframe = !wireframe;

    if (mouseRClicked)
    {
        glfwSetInputMode(&SlipWindow::Get().getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    else {
        glfwSetInputMode(&SlipWindow::Get().getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void SlipEditor::endRender()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void SlipEditor::newLevel()
{
    SlipLevel::Get().canDraw = false;

    SlipLevel::Get().path = "untilted";

    SlipLevel::Get().entities.clear();
    SlipLevel::Get().spawns.clear();

    std::fill(std::begin(SlipLevel::Get().bspPath), std::end(SlipLevel::Get().bspPath), '\0');

    SlipLevel::Get().models.clear();

    SlipLevel::Get().clean();

    SlipPhysics::Get().clean();
    SlipID::Get().reset();

    print("Loaded level: " + SlipLevel::Get().path);

    SlipLevel::Get().canDraw = true;
}

int SlipEditor::openLevel()
{

    SlipLevel::Get().path = IO::FileDialog::OpenFile("Level Cache (*.level_cache)\0 *.level_cache;");

    if (SlipLevel::Get().path.empty())
        return 0;

    SlipLevel::Get().canDraw = false;
    SlipLevel::Get().models.clear();
    SlipLevel::Get().entities.clear();
    SlipLevel::Get().spawns.clear();

    SlipPhysics::Get().clean();
    SlipID::Get().reset();

    //entities.push_back(m_Camera);

    std::ifstream input(SlipLevel::Get().path, std::ios::binary);

    IO::read(input, SlipLevel::Get().bspPath);

    if (std::strlen(SlipLevel::Get().bspPath) != 0)
    {
        SlipLevel::Get().bsp = new SlipBsp(SlipLevel::Get().bspPath);
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

        SlipLevel::Get().models.push_back(cacheMesh);
    }

    size_t spawnCount;
    IO::read(input, spawnCount);

    for (int i = 0; i < spawnCount; i++)
    {
        SlipSpawn* spa = new SlipSpawn(0, glm::vec3(), glm::vec3());
        spa->model = &SlipLevel::Get().debugModels[0];

        float juas[3];
        IO::read(input, juas);
        spa->position = glm::make_vec3(juas);

        IO::read(input, juas);
        spa->rotation = glm::make_vec3(juas);

        IO::read(input, spa->team);

        SlipLevel::Get().spawns.push_back(spa);
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
        strcpy(act->modelPath, SlipLevel::Get().models[model]->path.c_str());

        act->position = pos;
        act->rotation = rot;
        act->scale = sca;
        act->model = SlipLevel::Get().models[model];

        SlipLevel::Get().entities.push_back(act);
    }

    IO::read(input, SlipLevel::Get().terrain->heightfieldIMG);
    IO::read(input, SlipLevel::Get().terrain->collisionPath);

    SlipLevel::Get().terrain->apply();

    input.close();

    print("Loaded level: " + SlipLevel::Get().path);

    SlipLevel::Get().canDraw = true;

    return 1;
}

void SlipEditor::saveLevel()
{
    std::string savingPathFolder = IO::FileDialog::SaveFile("Level Cache (*.level_cache)\0 *.level_cache;");

    std::ofstream out(savingPathFolder, std::ios::binary);

    IO::write(out, SlipLevel::Get().bspPath);

    size_t modelCount = SlipLevel::Get().models.size();

    IO::write(out, modelCount);

    if (!SlipLevel::Get().models.empty())
    {
        for (int i = 0; i < modelCount; i++)
        {
            char cachePathMesh[192];
            strncpy(cachePathMesh, SlipLevel::Get().models[i]->path.c_str(), sizeof(cachePathMesh));
            IO::write(out, cachePathMesh);
        }
    }

    size_t spawnCount = SlipLevel::Get().spawns.size();

    IO::write(out, spawnCount);

    if (!SlipLevel::Get().spawns.empty())
    {
        for (int i = 0; i < spawnCount; i++)
        {
            float juas[3];

            juas[0] = SlipLevel::Get().spawns[i]->position.x;
            juas[1] = SlipLevel::Get().spawns[i]->position.y;
            juas[2] = SlipLevel::Get().spawns[i]->position.z;
            IO::write(out, juas);

            juas[0] = SlipLevel::Get().spawns[i]->rotation.x;
            juas[1] = SlipLevel::Get().spawns[i]->rotation.y;
            juas[2] = SlipLevel::Get().spawns[i]->rotation.z;
            IO::write(out, juas);
            IO::write(out, SlipLevel::Get().spawns[i]->team);
        }
    }

    size_t entitiesCount = SlipLevel::Get().entities.size();
    IO::write(out, entitiesCount);

    if (!SlipLevel::Get().entities.empty())
    {
        for (int i = 0; i < entitiesCount; i++)
        {
            float juas[3];

            juas[0] = SlipLevel::Get().entities[i]->position.x;
            juas[1] = SlipLevel::Get().entities[i]->position.y;
            juas[2] = SlipLevel::Get().entities[i]->position.z;
            IO::write(out, juas);

            juas[0] = SlipLevel::Get().entities[i]->rotation.x;
            juas[1] = SlipLevel::Get().entities[i]->rotation.y;
            juas[2] = SlipLevel::Get().entities[i]->rotation.z;
            IO::write(out, juas);

            juas[0] = SlipLevel::Get().entities[i]->scale.x;
            juas[1] = SlipLevel::Get().entities[i]->scale.y;
            juas[2] = SlipLevel::Get().entities[i]->scale.z;
            IO::write(out, juas);

            if (SlipActor* p = dynamic_cast<SlipActor*>(SlipLevel::Get().entities[i]))
            {
                for (int n = 0; n < modelCount; n++)
                {
                    if (p->modelPath == SlipLevel::Get().models[n]->path)
                    {
                        IO::write(out, n);
                    }
                }
            }
        }
    }

    IO::write(out, SlipLevel::Get().terrain->heightfieldIMG);
    IO::write(out, SlipLevel::Get().terrain->collisionPath);

    out.close();
}

void SlipEditor::print(std::string text)
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);

    strftime(buf, sizeof(buf), "%X", &tstruct);

    std::string esquemira = buf;

    std::string output = "[" + esquemira + "]: " + text;

    textItem.push_back(output);
}
