#include "k3/graphics/swapchain.hpp"

namespace k3::graphics { 

    void KeSwapChain::init(std::shared_ptr<KeDevice> device, VkExtent2D windowExtent) {
        KE_IN("m_device<{}>,<{},{}>", fmt::ptr(&device), windowExtent.width, windowExtent.height);
        assert(!m_initFlag && "Already had init.");
        m_device = device;
        m_windowExtent = windowExtent;

        KE_DEBUG("Init Systems");
        initSystems();
        KE_OUT(KE_NOARG);  
    }

    void KeSwapChain::init(std::shared_ptr<KeDevice> device, VkExtent2D windowExtent, std::unique_ptr<KeSwapChain> previous) {
        KE_IN("(m_device<{}>,<{},{}>,previous@<{}>)", fmt::ptr(&device), windowExtent.width, windowExtent.height, fmt::ptr(&previous));
        assert(!m_initFlag && "Already had init.");
        m_device = device;
        m_windowExtent = windowExtent;

        KE_DEBUG("Init Systems");
        oldSwapChain = std::move(previous);
        initSystems();
        if(!compareSwapFormats(*oldSwapChain.get())) {
            // TODO Make a callback to application to trigger update. 
            KE_CRITICAL("Swapchain image or depth format has changed!");
            throw std::runtime_error("Swapchain image or depth format has changed!");
        }

        KE_DEBUG("Cleanup previous swapchain.");
        oldSwapChain->shutdown();
        oldSwapChain = nullptr;
        KE_OUT(KE_NOARG);  
    }

    void KeSwapChain::initSystems() {
        KE_IN(KE_NOARG);  
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDepthResources();
        createFramebuffers();
        createSyncObjects();
        m_initFlag = true;
        KE_OUT(KE_NOARG);  
    }

    void KeSwapChain::shutdown() {
        KE_IN(KE_NOARG);
        assert(m_initFlag && "Must have been init to shutdown.");
        if(m_initFlag) {
            m_initFlag = false;
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                vkDestroySemaphore(m_device->getDevice(), m_renderFinishedSemaphores[i], nullptr);
                vkDestroySemaphore(m_device->getDevice(), m_imageAvailableSemaphores[i], nullptr);
                vkDestroyFence(m_device->getDevice(), m_inFlightFences[i], nullptr);
            }

            for (auto framebuffer : m_swapChainFramebuffers) {
                vkDestroyFramebuffer(m_device->getDevice(), framebuffer, nullptr);
            }

            for (int i = 0; i < m_depthImages.size(); i++) {
                vkDestroyImageView(m_device->getDevice(), m_depthImageViews[i], nullptr);
                vkDestroyImage(m_device->getDevice(), m_depthImages[i], nullptr);
                vkFreeMemory(m_device->getDevice(), m_depthImageMemorys[i], nullptr);
            }

            if(m_renderPass != nullptr) {
                vkDestroyRenderPass(m_device->getDevice(), m_renderPass, nullptr);
                m_renderPass = nullptr;
            }

            for (auto imageView : m_swapChainImageViews) {
                vkDestroyImageView(m_device->getDevice(), imageView, nullptr);
            }
            m_swapChainImageViews.clear();

            if (m_swapChain != nullptr) {
                vkDestroySwapchainKHR(m_device->getDevice(), m_swapChain, nullptr);
                m_swapChain = nullptr;
            }
        }
        KE_OUT(KE_NOARG);
    }

    void KeSwapChain::createSwapChain() {
        KE_IN(KE_NOARG);
        SwapChainSupportDetails swapChainSupport = m_device->getSwapChainSupport();

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_device->getSurface();

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        QueueFamilyIndices indices = m_device->findPhysicalQueueFamilies();
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;      // Optional
            createInfo.pQueueFamilyIndices = nullptr;  // Optional
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if(oldSwapChain == nullptr) {
            createInfo.oldSwapchain = VK_NULL_HANDLE;
        } else {
            KE_DEBUG("Found Old Swapchain. Adding to VkSwapchainCreateInfoKHR.");
            createInfo.oldSwapchain = oldSwapChain->m_swapChain;
        }
        

        if (vkCreateSwapchainKHR(m_device->getDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
            KE_CRITICAL("failed to create swap chain!");
        }

        // we only specified a minimum number of images in the swap chain, so the implementation is
        // allowed to create a swap chain with more. That's why we'll first query the final number of
        // images with vkGetSwapchainImagesKHR, then resize the container and finally call it again to
        // retrieve the handles.
        vkGetSwapchainImagesKHR(m_device->getDevice(), m_swapChain, &imageCount, nullptr);
        m_swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_device->getDevice(), m_swapChain, &imageCount, m_swapChainImages.data());

        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainExtent = extent;
        
        KE_OUT("(): m_swapChain@<{}>, m_swapChainImageFormat#{}, <{},{}>", fmt::ptr(&m_swapChain), m_swapChainImageFormat, m_swapChainExtent.width, m_swapChainExtent.height);
    }

    VkSurfaceFormatKHR KeSwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
        KE_IN(KE_NOARG);
        for (const auto &availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        KE_OUT(KE_NOARG);
        return availableFormats[0];
    }

    VkPresentModeKHR KeSwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
        KE_IN(KE_NOARG);
        for (const auto &availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                KE_INFO("Kinetic Selected Present Mode: \"Mailbox\".");
                return availablePresentMode;
            }
        }

        for (const auto &availablePresentMode : availablePresentModes) {
           if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
             KE_INFO("Present mode: Immediate");
             return availablePresentMode;
           }
        }

        KE_INFO("Kinetic Selected Present Mode: \"V-Sync\".");
        KE_OUT(KE_NOARG);
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D KeSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
        KE_IN(KE_NOARG);
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            VkExtent2D actualExtent = m_windowExtent;
            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
            return actualExtent;
        }
        KE_OUT(KE_NOARG);
    }

    void KeSwapChain::createImageViews() {
        KE_IN(KE_NOARG);
        m_swapChainImageViews.resize(m_swapChainImages.size());
        for (size_t i = 0; i < m_swapChainImages.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_swapChainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_swapChainImageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
                KE_ERROR("failed to create texture image view!");
            }
        }
        KE_OUT("(): m_swapChainImageViews[{}]@<{}>", m_swapChainImageViews.size(), fmt::ptr(&m_swapChainImageViews));
    }

    void KeSwapChain::createRenderPass() {
        KE_IN(KE_NOARG);
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = getSwapChainImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcAccessMask = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstSubpass = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_device->getDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            KE_CRITICAL("failed to create render pass!");
        }
        KE_OUT("(): m_renderPass@<{}>", fmt::ptr(&m_renderPass));
    }

    VkFormat KeSwapChain::findDepthFormat() {
        KE_IN(KE_NOARG);
        VkFormat format = m_device->findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        KE_OUT(KE_NOARG);
        return format;
    }

    void KeSwapChain::createDepthResources() {
        KE_IN(KE_NOARG);
        VkFormat depthFormat = findDepthFormat();
        m_swapChainDepthFormat = depthFormat;
        VkExtent2D swapChainExtent = getSwapChainExtent();

        m_depthImages.resize(imageCount());
        m_depthImageMemorys.resize(imageCount());
        m_depthImageViews.resize(imageCount());

        for (int i = 0; i < m_depthImages.size(); i++) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapChainExtent.width;
            imageInfo.extent.height = swapChainExtent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = depthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = 0;

            m_device->createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImages[i], m_depthImageMemorys[i]);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_depthImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &m_depthImageViews[i]) != VK_SUCCESS) {
                KE_CRITICAL("failed to create texture image view!");
                throw std::runtime_error("failed to create texture image view!");
            }
        }
        KE_OUT("(): m_depthImages[{}]@<{}>, m_depthImageMemorys[{}]@<{}>", m_depthImages.size(), fmt::ptr(&m_depthImages), m_depthImageMemorys.size(), fmt::ptr(&m_depthImageMemorys));
    }

    void KeSwapChain::createFramebuffers() {
        KE_IN(KE_NOARG);
        m_swapChainFramebuffers.resize(imageCount());
        for (size_t i = 0; i < imageCount(); i++) {
            std::array<VkImageView, 2> attachments = {m_swapChainImageViews[i], m_depthImageViews[i]};

            VkExtent2D swapChainExtent = getSwapChainExtent();
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(m_device->getDevice(), &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
                KE_CRITICAL("failed to create framebuffer!");
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
        KE_OUT("(): m_swapChainFramebuffers[{}]@<{}>", m_swapChainFramebuffers.size(), fmt::ptr(&m_swapChainFramebuffers));
    }

    void KeSwapChain::createSyncObjects() {
        KE_IN(KE_NOARG);
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        m_imagesInFlight.resize(imageCount(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(m_device->getDevice() , &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_device->getDevice() , &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_device->getDevice() , &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
                KE_CRITICAL("failed to create synchronization objects for a frame!");
                throw std::runtime_error("failed to create synchronization objects for a frame!");
            }
        }
        KE_OUT("(): m_imageAvailableSemaphores[{}]@<{}>, m_renderFinishedSemaphores[{}]@<{}>, m_inFlightFences[{}]@<{}>", m_imageAvailableSemaphores.size(), fmt::ptr(&m_imageAvailableSemaphores), m_renderFinishedSemaphores.size(), fmt::ptr(&m_renderFinishedSemaphores), m_inFlightFences.size(), fmt::ptr(&m_inFlightFences));
    }

    VkResult KeSwapChain::acquireNextImage(uint32_t *imageIndex) {
        vkWaitForFences(m_device->getDevice() , 1, &m_inFlightFences[m_currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());

        VkResult result = vkAcquireNextImageKHR(m_device->getDevice() , m_swapChain, std::numeric_limits<uint64_t>::max(), 
            m_imageAvailableSemaphores[m_currentFrame],  // must be a not signaled semaphore
            VK_NULL_HANDLE, imageIndex);
        return result;
    }

    VkResult KeSwapChain::submitCommandBuffers(const VkCommandBuffer *buffers, uint32_t *imageIndex) {
        if (m_imagesInFlight[*imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(m_device->getDevice() , 1, &m_imagesInFlight[*imageIndex], VK_TRUE, UINT64_MAX);
        }
        m_imagesInFlight[*imageIndex] = m_inFlightFences[m_currentFrame];

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = buffers;

        VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(m_device->getDevice() , 1, &m_inFlightFences[m_currentFrame]);
        if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {m_swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;

        presentInfo.pImageIndices = imageIndex;

        auto result = vkQueuePresentKHR(m_device->presentQueue(), &presentInfo);

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        return result;
    }
}