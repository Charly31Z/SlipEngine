#include "SlipGlobals.h"

#include <assert.h>

void SlipGlobals::update(double time)
{
	currentFrame = static_cast<float>(time);
	deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;
}

SlipGlobals::SlipGlobals()
{
	assert(!m_instance && "SlipGlobals has initialized...");
	m_instance = this;
}
