#include <indium/submission-batch.private.hpp>
#include <cassert>
#include <iostream>

int main() {
    Indium::SubmissionBatch batch;
    unsigned calls = 0;
    auto noSubmit = [&](uint32_t, const VkSubmitInfo2*) { ++calls; return VK_SUCCESS; };
    assert(batch.flush(noSubmit) == VK_SUCCESS && calls == 0);
    for (uint64_t i = 1; i <= 128; ++i) {
        VkCommandBufferSubmitInfo command {};
        command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        VkSemaphoreSubmitInfo wait {}, signal {};
        wait.sType = signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        wait.semaphore = signal.semaphore = reinterpret_cast<VkSemaphore>(uintptr_t(999));
        wait.value = i - 1; signal.value = i;
        wait.stageMask = signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 info {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        info.commandBufferInfoCount = 1; info.pCommandBufferInfos = &command;
        info.waitSemaphoreInfoCount = 1; info.pWaitSemaphoreInfos = &wait;
        info.signalSemaphoreInfoCount = 1; info.pSignalSemaphoreInfos = &signal;
        batch.append(info);
        // All source structures die here. Their pointers must not be retained.
    }
    assert(batch.full());
    auto verify = [&](uint32_t count, const VkSubmitInfo2* infos) {
        ++calls; assert(count == 128);
        for (uint64_t i = 0; i < count; ++i) {
            assert(infos[i].sType == VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            assert(infos[i].commandBufferInfoCount == 1);
            assert(infos[i].pCommandBufferInfos[0].commandBuffer == reinterpret_cast<VkCommandBuffer>(i + 1));
            assert(infos[i].pWaitSemaphoreInfos[0].value == i);
            assert(infos[i].pSignalSemaphoreInfos[0].value == i + 1);
            assert(infos[i].pSignalSemaphoreInfos[0].stageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        }
        return VK_SUCCESS;
    };
    assert(batch.flush(verify) == VK_SUCCESS && batch.empty() && calls == 1);
    VkCommandBufferSubmitInfo command {};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    VkSubmitInfo2 emptySemaphores {};
    emptySemaphores.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    emptySemaphores.commandBufferInfoCount = 1;
    emptySemaphores.pCommandBufferInfos = &command;
    batch.append(emptySemaphores);
    assert(batch.flush([](uint32_t count, const VkSubmitInfo2* infos) {
        assert(count == 1 && infos[0].waitSemaphoreInfoCount == 0 && infos[0].signalSemaphoreInfoCount == 0);
        return VK_ERROR_DEVICE_LOST;
    }) == VK_ERROR_DEVICE_LOST && !batch.empty());
    assert(batch.flush(noSubmit) == VK_SUCCESS && batch.empty());
    std::cout << "submission copy, ordering, capacity, reuse and failure tests passed\n";
}
