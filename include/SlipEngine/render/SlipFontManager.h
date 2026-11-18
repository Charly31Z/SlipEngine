#pragma once

#ifndef SLIP_FONT_MANAGER_H
#define SLIP_FONT_MANAGER_H

#include "SlipEngine.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct SlipGlyph {
    uint32_t textureID = 0;
    glm::ivec2 size = { 0, 0 };
    glm::ivec2 bearing = { 0, 0 };
    uint32_t advance = 0;
};

struct SlipFontAtlas {
    std::string path;
    int basePixelHeight = 48;
    float lineHeight = 48.0f;
    float ascender = 36.0f;
    std::unordered_map<uint32_t, SlipGlyph> glyphs;
    std::vector<uint8_t> fontBuffer;
};

class SLIPENGINE_API SlipFontManager {
public:
    static SlipFontManager& Get();

    SlipFontAtlas* GetOrLoadFont(const std::string& fontPath);

    glm::vec2 MeasureText(const std::string& text, const std::string& fontPath, float fontSize);

    // Dibuja texto en espacio ortográfico 2D (donde Y crece hacia abajo o hacia arriba según isYDown)
    void DrawText2D(const std::string& text,
                    const std::string& fontPath,
                    float fontSize,
                    const glm::vec2& pos,
                    const glm::vec4& color,
                    const glm::mat4& projection,
                    bool yDown = true);

    void DrawText2D(const std::string& fontPath,
                    const std::string& text,
                    float x,
                    float y,
                    float fontSize,
                    const glm::vec4& color,
                    const glm::mat4& projection) {
        DrawText2D(text, fontPath, fontSize, glm::vec2(x, y), color, projection, true);
    }

    void Clean();

private:
    SlipFontManager() = default;
    ~SlipFontManager();

    std::string ResolveFontPath(const std::string& requestedPath) const;

    std::unordered_map<std::string, SlipFontAtlas> m_Fonts;
    uint32_t m_VAO = 0;
    uint32_t m_VBO = 0;
};

#endif // SLIP_FONT_MANAGER_H

