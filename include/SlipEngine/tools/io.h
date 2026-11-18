#pragma once

#include "SlipFileSystem.h"
#include <fstream>
#include <string>

namespace IO
{
	template<typename T>
	inline void read(std::ifstream& ifstream, T& t)
	{
		ifstream.read(reinterpret_cast<char*>(&t), sizeof(T));
	}

	inline void readAllFromFile(const std::string& filePath, std::string& s)
	{
		if (!SlipFileSystem::ReadAllText(filePath, s)) {
			// Fallback estándar si no se pudo leer mediante SlipFileSystem / SDL_RWops
			std::ifstream in(filePath, std::ios::in | std::ios::binary);
			if (in) {
				s = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			}
		}
	}

	template<typename T>
	inline void write(std::ostream& ostream, T& data)
	{
		ostream.write(reinterpret_cast<char*>(&data), sizeof(T));
	}

	inline bool exist(const std::string& filePath)
	{
		return SlipFileSystem::Exists(filePath);
	}
}
