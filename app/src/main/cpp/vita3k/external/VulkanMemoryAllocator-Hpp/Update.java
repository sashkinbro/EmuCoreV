import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * <b>Java 16+</b>
 * <pre>{@code java Update.java vma_revision vk_hpp_revision}</pre>
 * Example: {@code java Update.java v3.0.1 main}
 */
public class Update {

    private static void exec(boolean ignoreErrors, String directory, String... command) throws IOException, InterruptedException {
        ProcessBuilder generator = new ProcessBuilder(command);
        generator.directory(new File(directory));
        generator.redirectOutput(ProcessBuilder.Redirect.INHERIT);
        generator.redirectError(ignoreErrors ? ProcessBuilder.Redirect.DISCARD : ProcessBuilder.Redirect.INHERIT);
        int code = generator.start().waitFor();
        if (code != 0 && !ignoreErrors) System.exit(code);
    }
    private static void exec(String directory, String... command) throws IOException, InterruptedException {
        exec(false, directory, command);
    }
    private static void execIgnoringErrors(String directory, String... command) throws IOException, InterruptedException {
        exec(true, directory, command);
    }

    private static String findRegex(String text, String regex, String errorMessage) {
        return Pattern.compile(regex).matcher(text).results().findFirst()
                .orElseThrow(() -> new Error(errorMessage)).group(1);
    }

    private static String findVulkanVersion(String text) {
        Matcher m = Pattern.compile("VK_VERSION_(\\d+)_(\\d+)").matcher(text);
        int major = 0, minor = 0;
        while (m.find()) {
            int mj = Integer.parseInt(m.group(1)), mn = Integer.parseInt(m.group(2));
            if (mj > major || (mj == major && mn > minor)) {
                major = mj;
                minor = mn;
            }
        }
        if (major == 0 && minor == 0) throw new Error("Cannot detect Vulkan version");
        return major + "." + minor;
    }

    private static String replaceReadme(String text, String... replacements) {
        for (int i = 0; i < replacements.length / 2; i++) {
            String key = replacements[i * 2], value = replacements[i * 2 + 1];
            text = text.replaceAll("<!--" + key + "-->.*?<!--/" + key + "-->", "<!--" + key + "-->" + value + "<!--/" + key + "-->");
        }
        return text;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("Usage: java Update.java vma_revision vk_headers_revision");
            return;
        }

        if (!Files.exists(Path.of("VulkanMemoryAllocator/.git")) || !Files.exists(Path.of("Vulkan-Headers/.git"))) {
            System.out.println("Initializing submodules...");
            exec(".", "git", "submodule", "update");
            System.out.println();
        }

        System.out.println("Updating VulkanMemoryAllocator...");
        exec("VulkanMemoryAllocator", "git", "-c", "advice.detachedHead=false", "checkout", args[0]);
        execIgnoringErrors("VulkanMemoryAllocator", "git", "pull");
        System.out.println();

        System.out.println("Updating Vulkan-Headers...");
        exec("Vulkan-Headers", "git", "-c", "advice.detachedHead=false", "checkout", args[1]);
        execIgnoringErrors("Vulkan-Headers", "git", "pull");
        System.out.println();

        Path vmaFile = Path.of("VulkanMemoryAllocator/include/vk_mem_alloc.h");
        String content = Files.readString(vmaFile);
        String version = findRegex(content, "<b>Version\\s*(.+?)\\s*</b>", "Cannot extract version");
        String vk = findVulkanVersion(content);
        System.out.println("VMA version: " + version);
        System.out.println("Vulkan version: " + vk);
        System.out.println();

        Path readmePath = Path.of("README.md");
        Files.writeString(readmePath, replaceReadme(Files.readString(readmePath),
                "VER", version,
                "VK", vk));

        System.out.println("Generating C++ bindings...");
        exec(".", System.getProperty("java.home") + "/bin/java", "Generate.java", vmaFile.toString());
    }
}
