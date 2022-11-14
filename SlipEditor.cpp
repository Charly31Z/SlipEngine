#include "SlipEditor.h"

bool EntitiesGetter(void* data, int index, const char** out_text)
{
    SlipEntity* ents = (SlipEntity*)data;
    SlipEntity& current_ent = ents[index];

    *out_text = current_ent.modelPath.c_str(); // not very safe

    return true;
}

SlipEditor::SlipEditor(int& width, int& height) : width(width), height(height)
{
}

void SlipEditor::init(GLFWwindow* window, SlipLevel& level)
{
    currentLevel = &level;

    //SETUP IMGUI
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ImGui::StyleColorsDark();
}

void SlipEditor::startRender()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
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
            if (ImGui::MenuItem("New..", "Ctrl+I")) { prop = SlipEditor::LEVEL; currentLevel->newLevel("unamed"); }
            if (ImGui::MenuItem("Save..", "Ctrl+S")) { currentLevel->saveLevel(); }
            if (ImGui::MenuItem("Open..", "Ctrl+O")) { hierarchyOpen = true; }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (hierarchyOpen)
    {
        prop = SlipEditor::LEVEL;

        ImGui::OpenPopup("Hierarchy Open");
        hierarchyOpen = false;
    }

    if (file_dialog.showFileDialog("Hierarchy Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".level_cache"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        currentLevel->openLevel(file_dialog.selected_path);
    }

    if (ImGui::TreeNode("Entities"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

        if (ImGui::IsItemClicked()) {
            existEnity = false;
            entity_mesh = NULL;
            prop = SlipEditor::ENTITY;
        }

        for (int i = 0; i < currentLevel->entities.size(); i++)
        {
            ImGuiTreeNodeFlags node_flags;

            if (entitySelected == i)
                node_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
            else
                node_flags = ImGuiTreeNodeFlags_Leaf;
            bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, currentLevel->entities[i].modelPath.c_str());

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
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", currentLevel->levelName.c_str(), ".level_cache");

        ImGui::Text("Bsp path (.bsp_cache)");
        ImGui::InputText(".b", currentLevel->bsp, 192);
        ImGui::SameLine();
        if (ImGui::Button("...b"))
        {
            ImGui::OpenPopup("Bsp Open");
        }
        ImGui::SameLine();
        if (ImGui::Button("Gou"))
        {
            prop = SlipEditor::BSP;
        }

        if (file_dialog.showFileDialog("Bsp Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".bsp_cache"))
        {
            std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
            std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

            std::string mystr = file_dialog.selected_path.substr(file_dialog.selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(currentLevel->bsp, file.c_str());
        }
    }
    break;
    case SlipEditor::BSP:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", currentLevel->levelName.c_str(), ".bsp_cache");

        ImGui::Text("SkyBox path (.model_cache)");
        ImGui::InputText(".sk", currentLevel->mBsp->skyboxPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("...s"))
        {
            ImGui::OpenPopup("Skybox Open");
        }

        if (ImGui::Button("Save"))
        {
            extractor.save(*currentLevel->mBsp, currentLevel->bsp);
        }

        if (file_dialog.showFileDialog("Skybox Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".model_cache"))
        {
            std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
            std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

            std::string mystr = file_dialog.selected_path.substr(file_dialog.selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(currentLevel->mBsp->skyboxPath, file.c_str());
        }
    }
    break;
    case SlipEditor::MODEL:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", currentLevel->models[selectedType].path.c_str(), ".model_cache");

        ImGui::Text("Collision path (.physics_cache)");
        ImGui::InputText(".1", currentLevel->models[selectedType].colPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("...1"))
        {
            ImGui::OpenPopup("Collision Open");
        }
        ImGui::SameLine();
        if (ImGui::Button("GoC"))
        {
            prop = SlipEditor::COLLISION;
            break;
        }

        materials_name = std::filesystem::path(currentLevel->models[selectedType].materials[matSelected].matPath).stem().string();

        if (ImGui::BeginCombo("Materials", materials_name.c_str()))
        {
            for (int n = 0; n < currentLevel->models[selectedType].materials.size(); n++)
            {
                std::filesystem::path path(currentLevel->models[selectedType].materials[n].matPath);

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
        ImGui::InputText(".", currentLevel->models[selectedType].materials[matSelected].matPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("..."))
        {
            ImGui::OpenPopup("Material Open");
        }
        ImGui::SameLine();
        if (ImGui::Button("GoM"))
        {
            prop = SlipEditor::MATERIAL;
            break;
        }

        if (ImGui::Button("Save"))
        {
            extractor.save(currentLevel->models[selectedType], currentLevel->models[selectedType].path);
        }

        if (file_dialog.showFileDialog("Material Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".material_cache"))
        {
            std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
            std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

            std::string mystr = file_dialog.selected_path.substr(file_dialog.selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(currentLevel->models[selectedType].materials[0].matPath, file.c_str());
        }

        if (file_dialog.showFileDialog("Collision Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".physics_cache"))
        {
            std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
            std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

            std::string mystr = file_dialog.selected_path.substr(file_dialog.selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(currentLevel->models[selectedType].colPath, file.c_str());
        }
    }
    break;
    case SlipEditor::COLLISION:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", currentLevel->models[selectedType].colPath, ".physics_cache");
        ImGui::Text("Mass (KG)");
        ImGui::InputInt(".m", &currentLevel->models[selectedType].collision->mass);

        switch (currentLevel->models[selectedType].collision->col_type)
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

                currentLevel->models[selectedType].collision->col_type = (SlipCollision::Collision_Type)n;
            }
            ImGui::EndCombo();
        }

        ImGui::InputFloat3("Scale", currentLevel->models[selectedType].collision->scale);

        if (ImGui::Button("Save"))
        {
            extractor.save(*currentLevel->models[selectedType].collision, currentLevel->models[selectedType].colPath);
        }
    }
    break;
    case SlipEditor::MATERIAL:
    {
        ImGui::TextColored(ImVec4(0.f, .5f, 1.f, 1.f), "Editing %s%s", currentLevel->models[selectedType].materials[0].matPath, ".material_cache");
        ImGui::Text("Shader type");
        ImGui::InputText("s.", currentLevel->models[selectedType].materials[0].mat->shaderType, 4);
        ImGui::Text("Shininess");
        ImGui::InputFloat("f.", &currentLevel->models[selectedType].materials[0].mat->shininess);
        ImGui::Text("Diffuse path (.texture_cache)");
        ImGui::InputText("d.", currentLevel->models[selectedType].materials[0].mat->diffPath, 192);
        ImGui::SameLine();
        if (ImGui::Button("..."))
        {
            ImGui::OpenPopup("Texture Open");
        }

        if (ImGui::Button("Save"))
        {
            extractor.save(*currentLevel->models[selectedType].materials[0].mat, currentLevel->models[selectedType].materials[0].mat->path);
        }

        if (file_dialog.showFileDialog("Texture Open", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".texture_cache"))
        {
            std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
            std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

            std::string mystr = file_dialog.selected_path.substr(file_dialog.selected_path.find("cache"));

            std::filesystem::path path(mystr);

            std::string file = path.parent_path().string() + "/" + path.stem().string();
            std::strcpy(currentLevel->models[selectedType].materials[0].mat->diffPath, file.c_str());
        }
    }
    break;
    case SlipEditor::ENTITY:
        {
            if (!existEnity)
            {
                if (ImGui::BeginCombo("modelPath", entity_mesh))
                {
                    for (int n = 0; n < currentLevel->models.size(); n++)
                    {
                        bool is_selected = (entity_mesh == currentLevel->models[n].path.c_str()); // You can store your selection however you want, outside or inside your objects
                        if (ImGui::Selectable(currentLevel->models[n].path.c_str(), is_selected))
                            entity_mesh = currentLevel->models[n].path.c_str();
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
                    SlipEntity* ent = new SlipEntity(entity_mesh);
                    ent->position = glm::vec3(posEnt[0], posEnt[1], posEnt[2]);
                    ent->rotation = glm::vec3(rotEnt[0], rotEnt[1], rotEnt[2]);
                    ent->scale = glm::vec3(scaEnt[0], scaEnt[1], scaEnt[2]);
                    currentLevel->entities.push_back(*ent);
                }
            }
            else
            {
                float pos[3] = {
                currentLevel->entities[entitySelected].position.x,
                currentLevel->entities[entitySelected].position.y,
                currentLevel->entities[entitySelected].position.z
                };
                float rot[3] = {
                    currentLevel->entities[entitySelected].rotation.x,
                    currentLevel->entities[entitySelected].rotation.y,
                    currentLevel->entities[entitySelected].rotation.z
                };
                float sca[3] = {
                    currentLevel->entities[entitySelected].scale.x,
                    currentLevel->entities[entitySelected].scale.y,
                    currentLevel->entities[entitySelected].scale.z
                };

                ImGui::InputFloat3("Translate", pos);
                ImGui::InputFloat3("Rotate", rot);
                ImGui::InputFloat3("Scale", sca);
                currentLevel->entities[entitySelected].position = glm::vec3(pos[0], pos[1], pos[2]);
                currentLevel->entities[entitySelected].rotation = glm::vec4(rot[0], rot[1], rot[2], 1.0f);
                currentLevel->entities[entitySelected].scale = glm::vec3(sca[0], sca[1], sca[2]);
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
        currentLevel->playMode();
        if (currentLevel->isPlaying())
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
        ImGui::EndMenuBar();
    }

    if (createBsp)
        ImGui::OpenPopup("Scene Create BSP File");

    if (createModel)
        ImGui::OpenPopup("Scene Create Model File");

    if (createCol)
        ImGui::OpenPopup("Scene Create Collision File");

    if (createTexture)
        ImGui::OpenPopup("Scene Create Texture File");

    if (sceneImport)
        ImGui::OpenPopup("Scene Open File");

    if (file_dialog.showFileDialog("Scene Create BSP File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".obj,.fbx"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        extractor.extractBsp(file_dialog.selected_path);
        createBsp = false;
    }

    if (file_dialog.showFileDialog("Scene Create Model File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".obj,.fbx"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        currentLevel->models.push_back(extractor.extract(file_dialog.selected_path));
        createModel = false;
    }

    if (file_dialog.showFileDialog("Scene Create Collision File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".obj,.fbx"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        extractor.extractCol(file_dialog.selected_path);
        createCol = false;
    }

    if (file_dialog.showFileDialog("Scene Create Texture File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".png,.jpg"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        extractor.extractTexture(file_dialog.selected_path);
        createTexture = false;
    }

    if (file_dialog.showFileDialog("Scene Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".model_cache"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        extractor.extractFromPreCache(currentLevel, file_dialog.selected_path);
        sceneImport = false;
    }

    ImGui::BeginChild("General", ImVec2(width / 5.25, height / 3.5), true);
    if (ImGui::TreeNode(SlipLevel::levelName.c_str()))
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
        for (int i = 0; i < currentLevel->models.size(); i++)
        {
            ImGui::Text(currentLevel->models[i].path.c_str());
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

void SlipEditor::endRender()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
