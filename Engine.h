#pragma once

#include "SlipWindow.h"
#include "SlipInput.h"
#include "SlipFrameBuffer.h"
#include "SlipEditor.h"
#include "SlipGlobals.h"
#include "SlipLevel.h"
#include "SlipID.h"
#include "SlipDebug.h"

class Engine
{
private:
	SlipWindow* m_window;
	SlipInput* m_input;
	SlipFrameBuffer* m_framebuffer;
	SlipEditor* m_editor;
	SlipGlobals* m_globals;
	SlipLevel* m_level;
	SlipID* m_id;
	SlipDebug* m_debug;

	inline static Engine* m_instance;
public:
	Engine();

	inline static Engine& Get() { return *m_instance; }

	SlipWindow& Window() { return *m_window; }

	SlipInput& Input() { return *m_input; }

	SlipGlobals& Globals() { return *m_globals; }

	SlipLevel& Level() { return *m_level; }

	SlipCamera& GetPrimaryCamera() { return m_level->GetCamera(); }

	SlipID& IDs() { return *m_id; }

	~Engine() { delete m_instance; }
};

