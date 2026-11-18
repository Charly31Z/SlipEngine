#pragma once

#ifndef TEST_GLSL_CHALLENGER_H
#define TEST_GLSL_CHALLENGER_H

struct ChallengerStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

extern ChallengerStats g_GlobalChallengerStats;

// Exported test suite runner functions
bool RunGLSLCompilerChallenges();
bool RunSlipMaterialChallenges();

// Helpers exported from compiler unit to material unit
bool CreateChallengerReflectionGraph(const char* filepath);
bool CreateChallengerGraphA(const char* filepath);
bool CreateChallengerGraphB(const char* filepath);
bool CreateChallengerPropsGraph(const char* filepath);
bool CreateChallengerRoundTripGraph(const char* filepath);

#endif // TEST_GLSL_CHALLENGER_H
