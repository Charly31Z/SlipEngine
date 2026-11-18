#pragma once

#ifndef SLIP_ID_H
#define SLIP_ID_H

#include "SlipEngine.h"
#include <cstdint>

class SLIPENGINE_API SlipID
{
private:
	uint32_t m_Id = 1;
	inline static SlipID* m_Instance = nullptr;

public:
	inline static SlipID& Get() {
		static SlipID s_DefaultInstance;
		if (m_Instance) return *m_Instance;
		return s_DefaultInstance;
	}

	void reset() { m_Id = 1; }

	SlipID() : m_Id(1) {
		m_Instance = this;
	}

	uint32_t next() { return m_Id++; }
};

#endif // !SLIP_ID_H
