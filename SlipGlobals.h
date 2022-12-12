#pragma once

#ifndef SLIP_GLOBALS_H
#define SLIP_GLOBALS_H

class SlipGlobals
{
private:
	float currentFrame, deltaTime, lastFrame;

	inline static SlipGlobals* m_instance;
public:
	inline static SlipGlobals& Get() { return *m_instance; }

	float GetDeltaTime() { return deltaTime; }

	void update(double time);

	SlipGlobals();
};

#endif // !SLIP_GLOBALS_H
