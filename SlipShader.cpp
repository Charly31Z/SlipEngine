#include "SlipShader.h"

#include <glad/glad.h>

#include "io.h"

SlipShader::SlipShader(const char* vertexPath, const char* fragmentPath)
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

void SlipShader::setMat4(const std::string& name, glm::mat4 &value) const
{
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

void SlipShader::setVec2(const std::string& name, glm::vec2 &value) const
{
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void SlipShader::setVec2(const std::string& name, float x, float y) const
{
    glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
}

void SlipShader::setVec3(const std::string& name, glm::vec3 &value) const
{
    glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void SlipShader::setVec3(const std::string& name, float x, float y, float z) const
{
    glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
}
