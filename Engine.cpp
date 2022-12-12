#include "Engine.h"

#include <assert.h>

Engine::Engine()
{
	assert(!m_instance && "Engine has initialized...");
	m_instance = this;

	m_window = new SlipWindow("SlipEngine", 800, 600);
	m_input = new SlipInput();
	m_framebuffer = new SlipFrameBuffer();
	m_id = new SlipID();
	m_globals = new SlipGlobals();
	m_level = new SlipLevel("untilted");
	m_debug = new SlipDebug();

	m_editor = new SlipEditor(m_window->getWidth(), m_window->getHeight());

	m_framebuffer->init();
	m_editor->init();

	while (!glfwWindowShouldClose(&m_window->getWindow()))
	{
		m_editor->startRender();
		m_editor->renderHierarchy();
		m_editor->renderProperties();
		m_editor->renderGame(*m_framebuffer);
		m_editor->renderScene();
		m_editor->processWindow();
		m_editor->processInput();

		m_globals->update(glfwGetTime());

		m_framebuffer->bind();

		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		m_level->draw();

		m_framebuffer->unbind();
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		//framebuffer.draw();

		m_editor->endRender();

		glfwSwapBuffers(&m_window->getWindow());
		glfwPollEvents();
	}
	m_level->clean();

	glfwTerminate();

	return;
}
