#include "SlipEditor.h"

#include <filesystem>
#include <assert.h>

#include "FileDialog.h"
#include "ImGuizmo.h"

#include "SlipWindow.h"
#include "SlipActor.h"

bool EntitiesGetter(void* data, int index, const char** out_text)
{
    SlipEntity* ents = (SlipEntity*)data;
    SlipEntity& current_ent = ents[index];

    if (SlipActor* p = dynamic_cast<SlipActor*>(&current_ent))
        *out_text = p->modelPath.c_str(); // not very safe

    return true;
}

SlipEditor::SlipEditor(int width, int height) : width(width), height(height)
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
    ImGui_ImplOpenGL3_Init("#version 130");

    ImGui::StyleColorsDark();
}

void SlipEditor::startRender()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    //ImGuizmo::BeginFrame();
    ImGui::NewFrame();

}

void SlipEditor::renderHierarchy()
{
    ImGui::Begin("Hierarchy", &hierarchy, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(0,0));
    ImGui::SetWindowSize(ImVec2(width/4.5, height/1.5));

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New..", "Ctrl+I")) { prop = SlipEditor::LEVEL; SlipLevel::Get().newLevel("unamed"); }
            if (ImGui::MenuItem("Save..", "Ctrl+S")) { SlipLevel::Get().saveLevel(); }
            if (ImGui::MenuItem("Open..", "Ctrl+O")) { hierarchyOpen = true; }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (hierarchyOpen)
    {
        prop = SlipEditor::LEVEL;

        SlipLevel::Get().openLevel(IO::FileDialog::OpenFile("Slip Level (*.level_cache)\0*.level_cache\0"));
        hierarchyOpen = false;
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

void SlipEditor::renderProperties()
{
    ImGui::Begin("Properties", &properties, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(0, height/1.5));
    ImGui::SetWindowSize(ImVec2(width / 4.5, height/3));

    switch (prop)
    {
    case SlipEditor::LEVEL:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().levelName.c_str(), ".level_cache");

        ImGui::Text("Bsp path (.bsp_cache)");
        ImGui::InputText(".b", SlipLevel::Get().bsp, 192);
        ImGui::SameLine();
        if (ImGui::Button("...b"))
        {
            std::string selected_path = IO::FileDialog::OpenFile("Slip BSP (*.bsp_cache)\0*.bsp_cache\0");

            std::string mystr = selected_path.substr(selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(SlipLevel::Get().bsp, file.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Gou"))
        {
            prop = SlipEditor::BSP;
        }
    }
    break;
    case SlipEditor::BSP:
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
    break;
    case SlipEditor::MODEL:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType].path.c_str(), ".model_cache");

        ImGui::Text("Collision path (.physics_cache)");
        ImGui::InputText(".1", SlipLevel::Get().models[selectedType].colPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("...1"))
        {
            std::string selected_path = IO::FileDialog::OpenFile("Slip Physics (*.physics_cache)\0*.physics_cache\0");

            std::string mystr = selected_path.substr(selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(SlipLevel::Get().models[selectedType].colPath, file.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("GoC"))
        {
            prop = SlipEditor::COLLISION;
            break;
        }

        materials_name = std::filesystem::path(SlipLevel::Get().models[selectedType].materials[matSelected].matPath).stem().string();

        if (ImGui::BeginCombo("Materials", materials_name.c_str()))
        {
            for (int n = 0; n < SlipLevel::Get().models[selectedType].materials.size(); n++)
            {
                std::filesystem::path path(SlipLevel::Get().models[selectedType].materials[n].matPath);

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
        ImGui::InputText(".", SlipLevel::Get().models[selectedType].materials[matSelected].matPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("..."))
        {
            std::string selected_path = IO::FileDialog::OpenFile("Slip Material (*.material_cache)\0*.material_cache\0");

            std::string mystr = selected_path.substr(selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(SlipLevel::Get().models[selectedType].materials[0].matPath, file.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("GoM"))
        {
            prop = SlipEditor::MATERIAL;
            break;
        }

        if (ImGui::Button("Save"))
        {
            extractor.save(SlipLevel::Get().models[selectedType], SlipLevel::Get().models[selectedType].path);
        }
    }
    break;
    case SlipEditor::COLLISION:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType].colPath, ".physics_cache");
        ImGui::Text("Mass (KG)");
        ImGui::InputInt(".m", &SlipLevel::Get().models[selectedType].collision->mass);

        switch (SlipLevel::Get().models[selectedType].collision->col_type)
        {
        case SlipCollision::BOX:
            collision_type = "BOX";
            break;
        case SlipCollision::SPHERE:
            collision_type = "SPHERE";
            break;
        case SlipCollision::CAPSULE:
            collision_type = "CAPSULE";
            break;
        case SlipCollision::CONE:
            collision_type = "CONE";
            break;
        case SlipCollision::CYLINDER:
            collision_type = "CYLINDER";
            break;
        }

        if (ImGui::BeginCombo("Collision type", collision_type))
        {
            for (int n = 0; n < 5; n++)
            {
                switch (n)
                {
                case SlipCollision::BOX:
                {
                    bool is_selected = (collision_type == "BOX");
                    if (ImGui::Selectable("BOX", is_selected))
                    {
                        collision_type = "BOX";
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                    break;
                case SlipCollision::SPHERE:
                {
                    bool is_selected = (collision_type == "SPHERE");
                    if (ImGui::Selectable("SPHERE", is_selected))
                    {
                        collision_type = "SPHERE";
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                    break;
                case SlipCollision::CAPSULE:
                {
                    bool is_selected = (collision_type == "CAPSULE");
                    if (ImGui::Selectable("CAPSULE", is_selected))
                    {
                        collision_type = "CAPSULE";
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                break;
                case SlipCollision::CONE:
                {
                    bool is_selected = (collision_type == "CONE");
                    if (ImGui::Selectable("CONE", is_selected))
                    {
                        collision_type = "CONE";
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                break;
                case SlipCollision::CYLINDER:
                {
                    bool is_selected = (collision_type == "CYLINDER");
                    if (ImGui::Selectable("CYLINDER", is_selected))
                    {
                        collision_type = "CYLINDER";
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                break;
                }

                SlipLevel::Get().models[selectedType].collision->col_type = (SlipCollision::Collision_Type)n;
            }
            ImGui::EndCombo();
        }

        ImGui::InputFloat3("Scale", SlipLevel::Get().models[selectedType].collision->scale);

        if (ImGui::Button("Save"))
        {
            extractor.save(*SlipLevel::Get().models[selectedType].collision, SlipLevel::Get().models[selectedType].colPath);
        }
    }
    break;
    case SlipEditor::MATERIAL:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", SlipLevel::Get().models[selectedType].materials[0].matPath, ".material_cache");
        ImGui::Text("Shader type");
        ImGui::InputText("s.", SlipLevel::Get().models[selectedType].materials[0].mat->shaderType, 4);
        ImGui::Text("Shininess");
        ImGui::InputFloat("f.", &SlipLevel::Get().models[selectedType].materials[0].mat->shininess);
        ImGui::Text("Diffuse path (.texture_cache)");
        ImGui::InputText("d.", SlipLevel::Get().models[selectedType].materials[0].mat->diffPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("..."))
        {
            std::string selected_path = IO::FileDialog::OpenFile("Slip Texture (*.texture_cache)\0*.texture_cache\0");

            std::string mystr = selected_path.substr(selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(SlipLevel::Get().models[selectedType].materials[0].mat->diffPath, file.c_str());
        }

        if (ImGui::Button("Save"))
        {
            extractor.save(*SlipLevel::Get().models[selectedType].materials[0].mat, SlipLevel::Get().models[selectedType].materials[0].mat->path);
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
                        bool is_selected = (entity_mesh == SlipLevel::Get().models[n].path.c_str()); // You can store your selection however you want, outside or inside your objects
                        if (ImGui::Selectable(SlipLevel::Get().models[n].path.c_str(), is_selected))
                            entity_mesh = SlipLevel::Get().models[n].path.c_str();
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                    }
                    ImGui::EndCombo();
                }

                ImGui::InputFloat3("Translate", posEnt);
                ImGui::InputFloat3("Rotate", rotEnt);
                ImGui::InputFloat3("Scale", scaEnt);

                if (ImGui::Button("Create Entitie"))
                {
                    SlipActor* ent = new SlipActor(entity_mesh);
                    ent->position = glm::vec3(posEnt[0], posEnt[1], posEnt[2]);
                    ent->rotation = glm::vec3(rotEnt[0], rotEnt[1], rotEnt[2]);
                    ent->scale = glm::vec3(scaEnt[0], scaEnt[1], scaEnt[2]);
                    SlipLevel::Get().entities.push_back(ent);
                }
            }
            else
            {
                float pos[3] = {
                SlipLevel::Get().entities[entitySelected]->position.x,
                SlipLevel::Get().entities[entitySelected]->position.y,
                SlipLevel::Get().entities[entitySelected]->position.z
                };
                float rot[3] = {
                    SlipLevel::Get().entities[entitySelected]->rotation.x,
                    SlipLevel::Get().entities[entitySelected]->rotation.y,
                    SlipLevel::Get().entities[entitySelected]->rotation.z
                };
                float sca[3] = {
                    SlipLevel::Get().entities[entitySelected]->scale.x,
                    SlipLevel::Get().entities[entitySelected]->scale.y,
                    SlipLevel::Get().entities[entitySelected]->scale.z
                };

                ImGui::InputFloat3("Translate", pos);
                ImGui::InputFloat3("Rotate", rot);
                ImGui::InputFloat3("Scale", sca);
                SlipLevel::Get().entities[entitySelected]->position = glm::vec3(pos[0], pos[1], pos[2]);
                SlipLevel::Get().entities[entitySelected]->rotation = glm::vec4(rot[0], rot[1], rot[2], 1.0f);
                SlipLevel::Get().entities[entitySelected]->scale = glm::vec3(sca[0], sca[1], sca[2]);
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

void SlipEditor::renderGame(SlipFrameBuffer& frameBuffer)
{
    ImGui::Begin("Game", &game, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(width / 4.5, 0));
    ImGui::SetWindowSize(ImVec2(width / 1.8, height / 1.5));

    if (ImGui::Button(playBtn))
    {
        SlipLevel::Get().playMode();
        if (SlipLevel::Get().isPlaying())
            playBtn = "Stop";
        else
            playBtn = "Play";
    }

    ImGui::SetCursorPos((ImGui::GetWindowSize() - ImVec2(width / 1.85, height / 2.2)) * 0.5f);
    ImGui::Image((void*)(intptr_t)frameBuffer.textureColorBuffer, ImVec2(width / 1.85, height / 1.85), ImVec2(0, 0), ImVec2(1, -1));
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
    ImGui::Begin("Scene", &scene, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowPos(ImVec2(width / 4.5, height / 1.5));
    ImGui::SetWindowSize(ImVec2(width / 1.28, height / 3));

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
        SlipLevel::Get().models.push_back(extractor.extract(IO::FileDialog::OpenFile("Objects 3D (*.obj)(*.fbx)\0*.obj;*.fbx\0")));
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

    ImGui::BeginChild("General", ImVec2(width / 5.25, height / 3.5), true);
    if (ImGui::TreeNode(SlipLevel::Get().levelName.c_str()))
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
    ImGui::BeginChild("In Scene", ImVec2(width / 1.75, height / 3.5), true);
    switch (sceneTypeSelected)
    {
    case 0:
    {
        for (int i = 0; i < SlipLevel::Get().models.size(); i++)
        {
            ImGui::Text(SlipLevel::Get().models[i].path.c_str());
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

void SlipEditor::processWindow()
{
    width = SlipWindow::Get().getWidth();
    height = SlipWindow::Get().getHeight();
}

void SlipEditor::processInput()
{
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
