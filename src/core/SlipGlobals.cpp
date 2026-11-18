#include "SlipGlobals.h"

#include <assert.h>

SlipGlobals& SlipGlobals::Get() {
	if (!m_instance) {
		static SlipGlobals s_DefaultGlobals;
		m_instance = &s_DefaultGlobals;
	}
	return *m_instance;
}

void SlipGlobals::update(double time)
{
	currentFrame = static_cast<float>(time);
	if (lastFrame == 0.0f) {
		deltaTime = 1.0f / 60.0f;
	} else {
		deltaTime = currentFrame - lastFrame;
		if (deltaTime <= 0.0f) {
			deltaTime = 0.0001f;
		} else if (deltaTime > 0.2f) {
			deltaTime = 0.2f;
		}
	}
	lastFrame = currentFrame;
}

SlipGlobals::SlipGlobals()
{
	m_instance = this;
	currentFrame = 0.0f;
	deltaTime = 1.0f / 60.0f;
	fixedDeltaTime = 1.0f / 60.0f;
	lastFrame = 0.0f;
	camera = new SlipCamera();
}
