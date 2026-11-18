#include "SlipOcclusionCuller.h"
#include "SlipShaderLibrary.h"
#include <glm/gtc/matrix_transform.hpp>

SlipOcclusionCuller& SlipOcclusionCuller::Get()
{
    static SlipOcclusionCuller instance;
    return instance;
}

void SlipOcclusionCuller::Init()
{
    if (m_Initialized) return;
    InitBoxMesh();
    m_Initialized = true;
}

void SlipOcclusionCuller::Shutdown()
{
    for (auto& pair : m_Queries) {
        if (pair.second.queryId != 0) {
            glDeleteQueries(1, &pair.second.queryId);
        }
    }
    m_Queries.clear();

    for (GLuint q : m_QueryPool) {
        glDeleteQueries(1, &q);
    }
    m_QueryPool.clear();

    if (m_BoxVAO != 0) {
        glDeleteVertexArrays(1, &m_BoxVAO);
        glDeleteBuffers(1, &m_BoxVBO);
        glDeleteBuffers(1, &m_BoxEBO);
        m_BoxVAO = m_BoxVBO = m_BoxEBO = 0;
    }

    m_Initialized = false;
}

GLuint SlipOcclusionCuller::AllocateQuery()
{
    if (!m_QueryPool.empty()) {
        GLuint q = m_QueryPool.back();
        m_QueryPool.pop_back();
        return q;
    }
    GLuint q = 0;
    glGenQueries(1, &q);
    return q;
}

void SlipOcclusionCuller::ReleaseQuery(GLuint q)
{
    if (q != 0) {
        m_QueryPool.push_back(q);
    }
}

void SlipOcclusionCuller::InitBoxMesh()
{
    if (m_BoxVAO != 0) return;

    // Cubo unitario centrado en [0, 1]
    float vertices[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f
    };

    unsigned int indices[] = {
        0, 2, 1, 0, 3, 2, // Front
        2, 3, 7, 2, 7, 6, // Top
        1, 2, 6, 1, 6, 5, // Right
        0, 4, 7, 0, 7, 3, // Left
        0, 1, 5, 0, 5, 4, // Bottom
        4, 5, 6, 4, 6, 7  // Back
    };

    glGenVertexArrays(1, &m_BoxVAO);
    glGenBuffers(1, &m_BoxVBO);
    glGenBuffers(1, &m_BoxEBO);

    glBindVertexArray(m_BoxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_BoxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_BoxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}

bool SlipOcclusionCuller::IsVisible(uint64_t objectId, const glm::vec3& eye, const glm::vec3& minPos, const glm::vec3& maxPos)
{
    if (!m_Enabled) return true;

    // Si la cámara se encuentra dentro o muy próxima a la caja (ej. Sponza o una habitación), nunca ocluir
    glm::vec3 expandedMin = minPos - glm::vec3(2.0f);
    glm::vec3 expandedMax = maxPos + glm::vec3(2.0f);
    if (eye.x >= expandedMin.x && eye.x <= expandedMax.x &&
        eye.y >= expandedMin.y && eye.y <= expandedMax.y &&
        eye.z >= expandedMin.z && eye.z <= expandedMax.z) {
        return true;
    }

    return IsVisible(objectId);
}

bool SlipOcclusionCuller::IsVisible(uint64_t objectId)
{
    if (!m_Enabled) return true;

    auto it = m_Queries.find(objectId);
    if (it == m_Queries.end()) {
        return true; // Primera vez: visible
    }

    auto& rec = it->second;
    if (rec.queryActive && rec.queryId != 0) {
        GLuint available = 0;
        glGetQueryObjectuiv(rec.queryId, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint samples = 0;
            glGetQueryObjectuiv(rec.queryId, GL_QUERY_RESULT, &samples);
            rec.lastResultVisible = (samples > 0);
            rec.queryActive = false;
        }
    }

    return rec.lastResultVisible;
}

void SlipOcclusionCuller::BeginOcclusionPass(const glm::mat4& viewProjection)
{
    if (!m_Enabled) return;
    Init();

    m_CurrentVP = viewProjection;

    // Deshabilitar escritura en color y profundidad, conservar Depth Test
    glGetIntegerv(GL_DEPTH_FUNC, &m_PrevDepthFunc);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    m_BoxShader = SlipShaderLibrary::Get("Unlit");
    if (m_BoxShader) {
        m_BoxShader->use();
        m_BoxShader->setMat4("u_Projection", m_CurrentVP);
        m_BoxShader->setMat4("u_View", glm::mat4(1.0f));
        m_BoxShader->setMat4("projection", m_CurrentVP);
        m_BoxShader->setMat4("view", glm::mat4(1.0f));
    }

    if (m_BoxVAO != 0) {
        glBindVertexArray(m_BoxVAO);
    }
}

void SlipOcclusionCuller::RenderBoundingBoxQuery(uint64_t objectId, const glm::vec3& minPos, const glm::vec3& maxPos)
{
    if (!m_Enabled || m_BoxVAO == 0 || !m_BoxShader) return;

    auto& rec = m_Queries[objectId];
    if (rec.queryId == 0) {
        rec.queryId = AllocateQuery();
    }
    rec.lastFrameQueried = m_FrameIndex;

    glm::vec3 size = maxPos - minPos;
    if (size.x <= 0.001f || size.y <= 0.001f || size.z <= 0.001f) return;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), minPos) * glm::scale(glm::mat4(1.0f), size);
    m_BoxShader->setMat4("u_Model", model);
    m_BoxShader->setMat4("model", model);

#if defined(GL_ANY_SAMPLES_PASSED)
    glBeginQuery(GL_ANY_SAMPLES_PASSED, rec.queryId);
#else
    glBeginQuery(GL_SAMPLES_PASSED, rec.queryId);
#endif

    glDisable(GL_CULL_FACE);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glEnable(GL_CULL_FACE);

#if defined(GL_ANY_SAMPLES_PASSED)
    glEndQuery(GL_ANY_SAMPLES_PASSED);
#else
    glEndQuery(GL_SAMPLES_PASSED);
#endif

    rec.queryActive = true;
}

void SlipOcclusionCuller::EndOcclusionPass()
{
    if (!m_Enabled) return;

    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(static_cast<GLenum>(m_PrevDepthFunc));
}

void SlipOcclusionCuller::EndFrame()
{
    m_FrameIndex++;

    // Limpiar queries de objetos que ya no existen tras varios frames sin consultar
    if (m_FrameIndex % 60 == 0) {
        for (auto it = m_Queries.begin(); it != m_Queries.end(); ) {
            if (m_FrameIndex - it->second.lastFrameQueried > 120) {
                ReleaseQuery(it->second.queryId);
                it = m_Queries.erase(it);
            } else {
                ++it;
            }
        }
    }
}

