#include <indium/render-pass-key.private.hpp>
#include <cassert>
#include <unordered_map>
#include <cstdio>

int main() {
    VkAttachmentDescription a {};
    a.format = VK_FORMAT_R8G8B8A8_UNORM; a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference colors[] {{0, VK_IMAGE_LAYOUT_GENERAL}, {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_GENERAL}};
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colors;
    VkSubpassDependency dep {};
    dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    VkRenderPassCreateInfo info {};
    info.attachmentCount = 1; info.pAttachments = &a;
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = 1; info.pDependencies = &dep;
    auto original = *Indium::renderPassKey(info);
    auto changed = [&] { assert(!(*Indium::renderPassKey(info) == original)); };
    auto checkAttachment = [&](auto& field, auto value) {
        auto saved = field; field = value; changed(); field = saved;
    };
    checkAttachment(a.loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
    checkAttachment(a.storeOp, VK_ATTACHMENT_STORE_OP_DONT_CARE);
    checkAttachment(a.stencilLoadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
    checkAttachment(a.initialLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    checkAttachment(a.finalLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    checkAttachment(a.format, VK_FORMAT_B8G8R8A8_UNORM);
    colors[1].attachment = 0; changed(); colors[1].attachment = VK_ATTACHMENT_UNUSED;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT; changed(); dep.dependencyFlags = 0;
    VkAttachmentDescription copy = a;
    info.pAttachments = &copy;
    assert(*Indium::renderPassKey(info) == original); // Addresses and padding are irrelevant.
    std::unordered_map<Indium::RenderPassKey, int, Indium::RenderPassKey::Hash> cache;
    cache.emplace(original, 42);
    assert(cache.at(*Indium::renderPassKey(info)) == 42);
    info.pNext = &info; assert(!Indium::renderPassKey(info)); info.pNext = nullptr;
    subpass.pResolveAttachments = colors; assert(!Indium::renderPassKey(info)); subpass.pResolveAttachments = nullptr;
    std::array<VkAttachmentDescription,16> attachments {};
    std::array<VkAttachmentReference,16> references {};
    std::array<VkSubpassDependency,2> dependencies {};
    info.attachmentCount = attachments.size(); info.pAttachments = attachments.data();
    subpass.colorAttachmentCount = references.size(); subpass.pColorAttachments = references.data();
    subpass.pDepthStencilAttachment = references.data();
    info.dependencyCount = dependencies.size(); info.pDependencies = dependencies.data();
    auto maximum = Indium::renderPassKey(info); assert(maximum && maximum->size <= maximum->words.size());
    info.attachmentCount = 17; assert(!Indium::renderPassKey(info));
    puts("PASS: exact render-pass preservation, layout, holes, bounded key and unsupported fallback");
}
