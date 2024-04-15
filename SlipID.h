#pragma once

#ifndef SLIP_ID_H
#define SLIP_ID_H

#include <cstdint>
#include <assert.h>

class SlipID
{
private:
	uint32_t m_Id;
	
	inline static SlipID* m_Instance;
public:
	inline static SlipID& Get() { return *m_Instance; }

	void reset() { m_Id = 0; }

	SlipID() : m_Id(0) {
		assert(!m_Instance && "SlipID has initialized...");
		m_Instance = this;
	}

	uint32_t next() { return m_Id++; }
};

#endif // !SLIP_ID_H
