#include "WindowerPlugin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d8.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {
HMODULE g_module = nullptr;

void module_log_path(char* output, std::size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    output[0] = '\0';

    char module_path[MAX_PATH] {};
    if (!g_module || !GetModuleFileNameA(g_module, module_path, sizeof(module_path))) {
        return;
    }

    char* slash = std::strrchr(module_path, '\\');
    if (!slash) {
        return;
    }

    *slash = '\0';
    std::snprintf(output, output_size, "%s\\settings\\TargetLines\\native.log", module_path);
}

void append_module_log(const char* message) {
    char path[MAX_PATH] {};
    module_log_path(path, sizeof(path));
    if (path[0] == '\0') {
        return;
    }

    FILE* file = std::fopen(path, "ab");
    if (!file) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    if (local) {
        char stamp[32] {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
        std::fprintf(file, "%s %s\n", stamp, message);
    } else {
        std::fprintf(file, "%s\n", message);
    }

    std::fclose(file);
}
}

struct ModuleRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};

class TargetLinesPlugin final : public PluginBase {
public:
    const char* __stdcall GetPluginAuthor() override {
        return "MogSafe";
    }

    const char* __stdcall GetPluginName() override {
        return "targetlines";
    }

    void __stdcall Load(PluginManager* manager) override {
        plugin_manager_ = manager;
        QueryPerformanceFrequency(&performance_frequency_);
        append_module_log("Load called");
        initialize_paths_from_module();
        append_log("loaded");
        probe_device("load");
    }

    void __stdcall Unload() override {
        append_log("unloaded");
    }

    void __stdcall PluginCommand(const char* command) override {
        if (!command || std::strcmp(command, "status") == 0) {
            char message[1024] {};
            std::snprintf(message, sizeof(message),
                "status: enabled=%s debug_bar=%s source_height=%.3f target_height=%.3f dynamic_bone=%d lines=%d projection=d3d-matrix-3dbezier",
                overlay_enabled_ ? "true" : "false",
                debug_bar_enabled_ ? "true" : "false",
                source_height_offset_,
                target_height_offset_,
                dynamic_bone_,
                count_lines());
            append_log(message);
            return;
        }

        if (std::strcmp(command, "count") == 0) {
            char message[512] {};
            std::snprintf(message, sizeof(message), "lines=%d", count_lines());
            append_log(message);
            return;
        }

        if (std::strcmp(command, "path") == 0) {
            char message[1024] {};
            std::snprintf(message, sizeof(message), "state=%s log=%s", state_path_, log_path_);
            append_log(message);
            return;
        }

        if (std::strcmp(command, "renderstats") == 0) {
            char message[512] {};
            std::snprintf(message, sizeof(message), "postrender_calls=%lu last_line_count=%d", postrender_calls_, last_line_count_);
            append_log(message);
            return;
        }

        if (std::strcmp(command, "device") == 0) {
            probe_device("command");
            return;
        }

        if (std::strcmp(command, "matrixprobe") == 0) {
            probe_matrices("command");
            return;
        }

        if (std::strcmp(command, "matrixnext") == 0) {
            matrix_probe_pending_ = true;
            append_log("matrix probe queued for next postrender");
            return;
        }

        if (std::strcmp(command, "ffxiprobe") == 0) {
            probe_ffxi_interface();
            return;
        }

        if (std::strcmp(command, "luamobprobe") == 0) {
            probe_luacore_mob_array();
            return;
        }

        if (std::strcmp(command, "boneprobe") == 0 || std::strcmp(command, "anchorprobe") == 0) {
            probe_bone_anchors();
            return;
        }

        if (std::strncmp(command, "dynamicbone", 11) == 0) {
            const char* value = command + 11;
            while (*value == ' ') {
                ++value;
            }

            if (*value == '\0') {
                char message[128] {};
                std::snprintf(message, sizeof(message), "dynamic_bone=%d", dynamic_bone_);
                append_log(message);
                return;
            }

            if (std::strcmp(value, "off") == 0 || std::strcmp(value, "none") == 0 || std::strcmp(value, "-1") == 0) {
                dynamic_bone_ = -1;
                append_log("dynamic_bone=off");
                return;
            }

            if (std::strcmp(value, "auto") == 0 || std::strcmp(value, "on") == 0 || std::strcmp(value, "default") == 0) {
                dynamic_bone_ = 21;
                append_log("dynamic_bone=21");
                return;
            }

            const int bone = std::atoi(value);
            if (bone < 0 || bone > 255) {
                append_log("dynamic_bone invalid; expected auto, off, or 0-255");
                return;
            }

            dynamic_bone_ = bone;
            char message[128] {};
            std::snprintf(message, sizeof(message), "dynamic_bone=%d", dynamic_bone_);
            append_log(message);
            return;
        }

        if (std::strncmp(command, "height ", 7) == 0) {
            source_height_offset_ = static_cast<float>(std::strtod(command + 7, nullptr));
            target_height_offset_ = source_height_offset_;
            char message[128] {};
            std::snprintf(message, sizeof(message), "height=%.3f", source_height_offset_);
            append_log(message);
            return;
        }

        if (std::strncmp(command, "sourceheight ", 13) == 0) {
            source_height_offset_ = static_cast<float>(std::strtod(command + 13, nullptr));
            char message[128] {};
            std::snprintf(message, sizeof(message), "source_height=%.3f", source_height_offset_);
            append_log(message);
            return;
        }

        if (std::strncmp(command, "targetheight ", 13) == 0) {
            target_height_offset_ = static_cast<float>(std::strtod(command + 13, nullptr));
            char message[128] {};
            std::snprintf(message, sizeof(message), "target_height=%.3f", target_height_offset_);
            append_log(message);
            return;
        }

        if (std::strcmp(command, "heightup") == 0) {
            source_height_offset_ -= 0.25f;
            target_height_offset_ -= 0.25f;
            char message[128] {};
            std::snprintf(message, sizeof(message), "height=%.3f", source_height_offset_);
            append_log(message);
            return;
        }

        if (std::strcmp(command, "heightdown") == 0) {
            source_height_offset_ += 0.25f;
            target_height_offset_ += 0.25f;
            char message[128] {};
            std::snprintf(message, sizeof(message), "height=%.3f", source_height_offset_);
            append_log(message);
            return;
        }

        if (std::strcmp(command, "height0") == 0) {
            source_height_offset_ = 0.0f;
            target_height_offset_ = 0.0f;
            append_log("height=0.000");
            return;
        }

        if (std::strcmp(command, "drawtest") == 0) {
            debug_bar_enabled_ = !debug_bar_enabled_;
            char message[128] {};
            std::snprintf(message, sizeof(message), "drawtest=%s", debug_bar_enabled_ ? "on" : "off");
            append_log(message);
            return;
        }

        if (std::strcmp(command, "drawon") == 0) {
            overlay_enabled_ = true;
            append_log("overlay=on");
            return;
        }

        if (std::strcmp(command, "drawoff") == 0) {
            overlay_enabled_ = false;
            append_log("overlay=off");
            return;
        }

        char unknown[512] {};
        std::snprintf(unknown, sizeof(unknown), "unknown command=%s", command);
        append_log(unknown);
    }

    void __stdcall PostRender() override {
        LARGE_INTEGER frame_counter {};
        QueryPerformanceCounter(&frame_counter);
        const float frame_ms = previous_postrender_counter_ > 0
            ? counter_milliseconds(frame_counter.QuadPart - previous_postrender_counter_)
            : 0.0f;
        previous_postrender_counter_ = frame_counter.QuadPart;
        benchmark_current_projections_ = 0;
        benchmark_current_draw_calls_ = 0;
        benchmark_current_state_reads_ = 0;
        projection_matrices_valid_ = false;

        ++postrender_calls_;

        if (postrender_calls_ == 1 || (postrender_calls_ % 300) == 0) {
            last_line_count_ = count_lines();
            char message[512] {};
            std::snprintf(message, sizeof(message), "postrender heartbeat calls=%lu lines=%d", postrender_calls_, last_line_count_);
            append_log(message);
        }

        if (debug_bar_enabled_) {
            draw_test_bar();
        }

        if (overlay_enabled_) {
            LARGE_INTEGER overlay_start {};
            LARGE_INTEGER overlay_end {};
            QueryPerformanceCounter(&overlay_start);
            draw_lines();
            QueryPerformanceCounter(&overlay_end);
            if (benchmark_enabled_) {
                record_benchmark_sample(frame_ms,
                    counter_milliseconds(overlay_end.QuadPart - overlay_start.QuadPart));
            }
        }

        if (matrix_probe_pending_) {
            matrix_probe_pending_ = false;
            probe_matrices("postrender");
        }
    }

private:
    void probe_device(const char* reason) {
        void* device = nullptr;
        if (plugin_manager_) {
            device = plugin_manager_->GetDirect3D8Device();
        }

        char message[256] {};
        std::snprintf(message, sizeof(message), "device probe reason=%s manager=%p device=%p", reason ? reason : "unknown", plugin_manager_, device);
        append_log(message);
        d3d_device_ = static_cast<IDirect3DDevice8*>(device);
    }

    void probe_matrices(const char* reason) {
        if (!d3d_device_) {
            probe_device("matrix");
        }

        if (!d3d_device_) {
            append_log("matrix probe skipped: no D3D8 device");
            return;
        }

        D3DMATRIX view {};
        D3DMATRIX projection {};
        D3DMATRIX world {};
        const HRESULT view_result = d3d_device_->GetTransform(D3DTS_VIEW, &view);
        const HRESULT projection_result = d3d_device_->GetTransform(D3DTS_PROJECTION, &projection);
        const HRESULT world_result = d3d_device_->GetTransform(D3DTS_WORLD, &world);

        log_matrix("view", reason, view_result, view);
        log_matrix("projection", reason, projection_result, projection);
        log_matrix("world", reason, world_result, world);
    }

    void log_matrix(const char* label, const char* reason, HRESULT result, const D3DMATRIX& matrix) {
        char message[1024] {};
        std::snprintf(message, sizeof(message),
            "matrix %s reason=%s hr=0x%08lx rows=[%.6f %.6f %.6f %.6f] [%.6f %.6f %.6f %.6f] [%.6f %.6f %.6f %.6f] [%.6f %.6f %.6f %.6f]",
            label ? label : "unknown",
            reason ? reason : "unknown",
            static_cast<unsigned long>(result),
            matrix.m[0][0], matrix.m[0][1], matrix.m[0][2], matrix.m[0][3],
            matrix.m[1][0], matrix.m[1][1], matrix.m[1][2], matrix.m[1][3],
            matrix.m[2][0], matrix.m[2][1], matrix.m[2][2], matrix.m[2][3],
            matrix.m[3][0], matrix.m[3][1], matrix.m[3][2], matrix.m[3][3]);
        append_log(message);
    }

    void module_label_for_address(std::uintptr_t address, char* output, std::size_t output_size) const {
        if (!output || output_size == 0) {
            return;
        }

        output[0] = '\0';
        MEMORY_BASIC_INFORMATION mbi {};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) || !mbi.AllocationBase) {
            std::snprintf(output, output_size, "module=unknown");
            return;
        }

        char path[MAX_PATH] {};
        HMODULE module = static_cast<HMODULE>(mbi.AllocationBase);
        if (!GetModuleFileNameA(module, path, sizeof(path))) {
            std::snprintf(output, output_size, "module=%08lx", reinterpret_cast<unsigned long>(module));
            return;
        }

        const char* name = std::strrchr(path, '\\');
        name = name ? name + 1 : path;
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
        std::snprintf(output, output_size, "module=%s+%08lx", name, static_cast<unsigned long>(address - base));
    }

    void probe_ffxi_interface() {
        probe_vtable("manager", reinterpret_cast<std::uintptr_t>(plugin_manager_), 16);

        FFXI* ffxi = plugin_manager_ ? plugin_manager_->GetFFXI() : nullptr;
        char header[256] {};
        std::snprintf(header, sizeof(header), "ffxiprobe manager=%p ffxi=%p", plugin_manager_, ffxi);
        append_log(header);
        if (!ffxi) {
            return;
        }

        probe_object_words("ffxi.raw", reinterpret_cast<std::uintptr_t>(ffxi), 32);
        probe_vtable("ffxi", reinterpret_cast<std::uintptr_t>(ffxi), 24);
    }

    void probe_object_words(const char* label, std::uintptr_t object, int words) {
        if (!object) {
            return;
        }

        for (int index = 0; index < words; ++index) {
            const std::uintptr_t address = object + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t);
            std::uintptr_t value = 0;
            if (!read_memory(address, value)) {
                break;
            }

            char module[128] {};
            module_label_for_address(value, module, sizeof(module));

            char ascii[8] {};
            for (int byte = 0; byte < 4; ++byte) {
                const char ch = static_cast<char>((value >> (byte * 8)) & 0xFF);
                ascii[byte] = (ch >= 32 && ch <= 126) ? ch : '.';
            }
            ascii[4] = '\0';

            char message[256] {};
            std::snprintf(message, sizeof(message), "ffxiprobe %s[%02d] @%08lx=%08lx ascii='%s' %s",
                label ? label : "object",
                index,
                static_cast<unsigned long>(address),
                static_cast<unsigned long>(value),
                ascii,
                module);
            append_log(message);
        }
    }

    void probe_vtable(const char* label, std::uintptr_t object, int entries) {
        if (!object) {
            char message[128] {};
            std::snprintf(message, sizeof(message), "ffxiprobe %s object=nil", label ? label : "unknown");
            append_log(message);
            return;
        }

        std::uintptr_t vtable = 0;
        if (!read_memory(object, vtable) || !is_readable_range(vtable, sizeof(std::uintptr_t))) {
            char message[160] {};
            std::snprintf(message, sizeof(message), "ffxiprobe %s object=%08lx vtable unreadable",
                label ? label : "unknown",
                static_cast<unsigned long>(object));
            append_log(message);
            return;
        }

        char vtable_module[128] {};
        module_label_for_address(vtable, vtable_module, sizeof(vtable_module));
        char vtable_message[256] {};
        std::snprintf(vtable_message, sizeof(vtable_message), "ffxiprobe %s object=%08lx vtable=%08lx %s",
            label ? label : "unknown",
            static_cast<unsigned long>(object),
            static_cast<unsigned long>(vtable),
            vtable_module);
        append_log(vtable_message);

        for (int index = 0; index < entries; ++index) {
            std::uintptr_t entry = 0;
            if (!read_memory(vtable + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t), entry)) {
                break;
            }

            char module[128] {};
            module_label_for_address(entry, module, sizeof(module));
            char message[256] {};
            std::snprintf(message, sizeof(message), "ffxiprobe %s.vtable[%02d]=%08lx %s",
                label ? label : "unknown",
                index,
                static_cast<unsigned long>(entry),
                module);
            append_log(message);
        }
    }

    void probe_luacore_mob_array() {
        LineState lines[4] {};
        const int line_count = read_lines(lines, 4);
        char header[128] {};
        std::snprintf(header, sizeof(header), "luamobprobe line_count=%d", line_count);
        append_log(header);
        if (line_count <= 0) {
            return;
        }

        std::uintptr_t mob_array = 0;
        std::uintptr_t lua_base = 0;
        std::uintptr_t context = 0;
        if (!get_luacore_mob_array(mob_array, &lua_base, &context)) {
            append_log("luamobprobe LuaCore.dll not loaded");
            return;
        }

        char message[256] {};
        std::snprintf(message, sizeof(message), "luamobprobe base=%08lx context=%08lx mob_array=%08lx",
            static_cast<unsigned long>(lua_base),
            static_cast<unsigned long>(context),
            static_cast<unsigned long>(mob_array));
        append_log(message);

        probe_luacore_mob("source", mob_array, lines[0].source_id, lines[0].source_index, lines[0].source_x, lines[0].source_y, lines[0].source_z);
        probe_luacore_mob("target", mob_array, lines[0].target_id, lines[0].target_index, lines[0].target_x, lines[0].target_y, lines[0].target_z);
    }

    void probe_luacore_mob(const char* label, std::uintptr_t mob_array, DWORD id, DWORD index, float lua_x, float lua_y, float lua_z) {
        if (index == 0 || index >= 0x900) {
            char message[128] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s skipped invalid index=%lu",
                label ? label : "unknown",
                static_cast<unsigned long>(index));
            append_log(message);
            return;
        }

        std::uintptr_t mob = 0;
        if (!read_memory(mob_array + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t), mob) || !is_readable_range(mob, 0x100)) {
            char message[256] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s index=%lu mob unreadable ptr=%08lx",
                label ? label : "unknown",
                static_cast<unsigned long>(index),
                static_cast<unsigned long>(mob));
            append_log(message);
            return;
        }

        char message[256] {};
        float current_x = lua_x;
        float current_y = lua_y;
        float current_z = lua_z;
        const bool current_ok = read_luacore_mob_root(mob, current_x, current_y, current_z);
        const float line_dx = current_x - lua_x;
        const float line_dy = current_y - lua_y;
        const float line_dz = current_z - lua_z;

        std::snprintf(message, sizeof(message), "luamobprobe %s id=%lu index=%lu line_root=(%.3f %.3f %.3f) current_root_ok=%s current_root=(%.3f %.3f %.3f) line_delta=(%.3f %.3f %.3f) mob=%08lx",
            label ? label : "unknown",
            static_cast<unsigned long>(id),
            static_cast<unsigned long>(index),
            lua_x, lua_y, lua_z,
            current_ok ? "true" : "false",
            current_x, current_y, current_z,
            line_dx, line_dy, line_dz,
            static_cast<unsigned long>(mob));
        append_log(message);

        probe_object_words(label && std::strcmp(label, "source") == 0 ? "luamob.source.raw" : "luamob.target.raw", mob, 48);
        probe_luacore_mob_fields(label, mob, id, index, lua_x, lua_y, lua_z);
        probe_luacore_actor_fields(label, mob, current_x, current_y, current_z);
        probe_luacore_pointer_candidates(label, mob, current_x, current_y, current_z);

        float bone_x = 0.0f;
        float bone_y = 0.0f;
        float bone_z = 0.0f;
        char detail[512] {};
        const bool bone_ok = read_bone_anchor(mob, 2, bone_x, bone_y, bone_z, detail, sizeof(detail));
        std::snprintf(message, sizeof(message), "luamobprobe %s direct_bone actor=%08lx bone_ok=%s anchor=(%.3f %.3f %.3f) %.80s",
            label ? label : "unknown",
            static_cast<unsigned long>(mob),
            bone_ok ? "true" : "false",
            bone_x, bone_y, bone_z,
            detail);
        append_log(message);
    }

    void probe_luacore_actor_fields(const char* label, std::uintptr_t mob, float lua_x, float lua_y, float lua_z) {
        static const std::uintptr_t offsets[] = {
            0x050,
            0x088,
            0x0a0,
        };

        for (std::size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            const std::uintptr_t offset = offsets[i];
            std::uintptr_t pointer = 0;
            const bool readable_pointer = read_memory(mob + offset, pointer);
            char module[128] {};
            if (readable_pointer) {
                module_label_for_address(pointer, module, sizeof(module));
            } else {
                std::snprintf(module, sizeof(module), "unreadable");
            }

            char message[256] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s actor_field offset=0x%03lx ptr=%08lx %s",
                label ? label : "unknown",
                static_cast<unsigned long>(offset),
                static_cast<unsigned long>(pointer),
                module);
            append_log(message);

            if (!readable_pointer || !is_readable_range(pointer, 0x20)) {
                continue;
            }

            float root_x = 0.0f;
            float root_y = 0.0f;
            float root_z = 0.0f;
            const bool root_ok = read_actor_root(pointer, root_x, root_y, root_z);
            const float dx = root_x - lua_x;
            const float dy = root_y - lua_y;
            const float dz = root_z - lua_z;
            const float distance = root_ok ? std::sqrt(dx * dx + dy * dy + dz * dz) : 9999.0f;
            const bool usable = root_ok && distance <= 0.50f;
            std::snprintf(message, sizeof(message), "luamobprobe %s actor_field_delta offset=0x%03lx root_ok=%s usable=%s root=(%.3f %.3f %.3f) delta=(%.3f %.3f %.3f) dist=%.3f",
                label ? label : "unknown",
                static_cast<unsigned long>(offset),
                root_ok ? "true" : "false",
                usable ? "true" : "false",
                root_x, root_y, root_z,
                dx, dy, dz,
                distance);
            append_log(message);

            char reason[32] {};
            std::snprintf(reason, sizeof(reason), "field_0x%03lx", static_cast<unsigned long>(offset));
            probe_actor_candidate_address(label, reason, pointer);
            if (offset == 0x0a0) {
                probe_actor_bones(label, pointer, 64);
            }
            probe_actor_roots_near_pointer(label, pointer, lua_x, lua_y, lua_z);
        }
    }

    void probe_luacore_pointer_candidates(const char* label, std::uintptr_t mob, float lua_x, float lua_y, float lua_z) {
        int candidates = 0;
        for (std::uintptr_t offset = 0; offset < 0x120 && candidates < 16; offset += 4) {
            std::uintptr_t pointer = 0;
            if (!read_memory(mob + offset, pointer) || !is_readable_range(pointer, 0x20)) {
                continue;
            }

            char module[128] {};
            module_label_for_address(pointer, module, sizeof(module));
            char message[256] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s ptr_candidate=%d offset=0x%03lx ptr=%08lx %s",
                label ? label : "unknown",
                candidates + 1,
                static_cast<unsigned long>(offset),
                static_cast<unsigned long>(pointer),
                module);
            append_log(message);

            probe_actor_candidate_address(label, "ptr", pointer);
            probe_actor_roots_near_pointer(label, pointer, lua_x, lua_y, lua_z);
            ++candidates;
        }
    }

    void probe_actor_candidate_address(const char* label, const char* reason, std::uintptr_t actor) {
        float bone_x = 0.0f;
        float bone_y = 0.0f;
        float bone_z = 0.0f;
        char detail[512] {};
        const bool bone_ok = read_bone_anchor(actor, 2, bone_x, bone_y, bone_z, detail, sizeof(detail));
        if (!bone_ok && std::strstr(detail, "bone_read_failed") != nullptr) {
            return;
        }

        char message[1024] {};
        std::snprintf(message, sizeof(message), "luamobprobe %s %s_actor=%08lx bone_ok=%s anchor=(%.3f %.3f %.3f) %.120s",
            label ? label : "unknown",
            reason ? reason : "candidate",
            static_cast<unsigned long>(actor),
            bone_ok ? "true" : "false",
            bone_x, bone_y, bone_z,
            detail);
        append_log(message);
    }

    void probe_actor_bones(const char* label, std::uintptr_t actor, int max_bones) {
        for (int bone = 0; bone < max_bones; ++bone) {
            float bone_x = 0.0f;
            float bone_y = 0.0f;
            float bone_z = 0.0f;
            char detail[512] {};
            const bool bone_ok = read_bone_anchor(actor, bone, bone_x, bone_y, bone_z, detail, sizeof(detail));
            if (!bone_ok) {
                continue;
            }

            char message[1024] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s bone_sample bone=%d anchor=(%.3f %.3f %.3f) %.120s",
                label ? label : "unknown",
                bone,
                bone_x, bone_y, bone_z,
                detail);
            append_log(message);
        }
    }

    void probe_actor_roots_near_pointer(const char* label, std::uintptr_t pointer, float lua_x, float lua_y, float lua_z) {
        if (!is_readable_range(pointer, 0x800)) {
            return;
        }

        int roots = 0;
        for (std::uintptr_t offset = 0; offset + 0x0c < 0x800 && roots < 4; offset += 4) {
            float root_x = 0.0f;
            float root_z = 0.0f;
            float root_y = 0.0f;
            if (!read_memory(pointer + offset, root_x) ||
                !read_memory(pointer + offset + 0x04, root_z) ||
                !read_memory(pointer + offset + 0x08, root_y)) {
                continue;
            }

            if (!std::isfinite(root_x) || !std::isfinite(root_y) || !std::isfinite(root_z) ||
                std::fabs(root_x) > 10000.0f || std::fabs(root_y) > 10000.0f || std::fabs(root_z) > 10000.0f) {
                continue;
            }

            if (std::fabs(root_x - lua_x) > 0.02f ||
                std::fabs(root_z - lua_z) > 0.02f ||
                std::fabs(root_y - lua_y) > 0.02f) {
                continue;
            }

            const std::uintptr_t actor = pointer + offset - 0x678;
            char message[512] {};
            std::snprintf(message, sizeof(message), "luamobprobe %s root_near_ptr ptr=%08lx root_offset=0x%03lx actor_guess=%08lx root=(%.3f %.3f %.3f)",
                label ? label : "unknown",
                static_cast<unsigned long>(pointer),
                static_cast<unsigned long>(offset),
                static_cast<unsigned long>(actor),
                root_x, root_y, root_z);
            append_log(message);
            probe_actor_candidate_address(label, "root_guess", actor);
            ++roots;
        }
    }

    void probe_luacore_mob_fields(const char* label, std::uintptr_t mob, DWORD id, DWORD index, float lua_x, float lua_y, float lua_z) {
        for (std::uintptr_t offset = 0; offset < 0x200; offset += 4) {
            DWORD integer = 0;
            if (read_memory(mob + offset, integer) && (integer == id || integer == index)) {
                char message[192] {};
                std::snprintf(message, sizeof(message), "luamobprobe %s int_match offset=0x%03lx value=%lu",
                    label ? label : "unknown",
                    static_cast<unsigned long>(offset),
                    static_cast<unsigned long>(integer));
                append_log(message);
            }

            float value = 0.0f;
            if (read_memory(mob + offset, value) &&
                (std::fabs(value - lua_x) <= 0.01f || std::fabs(value - lua_y) <= 0.01f || std::fabs(value - lua_z) <= 0.01f)) {
                char message[192] {};
                std::snprintf(message, sizeof(message), "luamobprobe %s float_match offset=0x%03lx value=%.3f",
                    label ? label : "unknown",
                    static_cast<unsigned long>(offset),
                    value);
                append_log(message);
            }
        }
    }

    struct DrawVertex {
        float x;
        float y;
        float z;
        float rhw;
        DWORD color;
    };

    void draw_test_bar() {
        DrawVertex vertices[] = {
            {80.0f, 80.0f, 0.0f, 1.0f, 0xCCFF3030},
            {300.0f, 80.0f, 0.0f, 1.0f, 0xCCFF3030},
            {80.0f, 88.0f, 0.0f, 1.0f, 0xCCFF3030},
            {300.0f, 88.0f, 0.0f, 1.0f, 0xCCFF3030},
        };

        draw_vertices(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(DrawVertex));
    }

    struct LineState {
        DWORD uid = 0;
        DWORD source_id = 0;
        DWORD target_id = 0;
        DWORD source_index = 0;
        DWORD target_index = 0;
        float source_x = 0.0f;
        float source_y = 0.0f;
        float source_z = 0.0f;
        float target_x = 0.0f;
        float target_y = 0.0f;
        float target_z = 0.0f;
        float source_model_size = 0.0f;
        float source_model_scale = 1.0f;
        float target_model_size = 0.0f;
        float target_model_scale = 1.0f;
        bool source_short_anchor = false;
        bool target_short_anchor = false;
        bool source_floating_anchor = false;
        bool target_floating_anchor = false;
        bool source_is_npc = false;
        bool target_is_npc = false;
        float timeout = 1.5f;
        DWORD color = 0xEFFFFFFF;
    };

    struct ActiveLine {
        unsigned long long key = 0;
        DWORD start_ms = 0;
        DWORD last_seen_ms = 0;
        bool spread_source = false;
        bool spread_target = false;
    };

    void draw_lines() {
        LineState lines[128] {};
        const int line_count = read_lines(lines, 128);
        if (line_count <= 0) {
            return;
        }

        if (boneprobe_requested_) {
            boneprobe_requested_ = false;
            const DWORD now_ms = GetTickCount();
            if (now_ms - last_boneprobe_ms_ > 1000) {
                last_boneprobe_ms_ = now_ms;
                append_log("boneprobe requested by state file");
                probe_actor_candidates("source", lines[0].source_id, lines[0].source_index, lines[0].source_x, lines[0].source_y, lines[0].source_z);
                probe_actor_candidates("target", lines[0].target_id, lines[0].target_index, lines[0].target_x, lines[0].target_y, lines[0].target_z);
            }
        }

        D3DVIEWPORT8 viewport {};
        if (!refresh_projection_matrices()) {
            return;
        }
        if (FAILED(d3d_device_->GetViewport(&viewport))) {
            viewport.Width = 1024;
            viewport.Height = 768;
        }

        std::uintptr_t mob_array = 0;
        if (dynamic_bone_ >= 0) {
            get_luacore_mob_array(mob_array, nullptr, nullptr);
        }

        if (!begin_draw_state()) {
            return;
        }

        const DWORD now_ms = GetTickCount();
        for (int i = 0; i < line_count; ++i) {
            ActiveLine* active = nullptr;
            float progress = 1.0f;
            float arc_settle = 0.0f;
            float settle = 0.0f;
            float tail = 0.0f;
            DWORD color = lines[i].color;
            if (!prepare_animated_line(lines[i], lines, line_count, i, now_ms, active, progress, arc_settle, settle, tail, color)) {
                continue;
            }

            const bool spread_source = active && active->spread_source;
            const bool spread_target = active && active->spread_target;
            begin_line_batch();
            draw_line_curve(lines[i], viewport, mob_array, spread_source, spread_target, progress, arc_settle, settle, tail, color);
            end_line_batch();
        }

        end_draw_state();
        prune_active_lines(now_ms);
    }

    bool prepare_animated_line(const LineState& line, const LineState* lines, int line_count, int index, DWORD now_ms, ActiveLine*& active, float& progress, float& arc_settle, float& settle, float& tail, DWORD& color) {
        const unsigned long long key = line_key(line);
        active = find_active_line(key);
        if (!active) {
            active = allocate_active_line(key, now_ms);
            if (active) {
                active->spread_source = source_continues_from_existing_target(lines, line_count, index);
                active->spread_target = shared_target_line(lines, line_count, index);
            }
        }

        if (!active) {
            return false;
        }

        active->last_seen_ms = now_ms;
        const float age = static_cast<float>(now_ms - active->start_ms) / 1000.0f;
        const float timeout = std::fmax(line.timeout, 0.1f);
        if (age > timeout) {
            return false;
        }

        const float travel_time = std::fmin(0.32f, timeout * 0.25f);
        const float recede_start = timeout * 0.61f;
        const float recede_duration = std::fmax(timeout - recede_start, 0.1f);
        progress = std::fmax(0.0f, std::fmin(age / std::fmax(travel_time, 0.05f), 1.0f));
        const float arc_duration = std::fmax(timeout - travel_time, 0.1f);
        const float arc_t = std::fmax(0.0f, std::fmin((age - travel_time) / arc_duration, 1.0f));
        arc_settle = arc_t * arc_t * (3.0f - 2.0f * arc_t);
        settle = 0.0f;
        if (age > recede_start) {
            settle = std::fmax(0.0f, std::fmin((age - recede_start) / recede_duration, 1.0f));
        }
        tail = settle * settle * (3.0f - 2.0f * settle);
        const float fade = settle <= 0.0f ? 1.0f : std::sqrt(std::fmax(0.0f, 1.0f - settle));
        color = scale_alpha(line.color, fade * opacity_scale_);
        return progress > 0.01f;
    }

    void draw_line_curve(const LineState& line, const D3DVIEWPORT8& viewport, std::uintptr_t mob_array, bool spread_source, bool spread_target, float progress, float arc_settle, float settle, float tail, DWORD color) {
        constexpr int segments = 38;
        const float core_thickness = 10.5f * width_scale_;
        const float border_thickness = 12.5f * width_scale_;
        const float haze_thickness = 16.5f * width_scale_ * (0.75f + glow_scale_ * 0.25f);
        const float head_radius = core_thickness * 0.85f;
        const float head_outer_radius = head_radius * 1.16f;
        const float head_inner_radius = head_radius * 0.72f;
        const float head_hot_radius = head_radius * 0.38f;
        DrawVertex haze_vertices[segments * 6] {};
        DrawVertex border_vertices[segments * 6] {};
        DrawVertex core_vertices[segments * 6] {};
        DrawVertex shine_vertices[segments * 6] {};
        int haze_vertex_count = 0;
        int border_vertex_count = 0;
        int core_vertex_count = 0;
        int shine_vertex_count = 0;
        float head_x = 0.0f;
        float head_y = 0.0f;
        bool have_head = false;
        const DWORD saturated_color = saturate_color(color);
        const DWORD haze_color = scale_alpha(saturated_color, 0.24f * (0.75f + glow_scale_ * 0.25f));
        const DWORD border_color = scale_alpha(darken_color(saturated_color), 0.82f);
        const DWORD core_color = scale_alpha(tint_white_color(saturated_color, 0.50f), 1.25f);
        const DWORD shine_color = scale_alpha(tint_white_color(saturated_color, 0.94f), 1.18f);

        float p0_x = line.source_x;
        float p0_y = line.source_y;
        float p0_z = line.source_z + model_adjusted_height(source_height_offset_, line.source_model_size, line.source_model_scale, line.source_short_anchor, line.source_floating_anchor, line.source_is_npc);
        float p2_x = line.target_x;
        float p2_y = line.target_y;
        float p2_z = line.target_z + model_adjusted_height(target_height_offset_, line.target_model_size, line.target_model_scale, line.target_short_anchor, line.target_floating_anchor, line.target_is_npc);
        if (dynamic_bone_ >= 0) {
            resolve_dynamic_anchor(mob_array, line.source_is_npc, line.source_index, dynamic_bone_, p0_x, p0_y, p0_z);
            resolve_dynamic_anchor(mob_array, line.target_is_npc, line.target_index, dynamic_bone_, p2_x, p2_y, p2_z);
        }
        if (spread_source) {
            apply_source_spread(line, p0_x, p0_y);
        }
        if (spread_target) {
            apply_target_spread(line, p2_x, p2_y);
        }
        const float dx = p2_x - p0_x;
        const float dy = p2_y - p0_y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const float arc_height_scale = std::fmax(0.50f, 1.0f - arc_settle * 0.30f - settle * 0.20f);
        const float arc_lift = (arc_height_offset_ - std::fmin(distance * 0.10f, 1.5f)) * arc_height_scale;
        const float p1_x = (p0_x + p2_x) * 0.5f;
        const float p1_y = (p0_y + p2_y) * 0.5f;
        const float p1_z = (p0_z + p2_z) * 0.5f + arc_lift;

        const float end_t = std::fmax(0.0f, std::fmin(progress, 1.0f));
        const float start_t = std::fmax(0.0f, std::fmin(tail * 0.985f, end_t - 0.015f));
        const float visible_range = end_t - start_t;
        if (visible_range <= 0.01f) {
            return;
        }

        float screen_xs[segments + 1] {};
        float screen_ys[segments + 1] {};
        float segment_alphas[segments + 1] {};
        bool screen_valid[segments + 1] {};

        for (int i = 0; i <= segments; ++i) {
            const float local_t = static_cast<float>(i) / static_cast<float>(segments);
            const float t = start_t + visible_range * local_t;
            const float inv = 1.0f - t;
            const float world_x = inv * inv * p0_x + 2.0f * inv * t * p1_x + t * t * p2_x;
            const float world_y = inv * inv * p0_y + 2.0f * inv * t * p1_y + t * t * p2_y;
            const float world_z = inv * inv * p0_z + 2.0f * inv * t * p1_z + t * t * p2_z;
            float screen_x = 0.0f;
            float screen_y = 0.0f;

            if (!live_world_to_screen(world_x, world_y, world_z, viewport, screen_x, screen_y)) {
                continue;
            }

            screen_xs[i] = screen_x;
            screen_ys[i] = screen_y;
            const float tail_fade = 1.0f - (1.0f - local_t) * (1.0f - local_t);
            const float tail_soft_start = std::fmin(local_t / 0.12f, 1.0f);
            segment_alphas[i] = tail_soft_start * (0.28f + 0.72f * tail_fade);
            screen_valid[i] = true;
            head_x = screen_x;
            head_y = screen_y;
            have_head = true;
        }

        if (!have_head) {
            return;
        }

        float cap_dir_x = 1.0f;
        float cap_dir_y = 0.0f;
        for (int i = segments - 1; i >= 0; --i) {
            if (!screen_valid[i]) {
                continue;
            }

            const float dx = head_x - screen_xs[i];
            const float dy = head_y - screen_ys[i];
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.01f) {
                cap_dir_x = dx / length;
                cap_dir_y = dy / length;
                break;
            }
        }

        for (int i = 1; i <= segments; ++i) {
            if (!screen_valid[i - 1] || !screen_valid[i]) {
                continue;
            }

            const float segment_alpha = segment_alphas[i];
            append_segment_quad(haze_vertices, haze_vertex_count, screen_xs[i - 1], screen_ys[i - 1], screen_xs[i], screen_ys[i], haze_thickness, scale_alpha(haze_color, segment_alpha), 0.0f);
            append_segment_quad_clipped_to_circle(border_vertices, border_vertex_count, screen_xs[i - 1], screen_ys[i - 1], screen_xs[i], screen_ys[i], border_thickness, scale_alpha(border_color, segment_alpha), head_x, head_y, head_outer_radius * 0.72f);
            append_segment_quad_clipped_to_circle(core_vertices, core_vertex_count, screen_xs[i - 1], screen_ys[i - 1], screen_xs[i], screen_ys[i], core_thickness, scale_alpha(core_color, segment_alpha), head_x, head_y, head_inner_radius * 0.82f);
            append_segment_quad_clipped_to_circle(shine_vertices, shine_vertex_count, screen_xs[i - 1], screen_ys[i - 1], screen_xs[i], screen_ys[i], std::fmax(2.5f, core_thickness * 0.42f), scale_alpha(shine_color, segment_alpha), head_x, head_y, head_hot_radius * 0.92f);
        }

        if (haze_vertex_count > 0) {
            draw_vertices(D3DPT_TRIANGLELIST, haze_vertex_count / 3, haze_vertices, sizeof(DrawVertex));
        }

        const float head_entry_delay = 0.08f;
        const float head_entry_t = std::fmax(0.0f, std::fmin((arc_settle - head_entry_delay) / (1.0f - head_entry_delay), 1.0f));
        const float head_landing_t = head_entry_t * head_entry_t * (3.0f - 2.0f * head_entry_t);
        const float head_alpha = std::fmax(0.0f, 1.0f - settle * 0.85f);
        const float forward_cap_alpha = head_alpha * (1.0f - head_landing_t * 0.72f);

        if (forward_cap_alpha > 0.01f) {
            draw_round_line_cap(head_x, head_y, cap_dir_x, cap_dir_y, haze_thickness * 0.5f, scale_alpha(haze_color, 0.80f * forward_cap_alpha));
        }

        if (border_vertex_count > 0) {
            draw_vertices(D3DPT_TRIANGLELIST, border_vertex_count / 3, border_vertices, sizeof(DrawVertex));
        }

        if (core_vertex_count > 0) {
            draw_vertices(D3DPT_TRIANGLELIST, core_vertex_count / 3, core_vertices, sizeof(DrawVertex));
        }

        if (shine_vertex_count > 0) {
            draw_vertices(D3DPT_TRIANGLELIST, shine_vertex_count / 3, shine_vertices, sizeof(DrawVertex));
        }

        if (head_alpha > 0.01f) {
            draw_head_marker(head_x, head_y, head_radius, haze_color, core_color, shine_color, cap_dir_x, cap_dir_y, head_landing_t, head_alpha);
        }
    }

    unsigned long long line_key(const LineState& line) const {
        const DWORD source = line.source_id ? line.source_id : static_cast<DWORD>(std::fabs(line.source_x * 100.0f));
        const DWORD target = line.target_id ? line.target_id : static_cast<DWORD>(std::fabs(line.target_x * 100.0f));
        const DWORD uid = line.uid ? line.uid : (source ^ (target << 1));
        return (static_cast<unsigned long long>(uid) << 32) ^ (static_cast<unsigned long long>(source) << 16) ^ target;
    }

    bool same_target(const LineState& left, const LineState& right) const {
        if (left.target_id != 0 && right.target_id != 0) {
            return left.target_id == right.target_id;
        }

        if (left.target_index != 0 && right.target_index != 0) {
            return left.target_index == right.target_index;
        }

        return std::fabs(left.target_x - right.target_x) <= 0.01f &&
            std::fabs(left.target_y - right.target_y) <= 0.01f &&
            std::fabs(left.target_z - right.target_z) <= 0.01f;
    }

    bool source_matches_target(const LineState& source_line, const LineState& target_line) const {
        if (source_line.source_id != 0 && target_line.target_id != 0) {
            return source_line.source_id == target_line.target_id;
        }

        if (source_line.source_index != 0 && target_line.target_index != 0) {
            return source_line.source_index == target_line.target_index;
        }

        return std::fabs(source_line.source_x - target_line.target_x) <= 0.01f &&
            std::fabs(source_line.source_y - target_line.target_y) <= 0.01f &&
            std::fabs(source_line.source_z - target_line.target_z) <= 0.01f;
    }

    bool line_is_newer_than(const LineState& line, const LineState& other, int index, int other_index) const {
        if (line.uid != 0 && other.uid != 0) {
            return line.uid > other.uid;
        }

        return index < other_index;
    }

    bool source_continues_from_existing_target(const LineState* lines, int line_count, int index) const {
        if (!lines || index < 0 || index >= line_count) {
            return false;
        }

        for (int i = 0; i < line_count; ++i) {
            if (i != index && line_is_newer_than(lines[index], lines[i], index, i) && source_matches_target(lines[index], lines[i])) {
                return true;
            }
        }

        return false;
    }

    bool shared_target_line(const LineState* lines, int line_count, int index) const {
        if (!lines || index < 0 || index >= line_count) {
            return false;
        }

        for (int i = 0; i < line_count; ++i) {
            if (i != index && line_is_newer_than(lines[index], lines[i], index, i) && same_target(lines[index], lines[i])) {
                return true;
            }
        }

        return false;
    }

    std::uint32_t mix_u32(std::uint32_t value) const {
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        value *= 0x846ca68bU;
        value ^= value >> 16;
        return value;
    }

    void apply_target_spread(const LineState& line, float& target_x, float& target_y) const {
        std::uint32_t seed = line.uid ? line.uid : (line.source_id ^ (line.target_id << 1));
        seed ^= line.source_id * 0x9e3779b9U;
        seed ^= line.target_id * 0x85ebca6bU;
        seed ^= line.source_index * 0xc2b2ae35U;
        seed ^= line.target_index * 0x27d4eb2fU;
        const std::uint32_t hash = mix_u32(seed);
        const float angle = (static_cast<float>(hash & 0xFFFFU) / 65535.0f) * 6.28318530718f;
        const float radius = 0.10f + (static_cast<float>((hash >> 16) & 0xFFU) / 255.0f) * 0.12f;
        target_x += std::cos(angle) * radius;
        target_y += std::sin(angle) * radius;
    }

    void apply_source_spread(const LineState& line, float& source_x, float& source_y) const {
        std::uint32_t seed = line.uid ? line.uid : (line.source_id ^ (line.target_id << 1));
        seed ^= line.source_id * 0x165667b1U;
        seed ^= line.target_id * 0xd3a2646cU;
        seed ^= line.source_index * 0xfd7046c5U;
        seed ^= line.target_index * 0xb55a4f09U;
        const std::uint32_t hash = mix_u32(seed);
        const float angle = (static_cast<float>(hash & 0xFFFFU) / 65535.0f) * 6.28318530718f;
        const float radius = 0.10f + (static_cast<float>((hash >> 16) & 0xFFU) / 255.0f) * 0.12f;
        source_x += std::cos(angle) * radius;
        source_y += std::sin(angle) * radius;
    }

    float model_adjusted_height(float manual_offset, float model_size, float model_scale, bool short_anchor, bool floating_anchor, bool is_npc) const {
        if (!is_npc) {
            return manual_offset;
        }

        if (floating_anchor) {
            return manual_offset - 1.05f;
        }

        if (short_anchor) {
            return manual_offset + 0.65f;
        }

        if (model_size <= 0.0f) {
            return manual_offset;
        }

        const float scale = model_scale > 0.0f ? model_scale : 1.0f;
        const float effective_size = model_size * scale;
        if (effective_size < 1.15f) {
            const float auto_lower = std::fmin((1.15f - effective_size) * 1.25f, 0.65f);
            return manual_offset + auto_lower;
        }

        if (effective_size <= 1.2f) {
            return manual_offset;
        }

        // In this coordinate mapping, more negative z offsets raise the screen anchor.
        const float auto_raise = std::fmin((effective_size - 1.2f) * 0.45f, 1.15f);
        return manual_offset - auto_raise;
    }

    bool resolve_dynamic_anchor(std::uintptr_t mob_array, bool is_npc, DWORD index, int bone, float& lua_x, float& lua_y, float& lua_z) {
        if (mob_array == 0 || !is_npc || index == 0 || index >= 0x900) {
            return false;
        }

        std::uintptr_t mob = 0;
        if (!read_memory(mob_array + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t), mob) || !is_readable_range(mob, 0x0a4)) {
            return false;
        }

        float mob_x = 0.0f;
        float mob_y = 0.0f;
        float mob_z = 0.0f;
        if (!read_luacore_mob_root(mob, mob_x, mob_y, mob_z)) {
            return false;
        }

        std::uintptr_t actor = 0;
        if (!read_memory(mob + 0x0a0, actor) || !is_readable_range(actor, 0x700)) {
            return false;
        }

        float actor_x = 0.0f;
        float actor_y = 0.0f;
        float actor_z = 0.0f;
        if (!read_actor_root(actor, actor_x, actor_y, actor_z)) {
            return false;
        }

        const float dx = actor_x - mob_x;
        const float dy = actor_y - mob_y;
        const float dz = actor_z - mob_z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > 0.50f) {
            return false;
        }

        float bone_x = 0.0f;
        float bone_y = 0.0f;
        float bone_z = 0.0f;
        char detail[128] {};
        if (!read_bone_anchor(actor, bone, bone_x, bone_y, bone_z, detail, sizeof(detail))) {
            return false;
        }

        lua_x = bone_x;
        lua_y = bone_y;
        lua_z = bone_z;
        return true;
    }

    ActiveLine* find_active_line(unsigned long long key) {
        for (int i = 0; i < active_line_count_; ++i) {
            if (active_lines_[i].key == key) {
                return &active_lines_[i];
            }
        }

        return nullptr;
    }

    ActiveLine* allocate_active_line(unsigned long long key, DWORD now_ms) {
        if (active_line_count_ < max_active_lines_) {
            ActiveLine& active = active_lines_[active_line_count_++];
            active.key = key;
            active.start_ms = now_ms;
            active.last_seen_ms = now_ms;
            return &active;
        }

        int oldest = 0;
        for (int i = 1; i < active_line_count_; ++i) {
            if (active_lines_[i].last_seen_ms < active_lines_[oldest].last_seen_ms) {
                oldest = i;
            }
        }

        ActiveLine& active = active_lines_[oldest];
        active.key = key;
        active.start_ms = now_ms;
        active.last_seen_ms = now_ms;
        return &active;
    }

    void prune_active_lines(DWORD now_ms) {
        int write = 0;
        for (int read = 0; read < active_line_count_; ++read) {
            if (now_ms - active_lines_[read].last_seen_ms <= 5000) {
                if (write != read) {
                    active_lines_[write] = active_lines_[read];
                }
                ++write;
            }
        }

        active_line_count_ = write;
    }

    DWORD scale_alpha(DWORD color, float scale) {
        const DWORD alpha = (color >> 24) & 0xFF;
        const DWORD scaled_alpha = static_cast<DWORD>(std::fmax(0.0f, std::fmin(255.0f, static_cast<float>(alpha) * scale)));
        return (color & 0x00FFFFFF) | (scaled_alpha << 24);
    }

    DWORD darken_color(DWORD color) {
        const DWORD alpha = color & 0xFF000000;
        const DWORD red = ((color >> 16) & 0xFF) / 3;
        const DWORD green = ((color >> 8) & 0xFF) / 3;
        const DWORD blue = (color & 0xFF) / 3;
        return alpha | (red << 16) | (green << 8) | blue;
    }

    DWORD brighten_color(DWORD color) {
        const DWORD alpha = color & 0xFF000000;
        DWORD red = (color >> 16) & 0xFF;
        DWORD green = (color >> 8) & 0xFF;
        DWORD blue = color & 0xFF;

        red = red + ((255 - red) * 2) / 3;
        green = green + ((255 - green) * 2) / 3;
        blue = blue + ((255 - blue) * 2) / 3;
        return alpha | (red << 16) | (green << 8) | blue;
    }

    DWORD saturate_color(DWORD color) {
        const DWORD alpha = color & 0xFF000000;
        int red = static_cast<int>((color >> 16) & 0xFF);
        int green = static_cast<int>((color >> 8) & 0xFF);
        int blue = static_cast<int>(color & 0xFF);
        const int gray = (red * 30 + green * 59 + blue * 11) / 100;

        red = gray + (red - gray) * 7 / 5;
        green = gray + (green - gray) * 7 / 5;
        blue = gray + (blue - gray) * 7 / 5;

        red = red < 0 ? 0 : (red > 255 ? 255 : red);
        green = green < 0 ? 0 : (green > 255 ? 255 : green);
        blue = blue < 0 ? 0 : (blue > 255 ? 255 : blue);
        return alpha | (static_cast<DWORD>(red) << 16) | (static_cast<DWORD>(green) << 8) | static_cast<DWORD>(blue);
    }

    DWORD tint_white_color(DWORD color, float amount) {
        const DWORD alpha = color & 0xFF000000;
        DWORD red = (color >> 16) & 0xFF;
        DWORD green = (color >> 8) & 0xFF;
        DWORD blue = color & 0xFF;
        amount = std::fmax(0.0f, std::fmin(amount, 1.0f));

        red = static_cast<DWORD>(static_cast<float>(red) + (255.0f - static_cast<float>(red)) * amount);
        green = static_cast<DWORD>(static_cast<float>(green) + (255.0f - static_cast<float>(green)) * amount);
        blue = static_cast<DWORD>(static_cast<float>(blue) + (255.0f - static_cast<float>(blue)) * amount);
        return alpha | (red << 16) | (green << 8) | blue;
    }

    float directional_head_fade(float unit_x, float unit_y, float direction_x, float direction_y, float landing_t) const {
        const float direction_length = std::sqrt(direction_x * direction_x + direction_y * direction_y);
        if (direction_length <= 0.001f) {
            return 1.0f - landing_t * 0.72f;
        }

        const float dot = (unit_x * direction_x + unit_y * direction_y) / direction_length;
        const float target_side = std::fmax(0.0f, std::fmin((dot + 1.0f) * 0.5f, 1.0f));
        const float target_absorb = landing_t * landing_t * (3.0f - 2.0f * landing_t);
        const float front_fade = 1.0f - target_absorb * (0.30f + target_side * 0.68f);
        return std::fmax(0.02f, std::fmin(front_fade, 1.0f));
    }

    void draw_head_marker(float center_x, float center_y, float radius, DWORD haze_color_base, DWORD core_color, DWORD shine_color, float direction_x, float direction_y, float landing_t, float base_alpha) {
        constexpr int slices = 36;
        DrawVertex vertices[slices * 9] {};
        int vertex_count = 0;
        const DWORD outer_color = scale_alpha(core_color, 0.86f * base_alpha);
        const DWORD inner_color = scale_alpha(core_color, 1.16f * base_alpha);
        const DWORD hot_color = scale_alpha(tint_white_color(shine_color, 0.35f), 1.20f * base_alpha);
        const float outer_radius = radius * 1.16f;
        const float inner_radius = radius * 0.72f;
        const float hot_radius = radius * 0.38f;
        const DWORD outer_center_color = scale_alpha(outer_color, directional_head_fade(0.0f, 0.0f, direction_x, direction_y, landing_t));
        const DWORD inner_center_color = scale_alpha(inner_color, directional_head_fade(0.0f, 0.0f, direction_x, direction_y, landing_t));
        const DWORD hot_center_color = scale_alpha(hot_color, directional_head_fade(0.0f, 0.0f, direction_x, direction_y, landing_t));
        (void)haze_color_base;

        for (int i = 0; i < slices; ++i) {
            const float a0 = 6.28318530718f * static_cast<float>(i) / static_cast<float>(slices);
            const float a1 = 6.28318530718f * static_cast<float>(i + 1) / static_cast<float>(slices);
            const float x0 = std::cos(a0);
            const float y0 = std::sin(a0);
            const float x1 = std::cos(a1);
            const float y1 = std::sin(a1);
            const float fade0 = directional_head_fade(x0, y0, direction_x, direction_y, landing_t);
            const float fade1 = directional_head_fade(x1, y1, direction_x, direction_y, landing_t);

            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, outer_center_color};
            vertices[vertex_count++] = DrawVertex {center_x + x0 * outer_radius, center_y + y0 * outer_radius, 0.0f, 1.0f, scale_alpha(outer_color, 0.76f * fade0)};
            vertices[vertex_count++] = DrawVertex {center_x + x1 * outer_radius, center_y + y1 * outer_radius, 0.0f, 1.0f, scale_alpha(outer_color, 0.76f * fade1)};

            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, inner_center_color};
            vertices[vertex_count++] = DrawVertex {center_x + x0 * inner_radius, center_y + y0 * inner_radius, 0.0f, 1.0f, scale_alpha(inner_color, fade0)};
            vertices[vertex_count++] = DrawVertex {center_x + x1 * inner_radius, center_y + y1 * inner_radius, 0.0f, 1.0f, scale_alpha(inner_color, fade1)};

            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, hot_center_color};
            vertices[vertex_count++] = DrawVertex {center_x + x0 * hot_radius, center_y + y0 * hot_radius, 0.0f, 1.0f, scale_alpha(hot_color, fade0)};
            vertices[vertex_count++] = DrawVertex {center_x + x1 * hot_radius, center_y + y1 * hot_radius, 0.0f, 1.0f, scale_alpha(hot_color, fade1)};
        }

        draw_vertices(D3DPT_TRIANGLELIST, vertex_count / 3, vertices, sizeof(DrawVertex));
    }

    void draw_round_line_cap(float center_x, float center_y, float direction_x, float direction_y, float radius, DWORD color) {
        constexpr int slices = 24;
        DrawVertex vertices[slices * 3] {};
        int vertex_count = 0;
        const float length = std::sqrt(direction_x * direction_x + direction_y * direction_y);
        if (radius <= 0.1f || length <= 0.001f) {
            return;
        }

        const float angle = std::atan2(direction_y / length, direction_x / length);
        const float start = angle - 1.57079632679f;
        const float step = 3.14159265359f / static_cast<float>(slices);

        for (int i = 0; i < slices; ++i) {
            const float a0 = start + step * static_cast<float>(i);
            const float a1 = start + step * static_cast<float>(i + 1);
            vertices[vertex_count++] = DrawVertex {center_x, center_y, 0.0f, 1.0f, color};
            vertices[vertex_count++] = DrawVertex {center_x + std::cos(a0) * radius, center_y + std::sin(a0) * radius, 0.0f, 1.0f, color};
            vertices[vertex_count++] = DrawVertex {center_x + std::cos(a1) * radius, center_y + std::sin(a1) * radius, 0.0f, 1.0f, color};
        }

        draw_vertices(D3DPT_TRIANGLELIST, vertex_count / 3, vertices, sizeof(DrawVertex));
    }

    void probe_bone_anchors() {
        LineState lines[4] {};
        const int line_count = read_lines(lines, 4);
        char message[256] {};
        std::snprintf(message, sizeof(message), "boneprobe line_count=%d", line_count);
        append_log(message);

        if (line_count <= 0) {
            return;
        }

        probe_actor_candidates("source", lines[0].source_id, lines[0].source_index, lines[0].source_x, lines[0].source_y, lines[0].source_z);
        probe_actor_candidates("target", lines[0].target_id, lines[0].target_index, lines[0].target_x, lines[0].target_y, lines[0].target_z);
    }

    bool is_readable_page(DWORD protect) const {
        if (protect & (PAGE_GUARD | PAGE_NOACCESS)) {
            return false;
        }

        const DWORD base = protect & 0xFF;
        return base == PAGE_READONLY ||
            base == PAGE_READWRITE ||
            base == PAGE_WRITECOPY ||
            base == PAGE_EXECUTE_READ ||
            base == PAGE_EXECUTE_READWRITE ||
            base == PAGE_EXECUTE_WRITECOPY;
    }

    bool is_readable_range(std::uintptr_t address, std::size_t size) const {
        if (address == 0 || size == 0) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi {};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
            return false;
        }

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t end = base + mbi.RegionSize;
        return mbi.State == MEM_COMMIT &&
            is_readable_page(mbi.Protect) &&
            address >= base &&
            address + size <= end;
    }

    template<typename T>
    bool read_memory_raw(std::uintptr_t address, T& value) const {
        SIZE_T bytes_read = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(T), &bytes_read) &&
            bytes_read == sizeof(T);
    }

    template<typename T>
    bool read_memory(std::uintptr_t address, T& value) const {
        if (!is_readable_range(address, sizeof(T))) {
            return false;
        }

        return read_memory_raw(address, value);
    }

    bool read_actor_root(std::uintptr_t actor, float& lua_x, float& lua_y, float& lua_z) {
        float root_x = 0.0f;
        float root_z = 0.0f;
        float root_y = 0.0f;
        if (!read_memory(actor + 0x678, root_x) ||
            !read_memory(actor + 0x67C, root_z) ||
            !read_memory(actor + 0x680, root_y)) {
            return false;
        }

        if (!std::isfinite(root_x) || !std::isfinite(root_y) || !std::isfinite(root_z) ||
            std::fabs(root_x) > 10000.0f || std::fabs(root_y) > 10000.0f || std::fabs(root_z) > 10000.0f) {
            return false;
        }

        lua_x = root_x;
        lua_y = root_y;
        lua_z = root_z;
        return true;
    }

    bool read_luacore_mob_root(std::uintptr_t mob, float& lua_x, float& lua_y, float& lua_z) {
        float root_x = 0.0f;
        float root_z = 0.0f;
        float root_y = 0.0f;
        if (!read_memory(mob + 0x004, root_x) ||
            !read_memory(mob + 0x008, root_z) ||
            !read_memory(mob + 0x00c, root_y)) {
            return false;
        }

        if (!std::isfinite(root_x) || !std::isfinite(root_y) || !std::isfinite(root_z) ||
            std::fabs(root_x) > 10000.0f || std::fabs(root_y) > 10000.0f || std::fabs(root_z) > 10000.0f) {
            return false;
        }

        lua_x = root_x;
        lua_y = root_y;
        lua_z = root_z;
        return true;
    }

    bool get_luacore_mob_array(std::uintptr_t& mob_array, std::uintptr_t* lua_base_out, std::uintptr_t* context_out) {
        mob_array = 0;
        if (lua_base_out) {
            *lua_base_out = 0;
        }
        if (context_out) {
            *context_out = 0;
        }

        HMODULE lua_core = GetModuleHandleA("LuaCore.dll");
        if (!lua_core) {
            return false;
        }

        const std::uintptr_t lua_base = reinterpret_cast<std::uintptr_t>(lua_core);
        const std::uintptr_t context_global = lua_base + 0x1c8400;
        std::uintptr_t context = 0;
        if (!read_memory(context_global, context) ||
            !read_memory(context + 0x24, mob_array) ||
            !is_readable_range(mob_array, sizeof(std::uintptr_t) * 0x900)) {
            mob_array = 0;
            return false;
        }

        if (lua_base_out) {
            *lua_base_out = lua_base;
        }
        if (context_out) {
            *context_out = context;
        }
        return true;
    }

    bool read_bone_anchor(std::uintptr_t actor, int bone, float& lua_x, float& lua_y, float& lua_z, char* detail, std::size_t detail_size) {
        float root_x = 0.0f;
        float root_z = 0.0f;
        float root_y = 0.0f;
        std::uint32_t skeleton_base = 0;
        std::uint32_t skeleton_offset = 0;
        std::uint32_t skeleton = 0;
        std::uint16_t bone_count = 0;

        if (!read_memory(actor + 0x678, root_x) ||
            !read_memory(actor + 0x67C, root_z) ||
            !read_memory(actor + 0x680, root_y) ||
            !read_memory(actor + 0x6B8, skeleton_base) ||
            !read_memory(static_cast<std::uintptr_t>(skeleton_base) + 0x0C, skeleton_offset) ||
            !read_memory(static_cast<std::uintptr_t>(skeleton_offset), skeleton) ||
            !read_memory(static_cast<std::uintptr_t>(skeleton) + 0x32, bone_count)) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "bone_read_failed");
            }
            return false;
        }

        if (!std::isfinite(root_x) || !std::isfinite(root_y) || !std::isfinite(root_z) ||
            std::fabs(root_x) > 10000.0f || std::fabs(root_y) > 10000.0f || std::fabs(root_z) > 10000.0f) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "invalid_root=(%.3f %.3f %.3f)", root_x, root_y, root_z);
            }
            return false;
        }

        if (bone_count == 0 || bone_count > 256) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "invalid_bone_count=%u skeleton=%08lx", bone_count, static_cast<unsigned long>(skeleton));
            }
            return false;
        }

        const std::uintptr_t generators = static_cast<std::uintptr_t>(skeleton) + 0x30 + 0x04 + 0x1E * bone_count + 4;
        const std::uintptr_t bone_base = generators + static_cast<std::uintptr_t>(bone) * 0x1A + 0x0E;
        float bone_x = 0.0f;
        float bone_z = 0.0f;
        float bone_y = 0.0f;
        if (!read_memory(bone_base + 0x0, bone_x) ||
            !read_memory(bone_base + 0x4, bone_z) ||
            !read_memory(bone_base + 0x8, bone_y)) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "bone_offset_read_failed bone_count=%u skeleton=%08lx generators=%08lx", bone_count, static_cast<unsigned long>(skeleton), static_cast<unsigned long>(generators));
            }
            return false;
        }

        if (!std::isfinite(bone_x) || !std::isfinite(bone_y) || !std::isfinite(bone_z) ||
            std::fabs(bone_x) > 100.0f || std::fabs(bone_y) > 100.0f || std::fabs(bone_z) > 100.0f) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "invalid_bone_offset bone_count=%u skeleton=%08lx bone=(%.3f %.3f %.3f)",
                    bone_count,
                    static_cast<unsigned long>(skeleton),
                    bone_x, bone_y, bone_z);
            }
            return false;
        }

        lua_x = root_x + bone_x;
        lua_y = root_y + bone_y;
        lua_z = root_z + bone_z;
        if (!std::isfinite(lua_x) || !std::isfinite(lua_y) || !std::isfinite(lua_z)) {
            if (detail && detail_size > 0) {
                std::snprintf(detail, detail_size, "invalid_anchor");
            }
            return false;
        }

        if (detail && detail_size > 0) {
            std::snprintf(detail, detail_size, "bone_count=%u skeleton=%08lx root=(%.3f %.3f %.3f) bone=(%.3f %.3f %.3f)",
                bone_count,
                static_cast<unsigned long>(skeleton),
                root_x, root_y, root_z,
                bone_x, bone_y, bone_z);
        }
        return true;
    }

    void probe_actor_candidates(const char* label, DWORD id, DWORD index, float lua_x, float lua_y, float lua_z) {
        char header[256] {};
        std::snprintf(header, sizeof(header), "boneprobe %s id=%lu index=%lu root=(%.3f %.3f %.3f)",
            label ? label : "unknown",
            static_cast<unsigned long>(id),
            static_cast<unsigned long>(index),
            lua_x, lua_y, lua_z);
        append_log(header);

        probe_actor_identity_near_roots(label, id, index, lua_x, lua_y, lua_z);
        append_log(label && std::strcmp(label, "source") == 0
            ? "boneprobe source coordinate scan disabled: render-thread process scan is too expensive"
            : "boneprobe target coordinate scan disabled: render-thread process scan is too expensive");
    }

    void probe_actor_coordinate_scan(const char* label, float lua_x, float lua_y, float lua_z) {
        SYSTEM_INFO info {};
        GetSystemInfo(&info);
        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
        const std::uintptr_t max_address = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);

        int matches = 0;
        int logged = 0;
        int bone_matches = 0;
        int regions = 0;
        unsigned long scanned_mb = 0;
        MEMORY_BASIC_INFORMATION mbi {};
        while (bone_matches < 4 && address < max_address && VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
            const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            std::uintptr_t region_end = region_base + mbi.RegionSize;
            if (region_end > max_address) {
                region_end = max_address;
            }
            if (mbi.State == MEM_COMMIT && is_readable_page(mbi.Protect) && mbi.RegionSize > 0x700) {
                ++regions;
                scanned_mb += static_cast<unsigned long>(mbi.RegionSize / (1024 * 1024));
                const std::uintptr_t scan_begin = region_base + 0x678;
                const std::uintptr_t scan_end = region_end > 0x684 ? region_end - 0x684 : scan_begin;
                for (std::uintptr_t pos = scan_begin; bone_matches < 4 && pos < scan_end; pos += sizeof(float)) {
                    float value = 0.0f;
                    if (!read_memory_raw(pos, value)) {
                        continue;
                    }

                    if (std::fabs(value - lua_x) > 0.05f) {
                        continue;
                    }

                    const std::uintptr_t actor = pos - 0x678;
                    float root_z = 0.0f;
                    float root_y = 0.0f;
                    if (!read_memory_raw(actor + 0x67C, root_z) || !read_memory_raw(actor + 0x680, root_y)) {
                        continue;
                    }

                    if (std::fabs(root_z - lua_z) > 0.10f || std::fabs(root_y - lua_y) > 0.10f) {
                        continue;
                    }

                    ++matches;
                    std::uint32_t skeleton_base_probe = 0;
                    if (!read_memory(actor + 0x6B8, skeleton_base_probe) || !is_readable_range(static_cast<std::uintptr_t>(skeleton_base_probe), 0x10)) {
                        if (logged < 16) {
                            char message[512] {};
                            std::snprintf(message, sizeof(message),
                                "boneprobe %s candidate=%d actor=%08lx bone_ok=false skeleton_base=%08lx invalid_skeleton_base",
                                label ? label : "unknown",
                                matches,
                                static_cast<unsigned long>(actor),
                                static_cast<unsigned long>(skeleton_base_probe));
                            append_log(message);
                            ++logged;
                        }
                        continue;
                    }

                    float bone_x = 0.0f;
                    float bone_y = 0.0f;
                    float bone_z = 0.0f;
                    char detail[512] {};
                    const bool bone_ok = read_bone_anchor(actor, 2, bone_x, bone_y, bone_z, detail, sizeof(detail));
                    if (bone_ok) {
                        ++bone_matches;
                    }
                    if (bone_ok || logged < 24) {
                        char message[1024] {};
                        std::snprintf(message, sizeof(message),
                            "boneprobe %s candidate=%d actor=%08lx bone_ok=%s anchor=(%.3f %.3f %.3f) %s",
                            label ? label : "unknown",
                            matches,
                            static_cast<unsigned long>(actor),
                            bone_ok ? "true" : "false",
                            bone_x, bone_y, bone_z,
                            detail);
                        append_log(message);
                        ++logged;
                    }
                }
            }

            address = region_end;
            if (address <= region_base) {
                break;
            }
        }

        if (matches == 0) {
            append_log(label && std::strcmp(label, "source") == 0 ? "boneprobe source no candidates" : "boneprobe target no candidates");
        }

        char summary[256] {};
        std::snprintf(summary, sizeof(summary), "boneprobe %s process_scan regions=%d scanned_mb~%lu coordinate_matches=%d logged=%d bone_matches=%d",
            label ? label : "unknown",
            regions,
            scanned_mb,
            matches,
            logged,
            bone_matches);
        append_log(summary);
    }

    void probe_actor_identity_near_roots(const char* label, DWORD id, DWORD index, float lua_x, float lua_y, float lua_z) {
        int found = 0;
        ModuleRange range = main_module_range();
        if (range.begin == 0 || range.end <= range.begin) {
            append_log(label && std::strcmp(label, "source") == 0 ? "boneprobe source identity scan skipped: no module range" : "boneprobe target identity scan skipped: no module range");
            return;
        }

        std::uintptr_t address = range.begin;
        MEMORY_BASIC_INFORMATION mbi {};
        while (found < 12 && address < range.end && VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
            const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            std::uintptr_t region_end = region_base + mbi.RegionSize;
            if (region_end > range.end) {
                region_end = range.end;
            }
            if (mbi.State == MEM_COMMIT && is_readable_page(mbi.Protect) && mbi.RegionSize > 0x100) {
                for (std::uintptr_t pos = region_base; found < 12 && pos + 4 < region_end; pos += 4) {
                    DWORD value = 0;
                    if (!read_memory(pos, value)) {
                        continue;
                    }

                    if (value != id && value != index) {
                        continue;
                    }

                    const std::uintptr_t window_start = pos > 0x900 ? pos - 0x900 : region_base;
                    const std::uintptr_t window_end = (pos + 0x900 < region_end) ? pos + 0x900 : region_end;
                    for (std::uintptr_t root_pos = window_start; root_pos + 0x10 < window_end; root_pos += 4) {
                        float root_x = 0.0f;
                        if (!read_memory(root_pos, root_x)) {
                            continue;
                        }

                        if (std::fabs(root_x - lua_x) > 0.05f) {
                            continue;
                        }

                        float root_z = 0.0f;
                        float root_y = 0.0f;
                        if (!read_memory(root_pos + 0x04, root_z) || !read_memory(root_pos + 0x08, root_y)) {
                            continue;
                        }

                        if (std::fabs(root_z - lua_z) > 0.10f || std::fabs(root_y - lua_y) > 0.10f) {
                            continue;
                        }

                        char message[512] {};
                        std::snprintf(message, sizeof(message),
                            "boneprobe %s identity_match=%d value=%lu value_addr=%08lx root_addr=%08lx root_offset_from_value=%ld root=(%.3f %.3f %.3f)",
                            label ? label : "unknown",
                            found + 1,
                            static_cast<unsigned long>(value),
                            static_cast<unsigned long>(pos),
                            static_cast<unsigned long>(root_pos),
                            static_cast<long>(root_pos) - static_cast<long>(pos),
                            root_x, root_y, root_z);
                        append_log(message);
                        ++found;
                        break;
                    }
                }
            }

            address = region_end;
            if (address <= region_base) {
                break;
            }
        }

        if (found == 0) {
            append_log(label && std::strcmp(label, "source") == 0 ? "boneprobe source no identity/root matches" : "boneprobe target no identity/root matches");
        }
    }

    ModuleRange main_module_range() const {
        HMODULE module = GetModuleHandleA(nullptr);
        if (!module) {
            return {};
        }

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
        IMAGE_DOS_HEADER dos {};
        if (!read_memory(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
            return {};
        }

        IMAGE_NT_HEADERS32 nt {};
        if (!read_memory(base + static_cast<std::uintptr_t>(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
            return {};
        }

        const std::uintptr_t size = nt.OptionalHeader.SizeOfImage;
        if (size == 0 || size > 0x2000000) {
            return {};
        }

        return {base, base + size};
    }

    float counter_milliseconds(LONGLONG ticks) const {
        if (performance_frequency_.QuadPart <= 0) {
            return 0.0f;
        }

        return static_cast<float>(static_cast<double>(ticks) * 1000.0
            / static_cast<double>(performance_frequency_.QuadPart));
    }

    void reset_benchmark_samples(DWORD now_ms) {
        benchmark_sample_count_ = 0;
        benchmark_projection_total_ = 0;
        benchmark_draw_call_total_ = 0;
        benchmark_state_read_total_ = 0;
        benchmark_last_report_ms_ = now_ms;
    }

    void set_benchmark_state(bool enabled, int line_count) {
        line_count = std::max(0, std::min(line_count, 128));
        if (benchmark_enabled_ == enabled && benchmark_line_count_ == line_count) {
            return;
        }

        benchmark_enabled_ = enabled;
        benchmark_line_count_ = enabled ? line_count : 0;
        benchmark_skip_next_sample_ = true;
        reset_benchmark_samples(GetTickCount());
    }

    int benchmark_percentile_index(int count, float percentile) const {
        if (count <= 1) {
            return 0;
        }

        const int index = static_cast<int>(std::ceil(percentile * static_cast<float>(count))) - 1;
        return std::max(0, std::min(index, count - 1));
    }

    void write_benchmark_report() {
        if (benchmark_path_[0] == '\0' || benchmark_sample_count_ <= 0) {
            return;
        }

        float frame_sorted[benchmark_max_samples_] {};
        float overlay_sorted[benchmark_max_samples_] {};
        float frame_total = 0.0f;
        float overlay_total = 0.0f;
        for (int i = 0; i < benchmark_sample_count_; ++i) {
            frame_sorted[i] = benchmark_frame_samples_[i];
            overlay_sorted[i] = benchmark_overlay_samples_[i];
            frame_total += benchmark_frame_samples_[i];
            overlay_total += benchmark_overlay_samples_[i];
        }
        std::sort(frame_sorted, frame_sorted + benchmark_sample_count_);
        std::sort(overlay_sorted, overlay_sorted + benchmark_sample_count_);

        const float frame_average = frame_total / static_cast<float>(benchmark_sample_count_);
        const float overlay_average = overlay_total / static_cast<float>(benchmark_sample_count_);
        const int p95 = benchmark_percentile_index(benchmark_sample_count_, 0.95f);
        const int p99 = benchmark_percentile_index(benchmark_sample_count_, 0.99f);
        const float sample_scale = 1.0f / static_cast<float>(benchmark_sample_count_);

        FILE* file = std::fopen(benchmark_path_, "wb");
        if (!file) {
            return;
        }

        std::fprintf(file,
            "{\"lines\":%d,\"samples\":%d,\"fps\":%.3f,"
            "\"frame_avg_ms\":%.3f,\"frame_p95_ms\":%.3f,\"frame_p99_ms\":%.3f,\"frame_max_ms\":%.3f,"
            "\"overlay_avg_ms\":%.3f,\"overlay_p95_ms\":%.3f,\"overlay_p99_ms\":%.3f,\"overlay_max_ms\":%.3f,"
            "\"projections_per_frame\":%.3f,\"draw_calls_per_frame\":%.3f,\"state_reads_per_frame\":%.3f}\n",
            benchmark_line_count_, benchmark_sample_count_, frame_average > 0.0f ? 1000.0f / frame_average : 0.0f,
            frame_average, frame_sorted[p95], frame_sorted[p99], frame_sorted[benchmark_sample_count_ - 1],
            overlay_average, overlay_sorted[p95], overlay_sorted[p99], overlay_sorted[benchmark_sample_count_ - 1],
            static_cast<float>(benchmark_projection_total_) * sample_scale,
            static_cast<float>(benchmark_draw_call_total_) * sample_scale,
            static_cast<float>(benchmark_state_read_total_) * sample_scale);
        std::fclose(file);
    }

    void record_benchmark_sample(float frame_ms, float overlay_ms) {
        const DWORD now_ms = GetTickCount();
        if (benchmark_skip_next_sample_) {
            benchmark_skip_next_sample_ = false;
            return;
        }
        if (frame_ms <= 0.0f || overlay_ms < 0.0f) {
            return;
        }

        if (benchmark_sample_count_ >= benchmark_max_samples_) {
            write_benchmark_report();
            reset_benchmark_samples(now_ms);
        }

        benchmark_frame_samples_[benchmark_sample_count_] = frame_ms;
        benchmark_overlay_samples_[benchmark_sample_count_] = overlay_ms;
        ++benchmark_sample_count_;
        benchmark_projection_total_ += benchmark_current_projections_;
        benchmark_draw_call_total_ += benchmark_current_draw_calls_;
        benchmark_state_read_total_ += benchmark_current_state_reads_;

        if (benchmark_sample_count_ >= 30 && now_ms - benchmark_last_report_ms_ >= 2000) {
            write_benchmark_report();
            reset_benchmark_samples(now_ms);
            benchmark_skip_next_sample_ = true;
        }
    }

    int read_lines(LineState* lines, int max_lines) {
        WIN32_FILE_ATTRIBUTE_DATA attributes_before {};
        if (!GetFileAttributesExA(state_path_, GetFileExInfoStandard, &attributes_before)) {
            state_cache_valid_ = false;
            set_benchmark_state(false, 0);
            return 0;
        }

        ULARGE_INTEGER write_time_before {};
        write_time_before.LowPart = attributes_before.ftLastWriteTime.dwLowDateTime;
        write_time_before.HighPart = attributes_before.ftLastWriteTime.dwHighDateTime;
        const unsigned long long file_size_before =
            (static_cast<unsigned long long>(attributes_before.nFileSizeHigh) << 32)
            | attributes_before.nFileSizeLow;

        if (state_cache_valid_
            && cached_state_write_time_ == write_time_before.QuadPart
            && cached_state_file_size_ == file_size_before) {
            return copy_cached_lines(lines, max_lines);
        }

        FILE* file = std::fopen(state_path_, "rb");
        if (!file) {
            return state_cache_valid_ ? copy_cached_lines(lines, max_lines) : 0;
        }

        ++benchmark_current_state_reads_;
        char buffer[131072] {};
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
        std::fclose(file);
        buffer[read] = '\0';

        std::size_t content_end = read;
        while (content_end > 0 && (buffer[content_end - 1] == ' ' || buffer[content_end - 1] == '\t'
            || buffer[content_end - 1] == '\r' || buffer[content_end - 1] == '\n')) {
            --content_end;
        }
        if (file_size_before >= sizeof(buffer) || content_end == 0 || buffer[0] != '{'
            || buffer[content_end - 1] != '}') {
            return state_cache_valid_ ? copy_cached_lines(lines, max_lines) : 0;
        }

        const char* benchmark = std::strstr(buffer, "\"benchmark\"");
        const bool benchmark_enabled = benchmark && parse_json_bool(benchmark, "\"enabled\"");
        const int benchmark_lines = benchmark
            ? static_cast<int>(parse_json_uint(benchmark, "\"lines\""))
            : 0;
        set_benchmark_state(benchmark_enabled, benchmark_lines);

        const bool has_lines = std::strstr(buffer, "\"source\"") != nullptr;

        const char* settings = has_lines ? std::strstr(buffer, "\"settings\"") : nullptr;
        if (settings) {
            const float opacity = parse_json_float(settings, "\"opacity\"");
            if (opacity >= 0.0f && opacity <= 1.0f) {
                opacity_scale_ = opacity;
            }
            const float width = parse_json_float(settings, "\"width\"");
            if (width >= 0.5f && width <= 2.0f) {
                width_scale_ = width;
            }
            const float glow = parse_json_float(settings, "\"glow\"");
            if (glow >= 0.5f && glow <= 2.0f) {
                glow_scale_ = glow;
            }
            const float source_height = parse_json_float(settings, "\"sourceheight\"");
            if (source_height >= -5.0f && source_height <= 5.0f) {
                source_height_offset_ = source_height;
            }
            const float target_height = parse_json_float(settings, "\"targetheight\"");
            if (target_height >= -5.0f && target_height <= 5.0f) {
                target_height_offset_ = target_height;
            }
        }
        boneprobe_requested_ = std::strstr(buffer, "\"boneprobe\":true") != nullptr;

        LineState parsed_lines[128] {};
        int count = 0;
        const int render_limit = benchmark_enabled_ ? max_lines : std::min(max_lines, 16);
        const char* lines_array = has_lines ? std::strstr(buffer, "\"lines\"") : nullptr;
        if (lines_array) {
            count = parse_line_array(lines_array, parsed_lines, render_limit);
        }

        if (count == 0) {
            const char* probe_lines_array = std::strstr(buffer, "\"probe_lines\"");
            if (probe_lines_array) {
                count = parse_line_array(probe_lines_array, parsed_lines, render_limit);
            }
        }

        cached_line_count_ = std::min(count, 128);
        for (int i = 0; i < cached_line_count_; ++i) {
            cached_lines_[i] = parsed_lines[i];
        }
        state_cache_valid_ = true;

        WIN32_FILE_ATTRIBUTE_DATA attributes_after {};
        if (GetFileAttributesExA(state_path_, GetFileExInfoStandard, &attributes_after)) {
            ULARGE_INTEGER write_time_after {};
            write_time_after.LowPart = attributes_after.ftLastWriteTime.dwLowDateTime;
            write_time_after.HighPart = attributes_after.ftLastWriteTime.dwHighDateTime;
            const unsigned long long file_size_after =
                (static_cast<unsigned long long>(attributes_after.nFileSizeHigh) << 32)
                | attributes_after.nFileSizeLow;
            if (write_time_after.QuadPart == write_time_before.QuadPart
                && file_size_after == file_size_before) {
                cached_state_write_time_ = write_time_after.QuadPart;
                cached_state_file_size_ = file_size_after;
            }
        }

        return copy_cached_lines(lines, max_lines);
    }

    int copy_cached_lines(LineState* lines, int max_lines) const {
        const int count = std::min(cached_line_count_, max_lines);
        for (int i = 0; i < count; ++i) {
            lines[i] = cached_lines_[i];
        }
        return count;
    }

    int parse_line_array(const char* array_start, LineState* lines, int max_lines) {
        int count = 0;
        const char* cursor = array_start;
        while (count < max_lines) {
            const char* source = std::strstr(cursor, "\"source\"");
            if (!source) {
                break;
            }

            const char* end = std::strchr(cursor, ']');
            if (end && source > end) {
                break;
            }

            const char* target = std::strstr(source, "\"target\"");
            if (!target || (end && target > end)) {
                break;
            }

            LineState& line = lines[count];
            line.uid = parse_json_uint(cursor, "\"uid\"");
            line.source_id = parse_json_uint(source, "\"id\"");
            line.source_index = parse_json_uint(source, "\"index\"");
            line.source_x = parse_json_float(source, "\"x\"");
            line.source_y = parse_json_float(source, "\"y\"");
            line.source_z = parse_json_float(source, "\"z\"");
            line.source_model_size = parse_json_float(source, "\"model_size\"");
            line.source_model_scale = parse_json_float(source, "\"model_scale\"");
            if (line.source_model_scale <= 0.0f) {
                line.source_model_scale = 1.0f;
            }
            line.source_short_anchor = parse_json_bool(source, "\"short_anchor\"");
            line.source_floating_anchor = parse_json_bool(source, "\"floating_anchor\"");
            line.source_is_npc = parse_json_bool(source, "\"npc\"");
            line.target_id = parse_json_uint(target, "\"id\"");
            line.target_index = parse_json_uint(target, "\"index\"");
            line.target_x = parse_json_float(target, "\"x\"");
            line.target_y = parse_json_float(target, "\"y\"");
            line.target_z = parse_json_float(target, "\"z\"");
            line.target_model_size = parse_json_float(target, "\"model_size\"");
            line.target_model_scale = parse_json_float(target, "\"model_scale\"");
            if (line.target_model_scale <= 0.0f) {
                line.target_model_scale = 1.0f;
            }
            line.target_short_anchor = parse_json_bool(target, "\"short_anchor\"");
            line.target_floating_anchor = parse_json_bool(target, "\"floating_anchor\"");
            line.target_is_npc = parse_json_bool(target, "\"npc\"");
            line.color = parse_json_uint(target, "\"color\"");
            if (line.color == 0) {
                line.color = 0xEFFFFFFF;
            }
            line.timeout = parse_json_float(target, "\"timeout\"");
            if (line.timeout <= 0.0f) {
                line.timeout = 1.5f;
            }

            ++count;
            cursor = target + 8;
        }

        return count;
    }

    float parse_json_float(const char* start, const char* key) {
        const char* key_pos = std::strstr(start, key);
        if (!key_pos) {
            return 0.0f;
        }

        const char* colon = std::strchr(key_pos, ':');
        if (!colon) {
            return 0.0f;
        }

        return static_cast<float>(std::strtod(colon + 1, nullptr));
    }

    DWORD parse_json_uint(const char* start, const char* key) {
        const char* key_pos = std::strstr(start, key);
        if (!key_pos) {
            return 0;
        }

        const char* colon = std::strchr(key_pos, ':');
        if (!colon) {
            return 0;
        }

        return static_cast<DWORD>(std::strtoul(colon + 1, nullptr, 10));
    }

    bool parse_json_bool(const char* start, const char* key) {
        const char* key_pos = std::strstr(start, key);
        if (!key_pos) {
            return false;
        }

        const char* colon = std::strchr(key_pos, ':');
        if (!colon) {
            return false;
        }

        while (*(++colon) == ' ') {
        }

        return std::strncmp(colon, "true", 4) == 0;
    }

    bool refresh_projection_matrices() {
        if (!d3d_device_) {
            return false;
        }

        if (FAILED(d3d_device_->GetTransform(D3DTS_VIEW, &cached_view_)) ||
            FAILED(d3d_device_->GetTransform(D3DTS_PROJECTION, &cached_projection_))) {
            projection_matrices_valid_ = false;
            return false;
        }

        projection_matrices_valid_ = true;
        return true;
    }

    bool live_world_to_screen(float lua_x, float lua_y, float lua_z, const D3DVIEWPORT8& viewport, float& screen_x, float& screen_y) {
        if (benchmark_enabled_) {
            ++benchmark_current_projections_;
        }
        if (!projection_matrices_valid_) {
            return false;
        }

        const D3DMATRIX& view = cached_view_;
        const D3DMATRIX& projection = cached_projection_;

        // FFXI Lua positions use x/y as ground-plane coordinates and z as height.
        // D3D's observed model translations map those to x/z ground-plane and y height.
        const float world_x = lua_x;
        const float world_y = lua_z;
        const float world_z = lua_y;

        const float view_x = world_x * view.m[0][0] + world_y * view.m[1][0] + world_z * view.m[2][0] + view.m[3][0];
        const float view_y = world_x * view.m[0][1] + world_y * view.m[1][1] + world_z * view.m[2][1] + view.m[3][1];
        const float view_z = world_x * view.m[0][2] + world_y * view.m[1][2] + world_z * view.m[2][2] + view.m[3][2];
        const float view_w = world_x * view.m[0][3] + world_y * view.m[1][3] + world_z * view.m[2][3] + view.m[3][3];

        const float clip_x = view_x * projection.m[0][0] + view_y * projection.m[1][0] + view_z * projection.m[2][0] + view_w * projection.m[3][0];
        const float clip_y = view_x * projection.m[0][1] + view_y * projection.m[1][1] + view_z * projection.m[2][1] + view_w * projection.m[3][1];
        const float clip_w = view_x * projection.m[0][3] + view_y * projection.m[1][3] + view_z * projection.m[2][3] + view_w * projection.m[3][3];

        if (std::fabs(clip_w) <= 0.0001f) {
            return false;
        }

        const float ndc_x = clip_x / clip_w;
        const float ndc_y = clip_y / clip_w;
        if (clip_w < 0.0f || ndc_x < -4.0f || ndc_x > 4.0f || ndc_y < -4.0f || ndc_y > 4.0f) {
            return false;
        }

        screen_x = static_cast<float>(viewport.X) + (ndc_x + 1.0f) * static_cast<float>(viewport.Width) * 0.5f;
        screen_y = static_cast<float>(viewport.Y) + (1.0f - ndc_y) * static_cast<float>(viewport.Height) * 0.5f;
        return true;
    }

    void append_segment_quad(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float thickness, DWORD color, float trim_end) {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.01f) {
            return;
        }

        if (trim_end > 0.0f) {
            const float visible_length = std::fmax(0.01f, length - trim_end);
            x2 = x1 + (dx / length) * visible_length;
            y2 = y1 + (dy / length) * visible_length;
        }

        const float clipped_dx = x2 - x1;
        const float clipped_dy = y2 - y1;
        const float clipped_length = std::sqrt(clipped_dx * clipped_dx + clipped_dy * clipped_dy);
        if (clipped_length <= 0.01f) {
            return;
        }

        const float nx = -clipped_dy / clipped_length * thickness * 0.5f;
        const float ny = clipped_dx / clipped_length * thickness * 0.5f;

        const DrawVertex a {x1 + nx, y1 + ny, 0.0f, 1.0f, color};
        const DrawVertex b {x1 - nx, y1 - ny, 0.0f, 1.0f, color};
        const DrawVertex c {x2 + nx, y2 + ny, 0.0f, 1.0f, color};
        const DrawVertex d {x2 - nx, y2 - ny, 0.0f, 1.0f, color};

        vertices[vertex_count++] = a;
        vertices[vertex_count++] = b;
        vertices[vertex_count++] = c;
        vertices[vertex_count++] = c;
        vertices[vertex_count++] = b;
        vertices[vertex_count++] = d;
    }

    void append_segment_quad_clipped_to_circle(DrawVertex* vertices, int& vertex_count, float x1, float y1, float x2, float y2, float thickness, DWORD color, float circle_x, float circle_y, float radius) {
        if (radius <= 0.0f) {
            append_segment_quad(vertices, vertex_count, x1, y1, x2, y2, thickness, color, 0.0f);
            return;
        }

        const float sx = x1 - circle_x;
        const float sy = y1 - circle_y;
        const float ex = x2 - circle_x;
        const float ey = y2 - circle_y;
        const float start_distance_sq = sx * sx + sy * sy;
        const float end_distance_sq = ex * ex + ey * ey;
        const float radius_sq = radius * radius;

        if (start_distance_sq <= radius_sq && end_distance_sq <= radius_sq) {
            return;
        }

        if (start_distance_sq > radius_sq && end_distance_sq > radius_sq) {
            append_segment_quad(vertices, vertex_count, x1, y1, x2, y2, thickness, color, 0.0f);
            return;
        }

        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float a = dx * dx + dy * dy;
        const float b = 2.0f * (sx * dx + sy * dy);
        const float c = start_distance_sq - radius_sq;
        const float discriminant = b * b - 4.0f * a * c;
        if (a <= 0.0001f || discriminant < 0.0f) {
            return;
        }

        const float root = std::sqrt(discriminant);
        const float t0 = (-b - root) / (2.0f * a);
        const float t1 = (-b + root) / (2.0f * a);
        float t = -1.0f;
        if (t0 >= 0.0f && t0 <= 1.0f) {
            t = t0;
        }
        if (t1 >= 0.0f && t1 <= 1.0f && (t < 0.0f || t1 < t)) {
            t = t1;
        }
        if (t < 0.0f) {
            return;
        }

        const float ix = x1 + dx * t;
        const float iy = y1 + dy * t;
        if (start_distance_sq > radius_sq) {
            append_segment_quad(vertices, vertex_count, x1, y1, ix, iy, thickness, color, 0.0f);
        } else {
            append_segment_quad(vertices, vertex_count, ix, iy, x2, y2, thickness, color, 0.0f);
        }
    }

    bool begin_draw_state() {
        if (draw_state_active_) {
            return true;
        }

        if (!d3d_device_) {
            probe_device("draw");
        }

        if (!d3d_device_) {
            return false;
        }

        saved_texture_ = nullptr;
        d3d_device_->GetVertexShader(&saved_shader_);
        d3d_device_->GetRenderState(D3DRS_ALPHABLENDENABLE, &saved_alpha_);
        d3d_device_->GetRenderState(D3DRS_SRCBLEND, &saved_src_);
        d3d_device_->GetRenderState(D3DRS_DESTBLEND, &saved_dest_);
        d3d_device_->GetRenderState(D3DRS_ZENABLE, &saved_z_);
        d3d_device_->GetRenderState(D3DRS_LIGHTING, &saved_lighting_);
        d3d_device_->GetRenderState(D3DRS_CULLMODE, &saved_cull_);
        d3d_device_->GetTexture(0, &saved_texture_);

        d3d_device_->SetTexture(0, nullptr);
        d3d_device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d3d_device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d3d_device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d3d_device_->SetRenderState(D3DRS_ZENABLE, FALSE);
        d3d_device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        d3d_device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d3d_device_->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

        draw_state_active_ = true;
        return true;
    }

    void end_draw_state() {
        if (!draw_state_active_ || !d3d_device_) {
            return;
        }

        d3d_device_->SetTexture(0, saved_texture_);
        if (saved_texture_) {
            saved_texture_->Release();
            saved_texture_ = nullptr;
        }
        d3d_device_->SetRenderState(D3DRS_ALPHABLENDENABLE, saved_alpha_);
        d3d_device_->SetRenderState(D3DRS_SRCBLEND, saved_src_);
        d3d_device_->SetRenderState(D3DRS_DESTBLEND, saved_dest_);
        d3d_device_->SetRenderState(D3DRS_ZENABLE, saved_z_);
        d3d_device_->SetRenderState(D3DRS_LIGHTING, saved_lighting_);
        d3d_device_->SetRenderState(D3DRS_CULLMODE, saved_cull_);
        d3d_device_->SetVertexShader(saved_shader_);
        draw_state_active_ = false;
    }

    void begin_line_batch() {
        line_batch_vertex_count_ = 0;
        line_batch_active_ = true;
    }

    void flush_line_batch() {
        if (line_batch_vertex_count_ <= 0) {
            return;
        }

        submit_vertices(D3DPT_TRIANGLELIST, static_cast<UINT>(line_batch_vertex_count_ / 3),
            line_batch_vertices_, sizeof(DrawVertex));
        line_batch_vertex_count_ = 0;
    }

    void end_line_batch() {
        flush_line_batch();
        line_batch_active_ = false;
    }

    void draw_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride) {
        if (line_batch_active_ && primitive_type == D3DPT_TRIANGLELIST && stride == sizeof(DrawVertex)) {
            const UINT vertex_count = primitive_count * 3;
            if (vertex_count > static_cast<UINT>(max_line_batch_vertices_)) {
                flush_line_batch();
                submit_vertices(primitive_type, primitive_count, vertices, stride);
                return;
            }

            if (line_batch_vertex_count_ + static_cast<int>(vertex_count) > max_line_batch_vertices_) {
                flush_line_batch();
            }
            std::memcpy(line_batch_vertices_ + line_batch_vertex_count_, vertices,
                static_cast<std::size_t>(vertex_count) * sizeof(DrawVertex));
            line_batch_vertex_count_ += static_cast<int>(vertex_count);
            return;
        }

        submit_vertices(primitive_type, primitive_count, vertices, stride);
    }

    void submit_vertices(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const DrawVertex* vertices, UINT stride) {
        if (benchmark_enabled_) {
            ++benchmark_current_draw_calls_;
        }

        const bool owns_draw_state = !draw_state_active_;
        if (owns_draw_state && !begin_draw_state()) {
            return;
        }

        d3d_device_->DrawPrimitiveUP(primitive_type, primitive_count, vertices, stride);

        if (owns_draw_state) {
            end_draw_state();
        }
    }

    void initialize_paths_from_module() {
        char module_path[MAX_PATH] {};
        if (!g_module || !GetModuleFileNameA(g_module, module_path, sizeof(module_path))) {
            return;
        }

        char* slash = std::strrchr(module_path, '\\');
        if (!slash) {
            return;
        }

        *slash = '\0';
        char settings_root[MAX_PATH] {};
        std::snprintf(settings_root, sizeof(settings_root), "%s\\settings", module_path);
        CreateDirectoryA(settings_root, nullptr);
        std::snprintf(settings_root, sizeof(settings_root), "%s\\settings\\TargetLines", module_path);
        CreateDirectoryA(settings_root, nullptr);
        std::snprintf(log_path_, sizeof(log_path_), "%s\\settings\\TargetLines\\native.log", module_path);
        std::snprintf(state_path_, sizeof(state_path_), "%s\\settings\\TargetLines\\lines.json", module_path);
        std::snprintf(benchmark_path_, sizeof(benchmark_path_), "%s\\settings\\TargetLines\\benchmark.json", module_path);
    }

    void append_log(const char* message) {
        if (log_path_[0] == '\0') {
            return;
        }

        FILE* file = std::fopen(log_path_, "ab");
        if (!file) {
            return;
        }

        std::time_t now = std::time(nullptr);
        std::tm* local = std::localtime(&now);
        if (local) {
            char stamp[32] {};
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
            std::fprintf(file, "%s %s\n", stamp, message);
        } else {
            std::fprintf(file, "%s\n", message);
        }

        std::fclose(file);
    }

    int count_lines() {
        if (benchmark_enabled_) {
            ++benchmark_current_state_reads_;
        }
        FILE* file = std::fopen(state_path_, "rb");
        if (!file) {
            return -static_cast<int>(errno);
        }

        int count = 0;
        constexpr char needle[] = "\"source\"";
        constexpr int needle_length = static_cast<int>(sizeof(needle) - 1);
        int matched = 0;

        int ch = 0;
        while ((ch = std::fgetc(file)) != EOF) {
            if (ch == needle[matched]) {
                ++matched;
                if (matched == needle_length) {
                    ++count;
                    matched = 0;
                }
            } else {
                matched = ch == needle[0] ? 1 : 0;
            }
        }

        std::fclose(file);
        return count;
    }
    char state_path_[1024] {};
    char log_path_[1024] {};
    char benchmark_path_[1024] {};
    unsigned long postrender_calls_ = 0;
    int last_line_count_ = 0;
    IDirect3DDevice8* d3d_device_ = nullptr;
    D3DMATRIX cached_view_ {};
    D3DMATRIX cached_projection_ {};
    bool projection_matrices_valid_ = false;
    DWORD saved_shader_ = 0;
    DWORD saved_alpha_ = 0;
    DWORD saved_src_ = 0;
    DWORD saved_dest_ = 0;
    DWORD saved_z_ = 0;
    DWORD saved_lighting_ = 0;
    DWORD saved_cull_ = 0;
    IDirect3DBaseTexture8* saved_texture_ = nullptr;
    bool draw_state_active_ = false;
    static constexpr int max_line_batch_vertices_ = 1400;
    DrawVertex line_batch_vertices_[max_line_batch_vertices_] {};
    int line_batch_vertex_count_ = 0;
    bool line_batch_active_ = false;
    bool overlay_enabled_ = true;
    bool debug_bar_enabled_ = false;
    bool matrix_probe_pending_ = false;
    float source_height_offset_ = -0.75f;
    float target_height_offset_ = -1.35f;
    float arc_height_offset_ = -2.0f;
    float opacity_scale_ = 0.8f;
    float width_scale_ = 1.0f;
    float glow_scale_ = 1.0f;
    int dynamic_bone_ = 21;
    static constexpr int benchmark_max_samples_ = 1024;
    LARGE_INTEGER performance_frequency_ {};
    LONGLONG previous_postrender_counter_ = 0;
    bool benchmark_enabled_ = false;
    bool benchmark_skip_next_sample_ = false;
    int benchmark_line_count_ = 0;
    int benchmark_sample_count_ = 0;
    DWORD benchmark_last_report_ms_ = 0;
    float benchmark_frame_samples_[benchmark_max_samples_] {};
    float benchmark_overlay_samples_[benchmark_max_samples_] {};
    unsigned long long benchmark_projection_total_ = 0;
    unsigned long long benchmark_draw_call_total_ = 0;
    unsigned long long benchmark_state_read_total_ = 0;
    unsigned int benchmark_current_projections_ = 0;
    unsigned int benchmark_current_draw_calls_ = 0;
    unsigned int benchmark_current_state_reads_ = 0;
    LineState cached_lines_[128] {};
    int cached_line_count_ = 0;
    unsigned long long cached_state_write_time_ = 0;
    unsigned long long cached_state_file_size_ = 0;
    bool state_cache_valid_ = false;
    bool boneprobe_requested_ = false;
    DWORD last_boneprobe_ms_ = 0;
    static constexpr int max_active_lines_ = 128;
    ActiveLine active_lines_[max_active_lines_] {};
    int active_line_count_ = 0;
};

std::uint32_t GetInterfaceVersion() {
    append_module_log("GetInterfaceVersion called returning 0x04070300");
    return WINDOWER_INTERFACE_VERSION;
}

PluginBase* CreateInstance() {
    append_module_log("CreateInstance called");
    return new TargetLinesPlugin();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        append_module_log("DllMain process attach");
    } else if (reason == DLL_PROCESS_DETACH) {
        append_module_log("DllMain process detach");
    }

    return TRUE;
}
