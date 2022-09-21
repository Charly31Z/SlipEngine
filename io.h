#pragma once

#include <fstream>
#include <string>

namespace IO
{
	template<typename T>
	inline void read(std::iostream& istream, T& t)
	{
		istream.read(reinterpret_cast<char*>(&t), sizeof(T));
	}

	inline void readAllFromFile(const std::string& filePath, std::string& s)
	{
		std::ifstream in(filePath, std::ios::in | std::ios::binary);
		if (in) {
			s = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		}
	}

	template<typename T>
	inline void write(std::ostream& ostream, T& data)
	{
		ostream.write(reinterpret_cast<char*>(&data), sizeof(T));
	}

	inline bool exist(const std::string& filePath)
	{
		std::ifstream in(filePath, std::ios::in | std::ios::binary);
		return in.good();
	}
}