/*
 * Enemy Counter - Minimal arcdps WvW enemy player counter
 *
 * Uses ONLY the official arcdps C-API. No memory reading or injection.
 * All enemy detection happens inside the cb_combat callback.
 */

#include <stdint.h>
#include <Windows.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstring>
#include "imgui/imgui.h"

/* ============================================================
 * arcdps C-API types (official API definitions)
 * ============================================================ */

enum cbtstatechange {
    CBTS_NONE,
    CBTS_ENTERCOMBAT,
    CBTS_EXITCOMBAT,
    CBTS_CHANGEUP,
    CBTS_CHANGEDEAD,
    CBTS_CHANGEDOWN,
    CBTS_SPAWN,
    CBTS_DESPAWN,
    CBTS_HEALTHUPDATE,
    CBTS_LOGSTART,
    CBTS_LOGEND,
    CBTS_WEAPSWAP,
    CBTS_MAXHEALTHUPDATE,
    CBTS_POINTOFVIEW,
    CBTS_LANGUAGE,
    CBTS_GWBUILD,
    CBTS_SHARDID,
    CBTS_REWARD,
    CBTS_BUFFINITIAL,
    CBTS_POSITION,
    CBTS_VELOCITY,
    CBTS_FACING,
    CBTS_TEAMCHANGE,
    CBTS_ATTACKTARGET,
    CBTS_TARGETABLE,
    CBTS_MAPID,
    CBTS_REPLINFO,
    CBTS_STACKACTIVE,
    CBTS_STACKRESET,
    CBTS_GUILD,
    CBTS_BUFFINFO,
    CBTS_BUFFFORMULA,
    CBTS_SKILLINFO,
    CBTS_SKILLTIMING,
    CBTS_BREAKBARSTATE,
    CBTS_BREAKBARPERCENT,
    CBTS_ERROR,
    CBTS_TAG,
    CBTS_BARRIERUPDATE,
    CBTS_STATRESET,
    CBTS_EXTENSION,
    CBTS_APIDELAYED,
    CBTS_INSTANCESTART,
    CBTS_TICKRATE,
    CBTS_LAST90BEFOREDOWN,
    CBTS_EFFECT,
    CBTS_IDTOGUID,
    CBTS_UNKNOWN
};

enum iff {
    IFF_FRIEND,
    IFF_FOE,
    IFF_UNKNOWN
};

typedef struct arcdps_exports {
    uintptr_t size;
    uint32_t sig;
    uint32_t imguivers;
    const char* out_name;
    const char* out_build;
    void* wnd_nofilter;
    void* combat;
    void* imgui;
    void* options_tab;
    void* combat_local;
    void* wnd_filter;
    void* options_windows;
} arcdps_exports;

typedef struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t value;
    int32_t buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t iff;
    uint8_t buff;
    uint8_t result;
    uint8_t is_activation;
    uint8_t is_buffremove;
    uint8_t is_ninety;
    uint8_t is_fifty;
    uint8_t is_moving;
    uint8_t is_statechange;
    uint8_t is_flanking;
    uint8_t is_shields;
    uint8_t is_offcycle;
    uint8_t pad61;
    uint8_t pad62;
    uint8_t pad63;
    uint8_t pad64;
} cbtevent;

typedef struct ag {
    char* name;
    uintptr_t id;
    uint32_t prof;
    uint32_t elite;
    uint32_t self;
    uint16_t team;
} ag;

/* ============================================================
 * Plugin exports / globals
 * ============================================================ */

static arcdps_exports arc_exports;
static char* arcvers = nullptr;

/* arcdps-provided helper exports */
static void* arclog = nullptr;
static const char* (*arccontext_0x510)() = nullptr;
static const char* arccontext = nullptr;
static void (*arccolors)(ImVec4** out) = nullptr;
static ImVec4* prof_colors = nullptr;

/* plugin state */
static bool enabled = true;
static bool show_window = true;
static bool show_class_list = true;
static bool use_prof_colors = true;
static bool active_combat_only = false;
static bool show_total_big = true;
static int sort_mode = 0; // 0 = by count, 1 = alphabetical, 2 = profession order
static int timeout_seconds = 30;
static int history_count = 10;
static bool show_history_window = false;
static ImGuiWindowFlags wFlags = ImGuiWindowFlags_None;

static UINT hotkey_vk = VK_F7;
static bool hotkey_pressed = false;
static bool waiting_for_hotkey = false;
static uint64_t last_event_time = 0;
static uint64_t last_damage_time = 0;
static std::chrono::steady_clock::time_point last_damage_realtime;
static uint16_t local_player_team = 0;

static std::mutex enemy_mutex;

static constexpr uint64_t ENEMY_TIMEOUT_MS_DEFAULT = 30000;
static constexpr uint32_t PROFESSION_NONE = 0;
static constexpr uint32_t ELITE_FLAG_NPC = 0xFFFFFFFF;

struct EnemyInfo {
    uint64_t last_seen_ms;
    uint32_t profession;
    uint32_t elite;
    uint16_t team;
    std::string name;
};

struct FightSnapshot {
    std::chrono::system_clock::time_point timestamp;
    uint64_t event_time;
    uint32_t total_enemies;
    std::vector<std::pair<std::string, uint32_t>> class_counts;
};

static std::mutex history_mutex;
static std::vector<FightSnapshot> fight_history;

static std::unordered_map<uint64_t, EnemyInfo> enemy_agents;

/* ============================================================
 * Forward declarations
 * ============================================================ */

static arcdps_exports* mod_init();
static uintptr_t mod_release();
static uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision);
static uintptr_t mod_imgui(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc);
static UINT mod_wnd_nofilter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void mod_options_windows(char* windowname);
static void mod_options_tab();

/* ============================================================
 * Utility helpers
 * ============================================================ */

static uint64_t current_time_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

static bool is_damage_event(cbtevent* ev) {
    if (!ev) return false;
    if (ev->is_statechange != CBTS_NONE) return false;
    return ev->value != 0 || ev->buff_dmg != 0;
}

static bool is_fight_active() {
    if (last_damage_time == 0) return false;
    auto now = std::chrono::steady_clock::now();
    uint64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_damage_realtime).count();
    uint64_t timeout_ms = static_cast<uint64_t>(timeout_seconds) * 1000ULL;
    return elapsed_ms <= timeout_ms;
}

static void log_arc(const char* str) {
    size_t(*log)(char*) = reinterpret_cast<size_t(*)(char*)>(arclog);
    if (log) (*log)(const_cast<char*>(str));
}

static bool is_wvw_map(uint32_t map_id) {
    switch (map_id) {
        case 0x0026: // Eternal Battlegrounds
        case 0x005F: // Green Borderlands
        case 0x0060: // Blue Borderlands
        case 0x044B: // Red Borderlands
        case 0x0523: // WvW Lounge
            return true;
        default:
            return false;
    }
}

static uint32_t current_map_id() {
    if (!arccontext) return 0;
    return static_cast<uint32_t>(
        (static_cast<unsigned char>(arccontext[0x701]) << 8) |
        (static_cast<unsigned char>(arccontext[0x700]))
    );
}

/* Player agents have a valid profession and are not flagged as NPCs/gadgets.
   Minions, mounts and most NPCs fail this check. */
static bool is_player_agent(const ag* agent) {
    if (!agent) return false;
    if (agent->prof == PROFESSION_NONE) return false;
    if (agent->elite == ELITE_FLAG_NPC) return false;
    if (agent->self != 0) return false;
    return true;
}

/* Update our cached local team ID whenever the local player appears. */
static void update_local_team(const ag* agent) {
    if (agent && agent->self != 0) {
        local_player_team = agent->team;
    }
}

/* In WvW a player is only an enemy if it has a known team and that team is
   different from the local player's team. Friendly players, squadmates and
   the local player itself are ignored. */
static bool is_enemy_agent(const ag* agent) {
    if (!agent) return false;
    if (agent->team == 0) return false;
    if (local_player_team != 0 && agent->team == local_player_team) return false;
    return true;
}

static const char* get_profession_name(uint32_t prof, uint32_t elite) {
    switch (elite) {
        case 5:  return "Druid";
        case 7:  return "Daredevil";
        case 18: return "Berserker";
        case 27: return "Dragonhunter";
        case 34: return "Reaper";
        case 40: return "Chronomancer";
        case 43: return "Scrapper";
        case 48: return "Tempest";
        case 52: return "Herald";
        case 55: return "Soulbeast";
        case 56: return "Weaver";
        case 57: return "Holosmith";
        case 58: return "Deadeye";
        case 59: return "Mirage";
        case 60: return "Scourge";
        case 61: return "Spellbreaker";
        case 62: return "Firebrand";
        case 63: return "Renegade";
        case 64: return "Harbinger";
        case 65: return "Willbender";
        case 66: return "Virtuoso";
        case 67: return "Catalyst";
        case 68: return "Bladesworn";
        case 69: return "Vindicator";
        case 70: return "Mechanist";
        case 71: return "Specter";
        case 72: return "Untamed";
        default: break;
    }
    switch (prof) {
        case 1: return "Guardian";
        case 2: return "Warrior";
        case 3: return "Engineer";
        case 4: return "Ranger";
        case 5: return "Thief";
        case 6: return "Elementalist";
        case 7: return "Mesmer";
        case 8: return "Necromancer";
        case 9: return "Revenant";
        default: return "Unknown";
    }
}

static uint32_t profession_name_to_index(const std::string& name) {
    if (name == "Guardian")     return 1;
    if (name == "Warrior")      return 2;
    if (name == "Engineer")     return 3;
    if (name == "Ranger")       return 4;
    if (name == "Thief")        return 5;
    if (name == "Elementalist") return 6;
    if (name == "Mesmer")       return 7;
    if (name == "Necromancer")  return 8;
    if (name == "Revenant")     return 9;
    return 0; // Unknown
}

/* Map a profession or elite specialization name to the base profession
   color index used by arcdps. This lets e.g. "Tempest" use the elementalist
   color instead of falling back to "Unknown". */
static uint32_t profession_name_to_color_index(const std::string& name) {
    if (name == "Druid" || name == "Soulbeast" || name == "Untamed") return 4;
    if (name == "Daredevil" || name == "Deadeye" || name == "Specter") return 5;
    if (name == "Berserker" || name == "Spellbreaker" || name == "Bladesworn") return 2;
    if (name == "Dragonhunter" || name == "Firebrand" || name == "Willbender") return 1;
    if (name == "Reaper" || name == "Scourge" || name == "Harbinger") return 8;
    if (name == "Chronomancer" || name == "Mirage" || name == "Virtuoso") return 7;
    if (name == "Scrapper" || name == "Holosmith" || name == "Mechanist") return 3;
    if (name == "Tempest" || name == "Weaver" || name == "Catalyst") return 6;
    if (name == "Herald" || name == "Renegade" || name == "Vindicator") return 9;
    return profession_name_to_index(name);
}

/* ============================================================
 * Core tracking
 * ============================================================ */

static void cleanup_expired_enemies(uint64_t event_time) {
    if (event_time == 0) return;
    std::lock_guard<std::mutex> lock(enemy_mutex);
    uint64_t timeout_ms = static_cast<uint64_t>(timeout_seconds) * 1000ULL;
    for (auto it = enemy_agents.begin(); it != enemy_agents.end();) {
        if (event_time > it->second.last_seen_ms &&
            (event_time - it->second.last_seen_ms) > timeout_ms) {
            it = enemy_agents.erase(it);
        } else {
            ++it;
        }
    }
}

static std::vector<std::pair<std::string, uint32_t>> build_class_counts_internal() {
    std::vector<std::pair<std::string, uint32_t>> class_counts;
    for (const auto& pair : enemy_agents) {
        const EnemyInfo& info = pair.second;
        const char* name = get_profession_name(info.profession, info.elite);
        bool found = false;
        for (auto& c : class_counts) {
            if (c.first == name) {
                c.second++;
                found = true;
                break;
            }
        }
        if (!found) {
            class_counts.emplace_back(name, 1);
        }
    }
    std::sort(class_counts.begin(), class_counts.end(),
        [](const std::pair<std::string, uint32_t>& a, const std::pair<std::string, uint32_t>& b) {
            return a.second > b.second;
        }
    );
    return class_counts;
}

static std::vector<std::pair<std::string, uint32_t>> build_class_counts() {
    std::lock_guard<std::mutex> lock(enemy_mutex);
    return build_class_counts_internal();
}

static void record_fight_snapshot_unlocked(uint64_t event_time) {
    FightSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.event_time = event_time;
    snapshot.total_enemies = static_cast<uint32_t>(enemy_agents.size());
    snapshot.class_counts = build_class_counts_internal();

    std::lock_guard<std::mutex> hlock(history_mutex);
    uint64_t timeout_ms = static_cast<uint64_t>(timeout_seconds) * 1000ULL;
    if (!fight_history.empty() &&
        event_time >= fight_history.front().event_time &&
        (event_time - fight_history.front().event_time) <= timeout_ms) {
        /* Still within the enemy timeout window: the current fight is a
           continuation of the last recorded one, so update it in place
           instead of creating a separate history entry. */
        fight_history.front() = std::move(snapshot);
    } else {
        fight_history.insert(fight_history.begin(), std::move(snapshot));
        if (static_cast<int>(fight_history.size()) > history_count) {
            fight_history.resize(history_count);
        }
    }
}

static void record_fight_snapshot(uint64_t event_time) {
    std::lock_guard<std::mutex> lock(enemy_mutex);
    if (enemy_agents.empty()) return;
    record_fight_snapshot_unlocked(event_time);
}

static void check_fight_end(uint64_t event_time) {
    if (last_damage_time == 0) return;
    if (is_fight_active()) return;

    std::lock_guard<std::mutex> lock(enemy_mutex);
    record_fight_snapshot_unlocked(event_time != 0 ? event_time : last_event_time);
    enemy_agents.clear();
    last_damage_time = 0;
}

static void record_enemy(const ag* agent, uint64_t event_time, bool active) {
    if (!is_player_agent(agent) || !is_enemy_agent(agent) || event_time == 0) return;

    std::lock_guard<std::mutex> lock(enemy_mutex);
    auto it = enemy_agents.find(static_cast<uint64_t>(agent->id));

    if (active_combat_only && !active) {
        /* when filtering active-only, still refresh timeout for already tracked
           agents on any combat event so they don't expire while nearby */
        if (it != enemy_agents.end()) {
            it->second.last_seen_ms = event_time;
        }
        return;
    }

    if (it == enemy_agents.end()) {
        EnemyInfo info;
        info.last_seen_ms = event_time;
        info.profession = agent->prof;
        info.elite = agent->elite;
        info.team = agent->team;
        info.name = agent->name ? agent->name : "";
        enemy_agents.emplace(static_cast<uint64_t>(agent->id), std::move(info));
    } else {
        it->second.last_seen_ms = event_time;
        it->second.profession = agent->prof;
        it->second.elite = agent->elite;
        it->second.team = agent->team;
        if (agent->name) it->second.name = agent->name;
    }
}

/* ============================================================
 * arcdps callbacks
 * ============================================================ */

static uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) {
    (void)skillname;
    (void)id;
    (void)revision;

    if (!enabled) return 0;

    /* Special non-event call: src is an agent being added to tracking.
       dst->self indicates whether it is the local player. We use this
       to detect that we are in a WvW map without reading client memory. */
    if (!ev) {
        if (src && dst && src->prof && src->elite != ELITE_FLAG_NPC && dst->self != 0) {
            /* keep window available on every map for testing; later restrict to WvW if desired */
            show_window = true;
        }
        update_local_team(dst);
        return 0;
    }

    if (!enabled) return 0;

    uint64_t event_time = ev ? ev->time : 0;
    last_event_time = event_time;

    if (ev->is_statechange == CBTS_LOGEND) {
        record_fight_snapshot(event_time);
    }

    /* Any damage from either side keeps the current fight alive.
       The fight ends when no damage has occurred for timeout_seconds. */
    if (is_damage_event(ev)) {
        last_damage_time = event_time;
        last_damage_realtime = std::chrono::steady_clock::now();
    }

    /* Determine whether this event represents an active combat action.
       Used to decide whether a *new* enemy should be added when the
       "only active combat" setting is enabled. */
    bool active = ev->value != 0 || (ev->buff && ev->is_activation == 0 && ev->is_buffremove == 0);

    /* Refresh timeout for already-tracked enemies on any event where they
       appear, even buffs/statechanges. This prevents them from disappearing
       while they are still involved in combat but not dealing direct damage. */
    update_local_team(src);
    update_local_team(dst);

    check_fight_end(event_time);

    if (is_fight_active()) {
        /* Track enemy players on both sides of a combat event. This catches
           enemies that damage our squad (src) as well as enemies our squad is
           fighting (dst). Guards, NPCs and siege weapons are still filtered out
           by is_player_agent because they either have no profession or are flagged
           as NPCs (elite == ELITE_FLAG_NPC). */
        if (is_player_agent(src) && is_enemy_agent(src)) {
            record_enemy(src, event_time, active);
        }
        if (is_player_agent(dst) && is_enemy_agent(dst)) {
            record_enemy(dst, event_time, active);
        }
    }

    cleanup_expired_enemies(event_time);

    return 0;
}

/* Catch F7 key to toggle window visibility. Return uMsg unchanged so other
   addons and the game still receive the key event. */
static const char* vk_to_string(UINT vk) {
    static char buf[32];
    switch (vk) {
        case VK_F1:  return "F1";
        case VK_F2:  return "F2";
        case VK_F3:  return "F3";
        case VK_F4:  return "F4";
        case VK_F5:  return "F5";
        case VK_F6:  return "F6";
        case VK_F7:  return "F7";
        case VK_F8:  return "F8";
        case VK_F9:  return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        case VK_INSERT:   return "Insert";
        case VK_DELETE:   return "Delete";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "Page Up";
        case VK_NEXT:     return "Page Down";
        case VK_TAB:      return "Tab";
        case VK_RETURN:   return "Enter";
        case VK_SPACE:    return "Space";
        case VK_ESCAPE:   return "Escape";
        case VK_BACK:     return "Backspace";
        case VK_OEM_3:    return "`";
        case VK_OEM_MINUS:return "-";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_4:    return "[";
        case VK_OEM_6:    return "]";
        case VK_OEM_5:    return "\\";
        case VK_OEM_1:    return ";";
        case VK_OEM_7:    return "'";
        case VK_OEM_COMMA:return ",";
        case VK_OEM_PERIOD:return ".";
        case VK_OEM_2:    return "/";
        default:
            if (vk >= '0' && vk <= '9') {
                buf[0] = static_cast<char>(vk);
                buf[1] = '\0';
                return buf;
            }
            if (vk >= 'A' && vk <= 'Z') {
                buf[0] = static_cast<char>(vk);
                buf[1] = '\0';
                return buf;
            }
            return "Unknown";
    }
}

static bool vk_is_modifier(UINT vk) {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
           vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static void toggle_windows();

static UINT mod_wnd_nofilter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)hWnd;
    (void)lParam;

    if (waiting_for_hotkey && uMsg == WM_KEYDOWN) {
        UINT vk = static_cast<UINT>(wParam);
        if (!vk_is_modifier(vk)) {
            hotkey_vk = vk;
            waiting_for_hotkey = false;
            hotkey_pressed = false;
        }
        return uMsg;
    }

    if (uMsg == WM_KEYDOWN && wParam == hotkey_vk) {
        if (!hotkey_pressed) {
            hotkey_pressed = true;
            toggle_windows();
        }
    } else if (uMsg == WM_KEYUP && wParam == hotkey_vk) {
        hotkey_pressed = false;
    }

    return uMsg;
}

/* Toggle visibility of the main counter window and the history window
   together, so the hotkey and the arcdps options checkbox open/close both. */
static void toggle_windows() {
    enabled = !enabled;
    show_history_window = enabled;
}

/* arcdps calls this once for every registered window checkbox in its
   Interface -> Extension Windows list. We draw our own checkbox here so
   the window can be toggled on/off from the arcdps options. */
static void mod_options_windows(char* windowname) {
    if (windowname && strcmp(windowname, arc_exports.out_name) == 0) {
        bool visible = enabled;
        if (ImGui::Checkbox("Visible", &visible)) {
            toggle_windows();
        }
    }
}

/* Called when drawing this module's own options tab. Add extended settings
   here (the main on/off checkbox is handled by mod_options_windows). */
static void mod_options_tab() {
    ImGui::Text("Enemy Counter settings");
    ImGui::Checkbox("Show class breakdown", &show_class_list);
    ImGui::Checkbox("Use profession colors", &use_prof_colors);
    ImGui::Checkbox("Only active combat enemies", &active_combat_only);
    ImGui::Checkbox("Show total as big number", &show_total_big);
    ImGui::SliderInt("Timeout (seconds)", &timeout_seconds, 5, 300);
    ImGui::SliderInt("Fight history size", &history_count, 1, 50);
    ImGui::Checkbox("Show fight history window", &show_history_window);
    const char* sorts[] = { "By count", "Alphabetical", "Profession order" };
    if (ImGui::BeginCombo("Sort classes", sorts[sort_mode])) {
        for (int i = 0; i < 3; ++i) {
            bool selected = (sort_mode == i);
            if (ImGui::Selectable(sorts[i], selected)) {
                sort_mode = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    char hotkey_label[64];
    if (waiting_for_hotkey) {
        snprintf(hotkey_label, sizeof(hotkey_label), "Press a key...");
    } else {
        snprintf(hotkey_label, sizeof(hotkey_label), "Toggle hotkey: %s", vk_to_string(hotkey_vk));
    }
    if (ImGui::Button(hotkey_label)) {
        waiting_for_hotkey = !waiting_for_hotkey;
    }
    ImGui::SameLine();
    ImGui::Text("(toggle window visibility)");

    if (ImGui::Button("Reset enemy counter")) {
        record_fight_snapshot(last_event_time);
        std::lock_guard<std::mutex> lock(enemy_mutex);
        enemy_agents.clear();
    }

    ImGui::Separator();
    ImGui::Text("Fight history (last %d)", history_count);
    {
        std::lock_guard<std::mutex> lock(history_mutex);
        if (fight_history.empty()) {
            ImGui::TextDisabled("No fights recorded yet.");
        } else {
            for (size_t i = 0; i < fight_history.size(); ++i) {
                const auto& fight = fight_history[i];
                auto time_t = std::chrono::system_clock::to_time_t(fight.timestamp);
                char time_buf[32];
                ctime_s(time_buf, sizeof(time_buf), &time_t);
                // remove trailing newline from ctime_s
                for (size_t k = 0; k < sizeof(time_buf); ++k) {
                    if (time_buf[k] == '\n') { time_buf[k] = '\0'; break; }
                }
                char header[128];
                snprintf(header, sizeof(header), "Fight %zu: %u enemies at %s", i + 1, fight.total_enemies, time_buf);
                if (ImGui::TreeNode(header)) {
                    for (const auto& c : fight.class_counts) {
                        ImGui::Text("%s: %u", c.first.c_str(), c.second);
                    }
                    ImGui::TreePop();
                }
            }
            if (ImGui::Button("Clear history")) {
                fight_history.clear();
            }
        }
    }
}

static void draw_history_window() {
    if (!show_history_window) return;

    std::vector<FightSnapshot> local_copy;
    {
        std::lock_guard<std::mutex> hlock(history_mutex);
        local_copy = fight_history;
    }

    ImGui::SetNextWindowPos(ImVec2(250, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Enemy Counter History", &show_history_window, wFlags)) {
        if (local_copy.empty()) {
            ImGui::TextDisabled("No fights recorded yet.");
        } else {
            for (size_t i = 0; i < local_copy.size(); ++i) {
                const auto& snap = local_copy[i];
                auto time_t = std::chrono::system_clock::to_time_t(snap.timestamp);
                struct tm tm_buf;
                localtime_s(&tm_buf, &time_t);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

                ImGui::Text("%zu. %s - %u enemies", i + 1, time_str, snap.total_enemies);
                ImGui::Indent();
                for (const auto& c : snap.class_counts) {
                    if (use_prof_colors && prof_colors) {
                        uint32_t prof_idx = profession_name_to_color_index(c.first);
                        ImGui::TextColored(prof_colors[prof_idx], "%s: %u", c.first.c_str(), c.second);
                    } else {
                        ImGui::Text("%s: %u", c.first.c_str(), c.second);
                    }
                }
                ImGui::Unindent();
                ImGui::Separator();
            }
            if (ImGui::Button("Clear history")) {
                std::lock_guard<std::mutex> hlock(history_mutex);
                fight_history.clear();
            }
        }
    }
    ImGui::End();
}

static uintptr_t mod_imgui(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc) {
    (void)hide_if_combat_or_ooc;

    if (!not_charsel_or_loading || !enabled) {
        draw_history_window();
        return 0;
    }

    cleanup_expired_enemies(last_event_time);
    check_fight_end(last_event_time);

    bool fight_active = is_fight_active();

    /* Build class breakdown under lock, then release it before calling ImGui. */
    std::vector<std::pair<std::string, uint32_t>> class_counts;
    uint32_t total = 0;
    {
        std::lock_guard<std::mutex> lock(enemy_mutex);
        if (fight_active) {
            class_counts.reserve(enemy_agents.size());
            for (const auto& pair : enemy_agents) {
                const EnemyInfo& info = pair.second;
                const char* name = get_profession_name(info.profession, info.elite);
                bool found = false;
                for (auto& c : class_counts) {
                    if (c.first == name) {
                        c.second++;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    class_counts.emplace_back(name, 1);
                }
                total++;
            }
        }
    }

    switch (sort_mode) {
        case 1: // alphabetical
            std::sort(class_counts.begin(), class_counts.end(),
                [](const std::pair<std::string, uint32_t>& a, const std::pair<std::string, uint32_t>& b) {
                    return a.first < b.first;
                }
            );
            break;
        case 2: // profession order
            std::sort(class_counts.begin(), class_counts.end(),
                [](const std::pair<std::string, uint32_t>& a, const std::pair<std::string, uint32_t>& b) {
                    return profession_name_to_index(a.first) < profession_name_to_index(b.first);
                }
            );
            break;
        default: // by count
            std::sort(class_counts.begin(), class_counts.end(),
                [](const std::pair<std::string, uint32_t>& a, const std::pair<std::string, uint32_t>& b) {
                    return a.second > b.second;
                }
            );
            break;
    }

    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Enemy Counter", &enabled, wFlags)) {
        if (show_total_big) {
            ImGui::Text("Total enemies");
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%u", total);
            ImGui::Separator();
        } else {
            ImGui::Text("Enemy players: %u", total);
            ImGui::Separator();
        }
        if (!fight_active) {
            ImGui::TextDisabled("No active fight");
        } else if (show_class_list) {
            if (class_counts.empty()) {
                ImGui::TextDisabled("No enemies detected yet.");
            } else {
                for (const auto& c : class_counts) {
                    if (use_prof_colors && prof_colors) {
                        uint32_t prof_idx = profession_name_to_color_index(c.first);
                        ImGui::TextColored(prof_colors[prof_idx], "%s: %u", c.first.c_str(), c.second);
                    } else {
                        ImGui::Text("%s: %u", c.first.c_str(), c.second);
                    }
                }
            }
        }
    }
    ImGui::End();

    draw_history_window();

    return 0;
}

/* ============================================================
 * DLL entry points
 * ============================================================ */

extern "C" __declspec(dllexport) void* get_init_addr(
    char* arcversion,
    ImGuiContext* imguictx,
    void* id3dptr,
    HANDLE arcdll,
    void* mallocfn,
    void* freefn,
    uint32_t d3dversion)
{
    (void)id3dptr;
    (void)d3dversion;

    arcvers = arcversion;

    arccontext_0x510 = reinterpret_cast<const char*(*)()>(GetProcAddress((HMODULE)arcdll, "e1"));
    arclog = reinterpret_cast<void*>(GetProcAddress((HMODULE)arcdll, "e8"));
    arccolors = reinterpret_cast<void(*)(ImVec4**)>(GetProcAddress((HMODULE)arcdll, "e5"));
    if (arccolors) {
        ImVec4* cols[5] = {};
        arccolors(cols);
        prof_colors = cols[1]; // base profession colours
    }

    if (arccontext_0x510) {
        arccontext = arccontext_0x510() - 0x510;
    }

    ImGui::SetCurrentContext(imguictx);
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(mallocfn),
        reinterpret_cast<void(*)(void*, void*)>(freefn)
    );

    return mod_init;
}

extern "C" __declspec(dllexport) void* get_release_addr() {
    return mod_release;
}

static arcdps_exports* mod_init() {
    memset(&arc_exports, 0, sizeof(arcdps_exports));
    arc_exports.size = sizeof(arcdps_exports);
    arc_exports.sig = 0xDEADBEEF;
    arc_exports.imguivers = IMGUI_VERSION_NUM;
    arc_exports.out_name = "Enemy Counter";
    arc_exports.out_build = "1.0.0";
    arc_exports.combat = mod_combat;
    arc_exports.imgui = mod_imgui;
    arc_exports.wnd_nofilter = mod_wnd_nofilter;
    arc_exports.options_tab = mod_options_tab;
    arc_exports.options_windows = mod_options_windows;

    log_arc("enemy_counter: mod_init");
    return &arc_exports;
}

static uintptr_t mod_release() {
    std::lock_guard<std::mutex> lock(enemy_mutex);
    enemy_agents.clear();
    return 0;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
    (void)hModule;
    (void)lpReserved;

    switch (ulReasonForCall) {
        case DLL_PROCESS_ATTACH: break;
        case DLL_PROCESS_DETACH: break;
        case DLL_THREAD_ATTACH:  break;
        case DLL_THREAD_DETACH:  break;
    }
    return TRUE;
}
