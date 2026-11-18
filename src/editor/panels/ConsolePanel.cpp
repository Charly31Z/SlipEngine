#include "SlipEngine/editor/panels/ConsolePanel.h"
#include "SlipEngine/core/SlipConsole.h"
#include <imgui.h>

void ConsolePanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Consola", &m_IsOpen)) {
        // Barra de herramientas superior
        if (ImGui::Button("Limpiar")) {
            SlipConsole::Clear();
        }
        ImGui::SameLine();

        if (ImGui::Button("Copiar")) {
            ImGui::LogToClipboard();
            std::lock_guard<std::mutex> lock(SlipConsole::s_Mutex);
            for (const auto& log : SlipConsole::s_Logs) {
                ImGui::LogText("%s\n", log.text.c_str());
            }
            ImGui::LogFinish();
        }
        ImGui::SameLine();

        ImGui::Checkbox("Auto-scroll", &SlipConsole::s_AutoScroll);
        ImGui::SameLine();

        // Filtro de búsqueda rápida
        static ImGuiTextFilter filter;
        filter.Draw("Buscar", 160.0f);

        ImGui::Separator();

        // Área con scroll para la salida de mensajes
        ImGui::BeginChild("LogScrollArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        {
            std::lock_guard<std::mutex> lock(SlipConsole::s_Mutex);
            for (const auto& log : SlipConsole::s_Logs) {
                if (!filter.PassFilter(log.text.c_str())) {
                    continue;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(log.color.r, log.color.g, log.color.b, log.color.a));
                ImGui::TextUnformatted(log.text.c_str());
                ImGui::PopStyleColor();
            }
        }

        // Auto-scroll hacia la parte inferior
        if (SlipConsole::s_AutoScroll && (SlipConsole::s_ScrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
            ImGui::SetScrollHereY(1.0f);
            SlipConsole::s_ScrollToBottom = false;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}
