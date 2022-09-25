#include "SlipEditor.h"

bool EntitiesGetter(void* data, int index, const char** out_text)
{
    SlipEntity* ents = (SlipEntity*)data;
    SlipEntity& current_ent = ents[index];

    *out_text = current_ent.name.c_str(); // not very safe

    return true;
}

void SlipEditor::init(GLFWwindow* window)
{
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

void SlipEditor::renderHierarchy(std::vector<SlipEntity>& entities, SlipShader& shader, SlipLight& sun, std::vector<SlipLight>& lights)
{
    ImGui::Begin("Scene", &hierarchy, ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Import model..", "Ctrl+I")) { hierarchyImport = true; }
            if (ImGui::MenuItem("Export model..", "Ctrl+O")) { hierarchySave = true; }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (hierarchyImport)
        ImGui::OpenPopup("Hierarchy Open File");
    if (hierarchySave)
        ImGui::OpenPopup("Hierarchy Export File");

    if (file_dialog.showFileDialog("Hierarchy Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".obj,.fbx"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file

        entities.push_back(SlipEntity::generateEntity2("imported", SlipModel{ "assets/models/" + file_dialog.selected_fn }, SlipMaterial{ shader, sun, lights }));
        hierarchyImport = false;
    }
    if (file_dialog.showFileDialog("Hierarchy Export File", imgui_addons::ImGuiFileBrowser::DialogMode::SAVE, ImVec2(700, 310), ".model_cache"))
    {
        std::cout << file_dialog.selected_fn << std::endl;      // The name of the selected file or directory in case of Select Directory dialog mode
        std::cout << file_dialog.selected_path << std::endl;    // The absolute path to the selected file
        std::cout << file_dialog.ext << std::endl;              // Access ext separately (For SAVE mode)
        //Do writing of files based on extension here

        std::ofstream o(file_dialog.selected_path + file_dialog.ext, std::ios::binary);

        int sizeVertices = entities[entitySelected].model.meshes[0].vertices.size();
        int sizeIndices = entities[entitySelected].model.meshes[0].indices.size();
        IO::write(o, sizeVertices);
        IO::write(o, sizeIndices);

        for (int i = 0; i < sizeVertices; i++)
        {
            float* positions = glm::value_ptr(entities[entitySelected].model.meshes[0].vertices[i].Position);
            float* normals = glm::value_ptr(entities[entitySelected].model.meshes[0].vertices[i].Normal);
            float* texcoords = glm::value_ptr(entities[entitySelected].model.meshes[0].vertices[i].TexCoords);
            IO::write(o, positions);
            IO::write(o, normals);
            IO::write(o, texcoords);
        }

        for (int i = 0; i < sizeIndices; i++)
        {
            IO::write(o, entities[entitySelected].model.meshes[0].indices[i]);
        }

        o.close();
        hierarchySave = false;
    }

    if (ImGui::TreeNode("Entities"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));
        for (int i = 0; i < entities.size(); i++)
        {
            ImGuiTreeNodeFlags node_flags;

            if (entitySelected == i)
                node_flags = ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_Leaf;
            else
                node_flags = ImGuiTreeNodeFlags_Leaf;
            bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, entities[i].name.c_str());

            if (ImGui::IsItemClicked()) {
                entitySelected = i;
            }

            if (node_open)
            {
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
        ImGui::PopStyleVar();
    }

    ImGui::End();
}

void SlipEditor::renderProperties(std::vector<SlipEntity>& entities)
{
    ImGui::Begin("Properties", &properties);

    float pos[3] = {
        entities[entitySelected].position.x,
        entities[entitySelected].position.y,
        entities[entitySelected].position.z
    };
    float rot[3] = {
        entities[entitySelected].rotation.x,
        entities[entitySelected].rotation.y,
        entities[entitySelected].rotation.z
    };
    float sca[3] = {
        entities[entitySelected].scale.x,
        entities[entitySelected].scale.y,
        entities[entitySelected].scale.z
    };

    ImGui::InputFloat3("Translate", pos);
    ImGui::InputFloat3("Rotate", rot);
    ImGui::InputFloat3("Scale", sca);
    entities[entitySelected].position = glm::vec3(pos[0], pos[1], pos[2]);
    entities[entitySelected].rotation = glm::vec3(rot[0], rot[1], rot[2]);
    entities[entitySelected].scale = glm::vec3(sca[0], sca[1], sca[2]);

    ImGui::Text("Material");
    float col[3] = {
        entities[entitySelected].material.color.x,
        entities[entitySelected].material.color.y,
        entities[entitySelected].material.color.z
    };
    ImGui::ColorEdit3("Color", col);
    entities[entitySelected].material.color = glm::vec3(col[0], col[1], col[2]);
    entities[entitySelected].material.ambient = glm::vec3(col[0], col[1], col[2]) / glm::vec3(8.f);
    entities[entitySelected].material.specular = glm::vec3(col[0], col[1], col[2]) / glm::vec3(6.f);

    ImGui::InputFloat("Shininess", &entities[entitySelected].material.shininess);

    ImGui::End();
}

void SlipEditor::endRender()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
