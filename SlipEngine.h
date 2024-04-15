#pragma once

#ifndef SLIPENGINE_EXPORTS
#define SLIPENGINE_API __declspec(dllexport)
#else
#define SLIPENGINE_API __declspec(dllimport)
#endif // !SLIPENGINE_EXPORTS