#include <vulkan/vulkan.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
struct Interface {
    unsigned location;
};
struct Binding {
    unsigned set, binding, type, count;
};
struct Metadata {
    VkShaderStageFlagBits stage {};
    std::vector<Interface> inputs, outputs;
    std::vector<Binding> bindings;
};
Metadata metadata(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Missing SPIR-V reflection metadata");
    Metadata result;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "STAGE") {
            unsigned stage;
            row >> stage;
            if (stage != 1 && stage != 16)
                throw std::runtime_error("Unsupported shader stage");
            result.stage = static_cast<VkShaderStageFlagBits>(stage);
        } else if (kind == "BIND") {
            Binding b {};
            row >> b.set >> b.binding >> b.type >> b.count;
            if (b.set > 3 || !b.count || b.count > 16 || b.type > 10)
                throw std::runtime_error("Unsupported descriptor metadata");
            result.bindings.push_back(b);
        } else if (kind == "IN" || kind == "OUT") {
            unsigned location, components, width, base, arrays;
            row >> location >> components >> width >> base >> arrays;
            if (components != 4 || width != 32 || base != 13 || arrays || location > 15)
                throw std::runtime_error("Unreviewed shader interface type");
            (kind == "IN" ? result.inputs : result.outputs).push_back({ location });
        } else if (kind == "INPUT_ATTACHMENT") {
            unsigned index;
            row >> index;
            if (index)
                throw std::runtime_error("Unreviewed input attachment index");
        } else
            throw std::runtime_error("Unknown metadata record");
        if (!row)
            throw std::runtime_error("Invalid shader metadata");
    }
    if (!result.stage)
        throw std::runtime_error("Missing shader stage");
    return result;
}

// Diagnostic companions only: supply the original fragment inputs, or consume
// all original vertex outputs. These are NOT substitutes for original UI draws.
std::vector<std::uint32_t> companion(bool vertex, const std::vector<Interface>& interface)
{
    std::vector<std::uint32_t> code { 0x07230203, 0x00010000, 0, 0, 0 };
    unsigned next = 1;
    const auto id = [&] { return next++; };
    const auto op = [&](unsigned opcode, std::vector<std::uint32_t> values) {
        code.push_back((unsigned(values.size() + 1) << 16) | opcode);
        code.insert(code.end(), values.begin(), values.end());
    };
    const auto void_type = id(), function_type = id(), scalar = id(), vector = id(), pointer = id();
    const auto output_pointer = vertex ? pointer : id(), zero = id(), one = id(), constant = id(), main = id(),
               label = id(), output = id();
    std::vector<unsigned> variables;
    for (std::size_t i = 0; i < interface.size(); ++i)
        variables.push_back(id());
    op(17, { 1 });
    op(14, { 0, 1 }); // Shader capability; Logical/GLSL450 memory model.
    std::vector<std::uint32_t> entry { vertex ? 0u : 4u, main, 0x6e69616d, 0, output };
    entry.insert(entry.end(), variables.begin(), variables.end());
    op(15, entry);
    if (!vertex)
        op(16, { main, 7 }); // OriginUpperLeft.
    op(71, { output, vertex ? 11u : 30u, 0 }); // Position builtin, or color location 0.
    for (std::size_t i = 0; i < interface.size(); ++i)
        op(71, { variables[i], 30, interface[i].location });
    op(19, { void_type });
    op(33, { function_type, void_type });
    op(22, { scalar, 32 });
    op(23, { vector, scalar, 4 });
    op(32, { pointer, vertex ? 3u : 1u, vector });
    if (!vertex)
        op(32, { output_pointer, 3, vector });
    op(43, { scalar, zero, 0 });
    op(43, { scalar, one, 0x3f800000 });
    op(44, { vector, constant, zero, zero, zero, one });
    op(59, { output_pointer, output, 3 });
    for (const auto variable : variables)
        op(59, { pointer, variable, vertex ? 3u : 1u });
    op(54, { void_type, main, 0, function_type });
    op(248, { label });
    auto value = constant;
    for (const auto variable : variables) {
        if (vertex)
            op(62, { variable, constant });
        else {
            const auto loaded = id(), sum = id();
            op(61, { vector, loaded, variable });
            op(129, { vector, sum, value, loaded });
            value = sum;
        }
    }
    op(62, { output, value });
    op(253, {});
    op(56, {});
    code[3] = next;
    return code;
}

struct Device {
    VkInstance instance {};
    VkDevice device {};
    VkRenderPass pass {};
    ~Device()
    {
        if (device) {
            if (pass)
                vkDestroyRenderPass(device, pass, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    void initialize()
    {
        VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "Independent PAF shader gate";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo create { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        create.pApplicationInfo = &app;
        require(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
        unsigned count = 0;
        require(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (!count)
            throw std::runtime_error("No Vulkan device");
        std::vector<VkPhysicalDevice> physical(count);
        require(vkEnumeratePhysicalDevices(instance, &count, physical.data()), "vkEnumeratePhysicalDevices");
        VkPhysicalDevice gpu = physical[0];
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(gpu, &properties);
        std::cout << "GPU " << properties.deviceName << " driver=" << properties.driverVersion << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues.data());
        unsigned family = 0;
        while (family < count && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            ++family;
        if (family == count)
            throw std::runtime_error("No graphics queue");
        const float priority = 1;
        VkDeviceQueueCreateInfo queue { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features {};
        vkGetPhysicalDeviceFeatures(gpu, &features);
        VkDeviceCreateInfo logical { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        logical.queueCreateInfoCount = 1;
        logical.pQueueCreateInfos = &queue;
        logical.pEnabledFeatures = &features;
        require(vkCreateDevice(gpu, &logical, nullptr, &device), "vkCreateDevice");
        VkAttachmentDescription attachment {};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        attachment.loadOp = attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentReference reference { 0, VK_IMAGE_LAYOUT_GENERAL };
        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        subpass.inputAttachmentCount = 1;
        subpass.pInputAttachments = &reference;
        VkSubpassDependency dependency { 0, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };
        VkRenderPassCreateInfo render { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        render.attachmentCount = 1;
        render.pAttachments = &attachment;
        render.subpassCount = 1;
        render.pSubpasses = &subpass;
        render.dependencyCount = 1;
        render.pDependencies = &dependency;
        require(vkCreateRenderPass(device, &render, nullptr, &pass), "vkCreateRenderPass");
    }
};
struct Pipeline {
    VkDevice device;
    VkPipeline pipeline {};
    VkPipelineLayout layout {};
    std::vector<VkDescriptorSetLayout> sets;
    std::vector<VkShaderModule> modules;
    ~Pipeline()
    {
        if (pipeline)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (layout)
            vkDestroyPipelineLayout(device, layout, nullptr);
        for (auto set : sets)
            vkDestroyDescriptorSetLayout(device, set, nullptr);
        for (auto module : modules)
            vkDestroyShaderModule(device, module, nullptr);
    }
    VkShaderModule module(const std::vector<std::uint32_t>& code)
    {
        VkShaderModuleCreateInfo create { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        create.codeSize = code.size() * 4;
        create.pCode = code.data();
        VkShaderModule result;
        require(vkCreateShaderModule(device, &create, nullptr, &result), "vkCreateShaderModule");
        modules.push_back(result);
        return result;
    }
};

void compile(Device& context, const std::filesystem::path& directory)
{
    const auto meta = metadata(directory / "shader.meta");
    std::ifstream input(directory / "shader.spv", std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (!input || size < 20 || size % 4 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid SPIR-V file");
    std::vector<std::uint32_t> code(std::size_t(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(code.data()), size);
    if (!input || code[0] != 0x07230203)
        throw std::runtime_error("Invalid SPIR-V contents");
    std::string entry_name;
    for (std::size_t position = 5; position < code.size();) {
        const auto count = code[position] >> 16, opcode = code[position] & 65535;
        if (!count || count > code.size() - position)
            throw std::runtime_error("Invalid SPIR-V instruction range");
        if (opcode == 15) {
            if (count < 5 || code[position + 1] != (meta.stage == VK_SHADER_STAGE_VERTEX_BIT ? 0u : 4u))
                throw std::runtime_error("SPIR-V entry stage differs from metadata");
            const auto* begin = reinterpret_cast<const char*>(code.data() + position + 3);
            const auto* end = begin + (count - 3) * 4;
            const auto* terminator = std::find(begin, end, '\0');
            if (terminator == end || !entry_name.empty())
                throw std::runtime_error("Unreviewed SPIR-V entry points");
            entry_name.assign(begin, terminator);
        }
        position += count;
    }
    if (entry_name.empty())
        throw std::runtime_error("Missing SPIR-V entry point");
    Pipeline resources { context.device };
    const bool vertex = meta.stage == VK_SHADER_STAGE_VERTEX_BIT;
    const auto original = resources.module(code),
               other = resources.module(companion(!vertex, vertex ? meta.outputs : meta.inputs));
    VkPipelineShaderStageCreateInfo stages[2] { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex ? original : other;
    stages[0].pName = vertex ? entry_name.c_str() : "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = vertex ? other : original;
    stages[1].pName = vertex ? "main" : entry_name.c_str();
    std::map<unsigned, std::vector<VkDescriptorSetLayoutBinding>> bindings;
    unsigned last = 0;
    for (const auto& b : meta.bindings) {
        last = std::max(last, b.set);
        bindings[b.set].push_back({ b.binding, static_cast<VkDescriptorType>(b.type), b.count, meta.stage, nullptr });
    }
    for (unsigned i = 0; i <= last; ++i) {
        auto& list = bindings[i];
        VkDescriptorSetLayoutCreateInfo create { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        create.bindingCount = list.size();
        create.pBindings = list.data();
        VkDescriptorSetLayout layout;
        require(vkCreateDescriptorSetLayout(context.device, &create, nullptr, &layout), "vkCreateDescriptorSetLayout");
        resources.sets.push_back(layout);
    }
    VkPipelineLayoutCreateInfo layout { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout.setLayoutCount = resources.sets.size();
    layout.pSetLayouts = resources.sets.data();
    require(vkCreatePipelineLayout(context.device, &layout, nullptr, &resources.layout), "vkCreatePipelineLayout");
    std::vector<VkVertexInputAttributeDescription> attributes;
    if (vertex)
        for (const auto& a : meta.inputs)
            attributes.push_back({ a.location, 0, VK_FORMAT_R32G32B32A32_SFLOAT, a.location * 16 });
    VkVertexInputBindingDescription binding { 0, 256, VK_VERTEX_INPUT_RATE_VERTEX };
    VkPipelineVertexInputStateCreateInfo vertices { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertices.vertexBindingDescriptionCount = attributes.empty() ? 0 : 1;
    vertices.pVertexBindingDescriptions = &binding;
    vertices.vertexAttributeDescriptionCount = attributes.size();
    vertices.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport { 0, 0, 8, 8, 0, 1 };
    VkRect2D scissor { { 0, 0 }, { 8, 8 } };
    VkPipelineViewportStateCreateInfo view { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    view.viewportCount = 1;
    view.pViewports = &viewport;
    view.scissorCount = 1;
    view.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState color {};
    color.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    VkGraphicsPipelineCreateInfo pipeline { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertices;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &view;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples;
    pipeline.pColorBlendState = &blend;
    pipeline.layout = resources.layout;
    pipeline.renderPass = context.pass;
    require(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &resources.pipeline),
        "vkCreateGraphicsPipelines");
}
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: paf_shader_pipeline_test SPIRV_DIRECTORY EXPECTED_COUNT\n";
        return 2;
    }
    try {
        std::cout << std::unitbuf;
        const auto expected = std::stoul(argv[2]);
        if (!expected)
            throw std::runtime_error("Expected count must be positive");
        Device context;
        context.initialize();
        unsigned passed = 0, failed = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1])) {
            if (entry.path().filename() != "shader.meta")
                continue;
            std::cout << "PIPELINE " << entry.path().parent_path().filename().string() << '\n';
            try {
                compile(context, entry.path().parent_path());
                ++passed;
            } catch (const std::exception& error) {
                ++failed;
                std::cout << "FAIL " << error.what() << '\n';
            }
        }
        std::cout << "RESULT passed=" << passed << " failed=" << failed << " expected=" << expected << '\n';
        if (failed || passed != expected)
            return 1;
        std::cout << "PASS diagnostic pipeline creation only; no original UI draw or pixel equivalence tested\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
