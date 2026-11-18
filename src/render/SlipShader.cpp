#include "SlipShader.h"
#include "SlipWindow.h"
#include "io.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <android/log.h>
    #define SLIP_SHADER_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "SlipShader", __VA_ARGS__)
#else
    #define SLIP_SHADER_ERR(...) printf(__VA_ARGS__)
#endif

SlipShader* SlipShader::CreateFromSource(const char* vertexSrc, const char* fragmentSrc)
{
    if (!vertexSrc || !fragmentSrc) return nullptr;

    int success = 0;
    char infoLog[1024] = { 0 };

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        SLIP_SHADER_ERR("[SlipShader] ERROR::VERTEX::glCreateShader retornó 0 (no hay contexto OpenGL activo en este hilo)\n");
        return nullptr;
    }
    glShaderSource(vertexShader, 1, &vertexSrc, NULL);
    glCompileShader(vertexShader);

    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        memset(infoLog, 0, sizeof(infoLog));
        glGetShaderInfoLog(vertexShader, sizeof(infoLog) - 1, NULL, infoLog);
        SLIP_SHADER_ERR("[SlipShader] ERROR::VERTEX::COMPILATION_FAILED: %s\n", infoLog);
        glDeleteShader(vertexShader);
        return nullptr;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        SLIP_SHADER_ERR("[SlipShader] ERROR::FRAGMENT::glCreateShader retornó 0 (no hay contexto OpenGL activo en este hilo)\n");
        glDeleteShader(vertexShader);
        return nullptr;
    }
    glShaderSource(fragmentShader, 1, &fragmentSrc, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        memset(infoLog, 0, sizeof(infoLog));
        glGetShaderInfoLog(fragmentShader, sizeof(infoLog) - 1, NULL, infoLog);
        SLIP_SHADER_ERR("[SlipShader] ERROR::FRAGMENT::COMPILATION_FAILED: %s\n", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return nullptr;
    }

    unsigned int programID = glCreateProgram();
    if (programID == 0) {
        SLIP_SHADER_ERR("[SlipShader] ERROR::PROGRAM::glCreateProgram retornó 0\n");
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return nullptr;
    }
    glAttachShader(programID, vertexShader);
    glAttachShader(programID, fragmentShader);
    glLinkProgram(programID);

    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        memset(infoLog, 0, sizeof(infoLog));
        glGetProgramInfoLog(programID, sizeof(infoLog) - 1, NULL, infoLog);
        SLIP_SHADER_ERR("[SlipShader] ERROR::PROGRAM::LINK_FAILED: %s\n", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(programID);
        return nullptr;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    SlipShader* shader = new SlipShader();
    shader->ID = programID;
    return shader;
}

SlipShader::SlipShader(const char* vertexPath, const char* fragmentPath)
{
    int  success;
    char infoLog[512];

    unsigned int vertexShader;
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    std::string vertexSource;
    if (!SlipFileSystem::ReadAllText(vertexPath, vertexSource)) {
        IO::readAllFromFile(vertexPath, vertexSource);
    }
    const char* vertexCode = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexCode, NULL);
    glCompileShader(vertexShader);

    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n\tIn %s: %s", vertexPath, infoLog);
    }

    unsigned int fragmentShader;
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    std::string fragmentSource;
    if (!SlipFileSystem::ReadAllText(fragmentPath, fragmentSource)) {
        IO::readAllFromFile(fragmentPath, fragmentSource);
    }
    const char* fragmentCode = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentCode, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n\tIn %s: %s", fragmentPath, infoLog);
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glLinkProgram(ID);

    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(ID, 512, NULL, infoLog);
        printf("ERROR::SHADER::PROGRAM::COMPILATION_FAILED\n\t%s", infoLog);
    }

    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);
}

SlipShader::SlipShader(const char* vertexPath, const char* fragmentPath, const char* tessellationControlPath, const char* tessellationEvaluationPath)
{
    int  success;
    char infoLog[512];

    unsigned int vertexShader;
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    std::string vertexSource;
    IO::readAllFromFile(vertexPath, vertexSource);
    const char* vertexCode = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexCode, NULL);
    glCompileShader(vertexShader);

    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n\tIn %s: %s", vertexPath, infoLog);
    }

    unsigned int fragmentShader;
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    std::string fragmentSource;
    IO::readAllFromFile(fragmentPath, fragmentSource);
    const char* fragmentCode = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentCode, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n\tIn %s: %s", fragmentPath, infoLog);
    }

    unsigned int tessellationControlShader;
    tessellationControlShader = glCreateShader(GL_TESS_CONTROL_SHADER);
    std::string tessellationControlSource;
    IO::readAllFromFile(tessellationControlPath, tessellationControlSource);
    const char* tessellationControlCode = tessellationControlSource.c_str();
    glShaderSource(tessellationControlShader, 1, &tessellationControlCode, NULL);
    glCompileShader(tessellationControlShader);

    glGetShaderiv(tessellationControlShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(tessellationControlShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::TESSELLATION::CONTROL::COMPILATION_FAILED\n\tIn %s: %s", tessellationControlPath, infoLog);
    }

    unsigned int tessellationEvaluationShader;
    tessellationEvaluationShader = glCreateShader(GL_TESS_EVALUATION_SHADER);
    std::string tessellationEvaluationSource;
    IO::readAllFromFile(tessellationEvaluationPath, tessellationEvaluationSource);
    const char* tessellationEvaluationCode = tessellationEvaluationSource.c_str();
    glShaderSource(tessellationEvaluationShader, 1, &tessellationEvaluationCode, NULL);
    glCompileShader(tessellationEvaluationShader);

    glGetShaderiv(tessellationEvaluationShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(tessellationEvaluationShader, 512, NULL, infoLog);
        printf("ERROR::SHADER::TESSELLATION::EVALUATION::COMPILATION_FAILED\n\tIn %s: %s", tessellationEvaluationPath, infoLog);
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glAttachShader(ID, tessellationControlShader);
    glAttachShader(ID, tessellationEvaluationShader);
    glLinkProgram(ID);

    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(ID, 512, NULL, infoLog);
        printf("ERROR::SHADER::PROGRAM::COMPILATION_FAILED\n\t%s", infoLog);
    }

    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);
}

void SlipShader::use()
{
    glUseProgram(ID);
}

void SlipShader::destroy()
{
    glDeleteProgram(ID);
}

void SlipShader::setBool(const std::string& name, bool value) const
{
    glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
}

void SlipShader::setInt(const std::string& name, int value) const
{
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void SlipShader::setFloat(const std::string& name, float value) const
{
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}

void SlipShader::setMat4(const std::string& name, const glm::mat4 &value) const
{
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

void SlipShader::setMat4Array(const std::string& name, const glm::mat4* values, int count) const
{
    if (count > 0 && values != nullptr) {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), count, GL_FALSE, glm::value_ptr(values[0]));
    }
}

void SlipShader::setVec2(const std::string& name, const glm::vec2 &value) const
{
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void SlipShader::setVec2(const std::string& name, float x, float y) const
{
    glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
}

void SlipShader::setVec3(const std::string& name, const glm::vec3 &value) const
{
    glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void SlipShader::setVec3(const std::string& name, float x, float y, float z) const
{
    glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
}

void SlipShader::setVec4(const std::string& name, const glm::vec4 &value) const
{
    glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void SlipShader::setVec4(const std::string& name, float x, float y, float z, float w) const
{
    glUniform4f(glGetUniformLocation(ID, name.c_str()), x, y, z, w);
}
