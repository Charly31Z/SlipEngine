#include "SlipEngine/render/SlipFontManager.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>

namespace {

static std::vector<uint32_t> DecodeUTF8(const std::string& str) {
    std::vector<uint32_t> codepoints;
    size_t i = 0;
    while (i < str.size()) {
        uint8_t c = static_cast<uint8_t>(str[i]);
        if ((c & 0x80) == 0) {
            codepoints.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(str[i + 1]) & 0x3F);
            codepoints.push_back(cp);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
            uint32_t cp = ((c & 0x0F) << 12) |
                          ((static_cast<uint8_t>(str[i + 1]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(str[i + 2]) & 0x3F);
            codepoints.push_back(cp);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
            uint32_t cp = ((c & 0x07) << 18) |
                          ((static_cast<uint8_t>(str[i + 1]) & 0x3F) << 12) |
                          ((static_cast<uint8_t>(str[i + 2]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(str[i + 3]) & 0x3F);
            codepoints.push_back(cp);
            i += 4;
        } else {
            codepoints.push_back('?');
            i += 1;
        }
    }
    return codepoints;
}

} // namespace

SlipFontManager& SlipFontManager::Get() {
    static SlipFontManager instance;
    return instance;
}

SlipFontManager::~SlipFontManager() {
    Clean();
}

void SlipFontManager::Clean() {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glDeleteTextures) {
        m_Fonts.clear();
        return;
    }
#endif
    for (auto& kv : m_Fonts) {
        for (auto& g : kv.second.glyphs) {
            if (g.second.textureID != 0) {
                glDeleteTextures(1, &g.second.textureID);
            }
        }
    }
    m_Fonts.clear();

    if (m_VAO != 0) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO != 0) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
}

std::string SlipFontManager::ResolveFontPath(const std::string& requestedPath) const {
    if (!requestedPath.empty()) {
        if (SlipFileSystem::Exists(requestedPath)) return requestedPath;
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (!assetsDir.empty()) {
            std::string combined = SlipFileSystem::CombinePath(assetsDir, requestedPath);
            if (SlipFileSystem::Exists(combined)) return combined;
        }
    }

    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf"
    };
    for (const char* c : candidates) {
        if (SlipFileSystem::Exists(c)) {
            return c;
        }
    }
    return "";
}

SlipFontAtlas* SlipFontManager::GetOrLoadFont(const std::string& fontPath) {
    std::string resolved = ResolveFontPath(fontPath);
    if (resolved.empty()) return nullptr;

    auto it = m_Fonts.find(resolved);
    if (it != m_Fonts.end()) {
        return &it->second;
    }

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        return nullptr;
    }

    SlipFontAtlas atlas;
    atlas.path = resolved;
    atlas.basePixelHeight = 48;

    FT_Face face = nullptr;
    if (SlipFileSystem::ReadAllBytes(resolved, atlas.fontBuffer) && !atlas.fontBuffer.empty()) {
        if (FT_New_Memory_Face(ft, atlas.fontBuffer.data(), static_cast<FT_Long>(atlas.fontBuffer.size()), 0, &face)) {
            FT_Done_FreeType(ft);
            return nullptr;
        }
    } else {
        if (FT_New_Face(ft, resolved.c_str(), 0, &face)) {
            FT_Done_FreeType(ft);
            return nullptr;
        }
    }

    FT_Set_Pixel_Sizes(face, 0, atlas.basePixelHeight);
    atlas.lineHeight = static_cast<float>(face->size->metrics.height >> 6);
    if (atlas.lineHeight <= 0.0f) atlas.lineHeight = static_cast<float>(atlas.basePixelHeight);
    atlas.ascender = static_cast<float>(face->size->metrics.ascender >> 6);
    if (atlas.ascender <= 0.0f) atlas.ascender = atlas.basePixelHeight * 0.8f;

    bool hasGL = true;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) hasGL = false;
#endif

    if (hasGL) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    for (uint32_t cp = 32; cp <= 255; ++cp) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) {
            continue;
        }

        SlipGlyph glyph;
        glyph.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
        glyph.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
        glyph.advance = static_cast<uint32_t>(face->glyph->advance.x);

        if (hasGL && glyph.size.x > 0 && glyph.size.y > 0 && face->glyph->bitmap.buffer) {
            glGenTextures(1, &glyph.textureID);
            glBindTexture(GL_TEXTURE_2D, glyph.textureID);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_R8,
                glyph.size.x,
                glyph.size.y,
                0,
                GL_RED,
                GL_UNSIGNED_BYTE,
                face->glyph->bitmap.buffer
            );
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        atlas.glyphs[cp] = glyph;
    }

    if (hasGL) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    m_Fonts[resolved] = std::move(atlas);
    return &m_Fonts[resolved];
}

glm::vec2 SlipFontManager::MeasureText(const std::string& text, const std::string& fontPath, float fontSize) {
    if (text.empty() || fontSize <= 0.0f) return glm::vec2(0.0f, fontSize);

    SlipFontAtlas* atlas = GetOrLoadFont(fontPath);
    if (!atlas) {
        return glm::vec2(static_cast<float>(text.size()) * fontSize * 0.55f, fontSize);
    }

    float scale = fontSize / static_cast<float>(atlas->basePixelHeight);
    std::vector<uint32_t> cps = DecodeUTF8(text);

    float maxWidth = 0.0f;
    float currentWidth = 0.0f;
    int lines = 1;

    for (uint32_t cp : cps) {
        if (cp == '\n') {
            maxWidth = (std::max)(maxWidth, currentWidth);
            currentWidth = 0.0f;
            lines++;
            continue;
        }
        auto it = atlas->glyphs.find(cp);
        if (it == atlas->glyphs.end()) it = atlas->glyphs.find('?');
        if (it != atlas->glyphs.end()) {
            currentWidth += (it->second.advance >> 6) * scale;
        }
    }
    maxWidth = (std::max)(maxWidth, currentWidth);
    return glm::vec2(maxWidth, lines * atlas->lineHeight * scale);
}

void SlipFontManager::DrawText2D(const std::string& text,
                                 const std::string& fontPath,
                                 float fontSize,
                                 const glm::vec2& pos,
                                 const glm::vec4& color,
                                 const glm::mat4& projection,
                                 bool yDown) {
    if (text.empty() || fontSize <= 0.0f) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenVertexArrays) return;
#endif

    SlipFontAtlas* atlas = GetOrLoadFont(fontPath);
    if (!atlas) return;

    SlipShader* shader = SlipShaderLibrary::Get("Font2D");
    if (!shader) return;

    if (m_VAO == 0) {
        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    float scale = fontSize / static_cast<float>(atlas->basePixelHeight);

    shader->use();
    shader->setMat4("u_Projection", projection);
    shader->setMat4("u_Model", glm::mat4(1.0f));
    shader->setVec4("u_Color", color.r, color.g, color.b, color.a);
    shader->setInt("u_FontAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VAO);

    std::vector<uint32_t> cps = DecodeUTF8(text);
    float cursorX = pos.x;
    float baselineY = yDown ? (pos.y + atlas->ascender * scale) : pos.y;

    for (uint32_t cp : cps) {
        if (cp == '\n') {
            cursorX = pos.x;
            baselineY += (yDown ? 1.0f : -1.0f) * (atlas->lineHeight * scale);
            continue;
        }

        auto it = atlas->glyphs.find(cp);
        if (it == atlas->glyphs.end()) it = atlas->glyphs.find('?');
        if (it == atlas->glyphs.end()) continue;

        const SlipGlyph& ch = it->second;
        if (ch.textureID != 0 && ch.size.x > 0 && ch.size.y > 0) {
            float xpos = cursorX + ch.bearing.x * scale;
            float w = ch.size.x * scale;
            float h = ch.size.y * scale;

            if (yDown) {
                float ypos = baselineY - ch.bearing.y * scale;
                float vertices[6][4] = {
                    { xpos,     ypos + h,   0.0f, 1.0f },
                    { xpos,     ypos,       0.0f, 0.0f },
                    { xpos + w, ypos,       1.0f, 0.0f },

                    { xpos,     ypos + h,   0.0f, 1.0f },
                    { xpos + w, ypos,       1.0f, 0.0f },
                    { xpos + w, ypos + h,   1.0f, 1.0f }
                };
                glBindTexture(GL_TEXTURE_2D, ch.textureID);
                glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            } else {
                float ypos = baselineY - (ch.size.y - ch.bearing.y) * scale;
                float vertices[6][4] = {
                    { xpos,     ypos + h,   0.0f, 0.0f },
                    { xpos,     ypos,       0.0f, 1.0f },
                    { xpos + w, ypos,       1.0f, 1.0f },

                    { xpos,     ypos + h,   0.0f, 0.0f },
                    { xpos + w, ypos,       1.0f, 1.0f },
                    { xpos + w, ypos + h,   1.0f, 0.0f }
                };
                glBindTexture(GL_TEXTURE_2D, ch.textureID);
                glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        cursorX += (ch.advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

