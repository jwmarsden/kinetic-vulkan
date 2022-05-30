#pragma once

#include "log.h"
#include "k3/logging/log_manager.hpp"

#include "k3/graphics/window.hpp"
#include "k3/graphics/graphics.hpp"

namespace k3::controller {

    class WindowBehaviorController {

        public:

            struct KeyMappings {
                int escapeWindow = GLFW_KEY_ESCAPE;
            };

            WindowBehaviorController() = default;

            void init(std::shared_ptr<k3::graphics::KeWindow> window, std::shared_ptr<k3::graphics::KeGraphics> m_graphics);

            void shutdown();

        private:

            static void windowFocusCallback(GLFWwindow* glfwWindow, int focused);

            static void windowMaximizeCallback(GLFWwindow* glfwWindow, int maximized);

            static void keyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);

            static void cursorPositionCallback(GLFWwindow* glfwWindow, double xpos, double ypos);

            static void mouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods);

            void registerForWindowEvents();

            bool m_initFlag = false;

            std::shared_ptr<k3::graphics::KeWindow> m_window = nullptr;

            std::shared_ptr<k3::graphics::KeGraphics> m_graphics = nullptr;



    };

}