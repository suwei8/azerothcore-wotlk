/*
 * PvP Trainer NPC — CreatureScript for the PvP Practice System
 * + PvP Battle Logger — Records combat events for AI analysis
 * + Teleporter NPC — Teleports players to major cities and common dungeons
 *
 * NPC Entries:
 *   400103 - pvp教练员
 *   400104 - 传送员
 *
 * Provides a Gossip menu to:
 *   1. Select opponent class
 *   2. Select difficulty (Bronze / Platinum / Gladiator)
 *   3. Summon a Playerbot with the chosen settings
 *
 * After a duel ends, a combat log (JSON) is written to /azerothcore/pvp_battle_logs/
 * for external AI analysis by Gemini via the pvp_analyzer bridge.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Chat.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "GossipDef.h"
#include "ScriptedGossip.h"
#include "PlayerbotFactory.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "Spell.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

// ============================================================================
// Constants
// ============================================================================

// Main menu
constexpr uint32 GOSSIP_MENU_MAIN            = 0;

// Difficulty selection senders (high bits encode class)
constexpr uint32 GOSSIP_SENDER_DIFFICULTY    = 100;

// Action IDs for difficulty
constexpr uint32 GOSSIP_ACTION_BRONZE        = 1;
constexpr uint32 GOSSIP_ACTION_PLATINUM      = 2;
constexpr uint32 GOSSIP_ACTION_GLADIATOR     = 3;

// Sender IDs for class selection (1-9 = WoW class IDs)
constexpr uint32 GOSSIP_SENDER_CLASS_BASE    = 200;

// Battle log directory — must be on the shared Docker volume mount
// Inside container: /azerothcore/pvp_battle_logs
// On host: /home/sw/dev_root/azerothcore-wotlk/pvp_battle_logs
static const std::string BATTLE_LOG_DIR = "/azerothcore/pvp_battle_logs";

// ============================================================================
// Class Name Helper
// ============================================================================

static const char* GetClassName(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:      return "战士 (Warrior)";
        case CLASS_PALADIN:      return "圣骑士 (Paladin)";
        case CLASS_HUNTER:       return "猎人 (Hunter)";
        case CLASS_ROGUE:        return "盗贼 (Rogue)";
        case CLASS_PRIEST:       return "牧师 (Priest)";
        case CLASS_DEATH_KNIGHT: return "死亡骑士 (Death Knight)";
        case CLASS_SHAMAN:       return "萨满 (Shaman)";
        case CLASS_MAGE:        return "法师 (Mage)";
        case CLASS_WARLOCK:      return "术士 (Warlock)";
        case CLASS_DRUID:        return "德鲁伊 (Druid)";
        default:                 return "Unknown";
    }
}

static const char* GetClassNameEN(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Unknown";
    }
}

static const char* GetDifficultyName(uint8 diff)
{
    switch (diff)
    {
        case 1: return "Bronze";
        case 2: return "Platinum";
        case 3: return "Gladiator";
        default: return "Unknown";
    }
}

struct TeleportDestination
{
    char const* label;
    char const* teleName;
};

static constexpr uint32 GOSSIP_SENDER_TELEPORT_ROOT       = 500;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_ALLIANCE   = 501;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_HORDE      = 502;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_NEUTRAL    = 503;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_CLASSIC    = 504;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_OUTLAND    = 505;
static constexpr uint32 GOSSIP_SENDER_TELEPORT_NORTHREND  = 506;

static constexpr uint32 GOSSIP_ACTION_TELEPORT_ALLIANCE   = 1;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_HORDE      = 2;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_NEUTRAL    = 3;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_CLASSIC    = 4;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_OUTLAND    = 5;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_NORTHREND  = 6;
static constexpr uint32 GOSSIP_ACTION_TELEPORT_BACK       = 999;

static TeleportDestination const ALLIANCE_CITIES[] =
{
    { "暴风城 (Stormwind)", "Stormwind" },
    { "铁炉堡 (Ironforge)", "Ironforge" },
    { "达纳苏斯 (Darnassus)", "Darnassus" },
    { "埃索达 (The Exodar)", "TheExodar" }
};

static TeleportDestination const HORDE_CITIES[] =
{
    { "奥格瑞玛 (Orgrimmar)", "Orgrimmar" },
    { "幽暗城 (Undercity)", "Undercity" },
    { "雷霆崖 (Thunder Bluff)", "ThunderBluff" },
    { "银月城 (Silvermoon)", "SilvermoonCity" }
};

static TeleportDestination const NEUTRAL_CITIES[] =
{
    { "达拉然 (Dalaran)", "Dalaran" },
    { "沙塔斯 (Shattrath)", "Shattrath" }
};

static TeleportDestination const CLASSIC_DUNGEONS[] =
{
    { "死亡矿井 (Deadmines)", "Deadmines" },
    { "影牙城堡 (Shadowfang Keep)", "ShadowFangKeep" },
    { "血色修道院 (Scarlet Monastery)", "ScarletMonastery" },
    { "斯坦索姆 (Stratholme)", "Stratholme" }
};

static TeleportDestination const OUTLAND_DUNGEONS[] =
{
    { "地狱火城墙 (Hellfire Ramparts)", "HellfireRamparts" },
    { "鲜血熔炉 (The Blood Furnace)", "TheBloodFurnace" },
    { "奴隶围栏 (The Slave Pens)", "TheSlavePens" },
    { "幽暗沼泽 (The Underbog)", "TheUnderbog" },
    { "法力陵墓 (Mana-Tombs)", "ManaTombs" },
    { "塞泰克大厅 (Sethekk Halls)", "SethekkHalls" }
};

static TeleportDestination const NORTHREND_DUNGEONS[] =
{
    { "乌特加德城堡 (Utgarde Keep)", "UtgardeKeep" },
    { "魔枢 (The Nexus)", "TheNexus" },
    { "艾卓-尼鲁布 (Azjol-Nerub)", "AzjolNerub" },
    { "安卡赫特 (Ahn'kahet)", "AhnKahet" },
    { "达克萨隆要塞 (Drak'Tharon Keep)", "DrakTharonKeep" },
    { "古达克 (Gundrak)", "Gundrak" },
    { "闪电大厅 (Halls of Lightning)", "HallsOfLightning" },
    { "紫罗兰监狱 (The Violet Hold)", "TheVioletHold" },
    { "净化斯坦索姆 (Culling of Stratholme)", "TheCullingOfStratholme" }
};

template <typename T, size_t N>
static constexpr size_t ArraySize(T const (&)[N])
{
    return N;
}

template <size_t N>
static void ShowTeleportDestinationMenu(Player* player, Creature* creature, uint32 sender,
    TeleportDestination const (&destinations)[N])
{
    ClearGossipMenuFor(player);

    for (uint32 i = 0; i < N; ++i)
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, destinations[i].label, sender, i);

    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回上一级", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_BACK);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
}

static bool TeleportPlayerToLocation(Player* player, Creature* creature, char const* teleName)
{
    if (!player || !creature || !teleName)
        return false;

    if (player->IsInCombat())
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF0000[传送员]|r 战斗中无法传送。");
        CloseGossipMenuFor(player);
        return true;
    }

    if (player->IsInFlight())
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF0000[传送员]|r 飞行途中无法传送。");
        CloseGossipMenuFor(player);
        return true;
    }

    if (player->isDead())
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF0000[传送员]|r 死亡状态无法传送。");
        CloseGossipMenuFor(player);
        return true;
    }

    GameTele const* tele = sObjectMgr->GetGameTele(teleName, true);
    if (!tele)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF0000[传送员]|r 未找到传送点：{}", teleName);
        CloseGossipMenuFor(player);
        return true;
    }

    CloseGossipMenuFor(player);
    player->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
    return true;
}

// ============================================================================
// PvP Battle Session — Tracks an active PvP Trainer duel
// ============================================================================

struct PvPBattleSession
{
    struct PendingSpellCast
    {
        ObjectGuid casterGuid;
        ObjectGuid targetGuid;
        uint32 spellId = 0;
        std::string spellName;
        std::string targetName;
        float startTime = 0.0f;
        bool successLogged = false;
    };

    ObjectGuid playerGuid;
    ObjectGuid botGuid;
    std::string playerName;
    std::string botName;
    uint8 playerClass;
    uint8 botClass;
    uint8 difficulty;
    std::string scenarioId;
    std::chrono::steady_clock::time_point startTime;
    std::vector<std::string> events; // JSON lines
    std::vector<PendingSpellCast> pendingCasts;
    std::mutex eventMutex;

    uint64 playerDamageDone = 0;
    uint64 botDamageDone = 0;
    uint64 playerHealingDone = 0;
    uint64 botHealingDone = 0;
    uint32 spellCastStartCount = 0;
    uint32 spellCastSuccessCount = 0;
    uint32 auraApplyCount = 0;
    uint32 auraRemoveCount = 0;

    float GetElapsed() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(now - startTime).count();
    }

    void AddEvent(const std::string& jsonLine)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        events.push_back(jsonLine);
    }

    void AddDamage(ObjectGuid sourceGuid, uint32 amount)
    {
        if (sourceGuid == playerGuid)
            playerDamageDone += amount;
        else if (sourceGuid == botGuid)
            botDamageDone += amount;
    }

    void AddHealing(ObjectGuid sourceGuid, uint32 amount)
    {
        if (sourceGuid == playerGuid)
            playerHealingDone += amount;
        else if (sourceGuid == botGuid)
            botHealingDone += amount;
    }

    void RememberSpellCast(ObjectGuid casterGuid, ObjectGuid targetGuid, uint32 spellId,
        std::string const& spellName, std::string const& targetName, float now)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        pendingCasts.erase(std::remove_if(pendingCasts.begin(), pendingCasts.end(),
            [now](PendingSpellCast const& cast)
            {
                return cast.successLogged || (now - cast.startTime) > 15.0f;
            }), pendingCasts.end());

        pendingCasts.push_back({ casterGuid, targetGuid, spellId, spellName, targetName, now, false });
    }

    bool ResolveSpellCastSuccess(ObjectGuid casterGuid, uint32 spellId, float now,
        std::string& spellNameOut, std::string& targetNameOut, float& startTimeOut)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        pendingCasts.erase(std::remove_if(pendingCasts.begin(), pendingCasts.end(),
            [now](PendingSpellCast const& cast)
            {
                return cast.successLogged || (now - cast.startTime) > 15.0f;
            }), pendingCasts.end());

        for (auto it = pendingCasts.rbegin(); it != pendingCasts.rend(); ++it)
        {
            if (it->casterGuid != casterGuid || it->spellId != spellId || it->successLogged)
                continue;

            it->successLogged = true;
            spellNameOut = it->spellName;
            targetNameOut = it->targetName;
            startTimeOut = it->startTime;
            return true;
        }

        return false;
    }
};

// Global map of active sessions (keyed by player GUID low part)
static std::unordered_map<uint32, std::shared_ptr<PvPBattleSession>> sActiveSessions;
static std::mutex sSessionMutex;

static std::string GetSafeSpellName(SpellInfo const* spellInfo)
{
    if (!spellInfo || !spellInfo->SpellName[0])
        return "Unknown";

    return spellInfo->SpellName[0];
}

static bool TryGetCrowdControlType(SpellInfo const* spellInfo, std::string& ccType)
{
    if (!spellInfo)
        return false;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (spellInfo->Effects[i].Mechanic)
        {
            case MECHANIC_STUN:        ccType = "STUN"; return true;
            case MECHANIC_ROOT:        ccType = "ROOT"; return true;
            case MECHANIC_SILENCE:     ccType = "SILENCE"; return true;
            case MECHANIC_FEAR:        ccType = "FEAR"; return true;
            case MECHANIC_POLYMORPH:   ccType = "POLYMORPH"; return true;
            case MECHANIC_FREEZE:      ccType = "FREEZE"; return true;
            case MECHANIC_SAPPED:      ccType = "SAP"; return true;
            case MECHANIC_SLEEP:       ccType = "SLEEP"; return true;
            case MECHANIC_CHARM:       ccType = "CHARM"; return true;
            case MECHANIC_BANISH:      ccType = "BANISH"; return true;
            case MECHANIC_DISORIENTED: ccType = "DISORIENT"; return true;
            case MECHANIC_BLEED:       ccType = "BLEED"; return true;
            case MECHANIC_DISARM:      ccType = "DISARM"; return true;
            default: break;
        }
    }

    switch (spellInfo->Mechanic)
    {
        case MECHANIC_STUN:      ccType = "STUN"; return true;
        case MECHANIC_ROOT:      ccType = "ROOT"; return true;
        case MECHANIC_SILENCE:   ccType = "SILENCE"; return true;
        case MECHANIC_FEAR:      ccType = "FEAR"; return true;
        case MECHANIC_POLYMORPH: ccType = "POLYMORPH"; return true;
        case MECHANIC_FREEZE:    ccType = "FREEZE"; return true;
        case MECHANIC_SAPPED:    ccType = "SAP"; return true;
        case MECHANIC_SLEEP:     ccType = "SLEEP"; return true;
        case MECHANIC_DISARM:    ccType = "DISARM"; return true;
        default: break;
    }

    return false;
}

// Helper: Escape a string for JSON
static std::string JsonEscape(const std::string& s)
{
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s)
    {
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// Helper: Find session by participant GUID
static std::shared_ptr<PvPBattleSession> FindSessionByGuid(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(sSessionMutex);
    uint32 lowGuid = guid.GetCounter();
    // Check direct player key
    auto it = sActiveSessions.find(lowGuid);
    if (it != sActiveSessions.end())
        return it->second;

    // Check if guid is a bot in any session
    for (auto& pair : sActiveSessions)
    {
        if (pair.second->botGuid == guid || pair.second->playerGuid == guid)
            return pair.second;
    }
    return nullptr;
}

static void TryLogResolvedSpellSuccess(std::shared_ptr<PvPBattleSession> const& session,
    Unit* caster, Unit* target, SpellInfo const* spellInfo, char const* confirmSource)
{
    if (!session || !caster || !spellInfo)
        return;

    std::string spellName;
    std::string targetName;
    float startTime = 0.0f;
    float now = session->GetElapsed();

    if (!session->ResolveSpellCastSuccess(caster->GetGUID(), spellInfo->Id, now, spellName, targetName, startTime))
        return;

    ++session->spellCastSuccessCount;

    if (spellName.empty())
        spellName = GetSafeSpellName(spellInfo);
    if (targetName.empty() && target)
        targetName = target->GetName();

    uint32 latencyMs = now >= startTime ? static_cast<uint32>((now - startTime) * 1000.0f) : 0;

    std::ostringstream ss;
    ss << "{\"event\":\"SPELL_CAST_SUCCESS\",\"time\":" << std::fixed << std::setprecision(1) << now
       << ",\"scenario_id\":\"" << session->scenarioId << "\""
       << ",\"source\":\"" << JsonEscape(caster->GetName()) << "\""
       << ",\"target\":\"" << JsonEscape(targetName) << "\""
       << ",\"spell\":\"" << JsonEscape(spellName) << "\""
       << ",\"spell_id\":" << spellInfo->Id
       << ",\"confirm_source\":\"" << confirmSource << "\""
       << ",\"latency_ms\":" << latencyMs
       << "}";
    session->AddEvent(ss.str());
}

// Helper: Register a new session
static void RegisterSession(Player* player, Player* bot, uint8 difficulty)
{
    auto session = std::make_shared<PvPBattleSession>();
    session->playerGuid = player->GetGUID();
    session->botGuid = bot->GetGUID();
    session->playerName = player->GetName();
    session->botName = bot->GetName();
    session->playerClass = player->getClass();
    session->botClass = bot->getClass();
    session->difficulty = difficulty;
    session->startTime = std::chrono::steady_clock::now();

    // Generate scenario_id: player_class_difficulty_timestamp
    auto sysNow = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(sysNow);
    struct tm tmBuf;
    localtime_r(&timeT, &tmBuf);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tmBuf);
    session->scenarioId = std::string(player->GetName()) + "_" + GetClassNameEN(bot->getClass()) + "_" + GetDifficultyName(difficulty) + "_" + timeBuf;

    // Add DUEL_START event
    std::ostringstream ss;
    ss << "{\"event\":\"DUEL_START\",\"time\":0.0"
       << ",\"scenario_id\":\"" << session->scenarioId << "\""
       << ",\"player\":\"" << JsonEscape(player->GetName()) << "\""
       << ",\"player_class\":\"" << GetClassNameEN(player->getClass()) << "\""
       << ",\"opponent\":\"" << JsonEscape(bot->GetName()) << "\""
       << ",\"opponent_class\":\"" << GetClassNameEN(bot->getClass()) << "\""
       << ",\"difficulty\":\"" << GetDifficultyName(difficulty) << "\""
       << ",\"player_hp\":" << player->GetMaxHealth()
       << ",\"opponent_hp\":" << bot->GetMaxHealth()
       << "}";
    session->events.push_back(ss.str());

    std::lock_guard<std::mutex> lock(sSessionMutex);
    sActiveSessions[player->GetGUID().GetCounter()] = session;
}

// Helper: Write session log to file and cleanup
static void FinalizeSession(std::shared_ptr<PvPBattleSession> session,
                             const std::string& winner, const std::string& loser)
{
    float duration = session->GetElapsed();

    // Add DUEL_END event
    std::ostringstream endSs;
    endSs << "{\"event\":\"DUEL_END\",\"time\":" << std::fixed << std::setprecision(1) << duration
          << ",\"scenario_id\":\"" << session->scenarioId << "\""
          << ",\"winner\":\"" << JsonEscape(winner) << "\""
          << ",\"loser\":\"" << JsonEscape(loser) << "\""
          << ",\"duration\":" << std::fixed << std::setprecision(1) << duration
          << "}";
    session->AddEvent(endSs.str());

    bool winnerIsPlayer = winner == session->playerName;

    std::ostringstream summarySs;
    summarySs << "{\"event\":\"DUEL_SUMMARY\",\"time\":" << std::fixed << std::setprecision(1) << duration
              << ",\"scenario_id\":\"" << session->scenarioId << "\""
              << ",\"winner\":\"" << JsonEscape(winner) << "\""
              << ",\"loser\":\"" << JsonEscape(loser) << "\""
              << ",\"winner_is_player\":" << (winnerIsPlayer ? "true" : "false")
              << ",\"duration\":" << std::fixed << std::setprecision(1) << duration
              << ",\"duration_ms\":" << static_cast<uint32>(duration * 1000.0f)
              << ",\"player_total_damage\":" << session->playerDamageDone
              << ",\"bot_total_damage\":" << session->botDamageDone
              << ",\"player_total_healing\":" << session->playerHealingDone
              << ",\"bot_total_healing\":" << session->botHealingDone
              << ",\"spell_cast_start_count\":" << session->spellCastStartCount
              << ",\"spell_cast_success_count\":" << session->spellCastSuccessCount
              << ",\"aura_apply_count\":" << session->auraApplyCount
              << ",\"aura_remove_count\":" << session->auraRemoveCount
              << ",\"pet_interference_count\":0"
              << "}";
    session->AddEvent(summarySs.str());

    // Create log directory
    mkdir(BATTLE_LOG_DIR.c_str(), 0755);

    // Generate filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf;
    localtime_r(&timeT, &tmBuf);
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tmBuf);

    std::string filename = BATTLE_LOG_DIR + "/battle_" + std::string(timeBuf) + "_"
                          + session->playerName + "_vs_" + session->botName + ".jsonl";

    // Write log file
    std::ofstream logFile(filename);
    if (logFile.is_open())
    {
        std::lock_guard<std::mutex> lock(session->eventMutex);
        for (const auto& line : session->events)
        {
            logFile << line << "\n";
        }
        logFile.close();
        LOG_INFO("module", "[PvP教练员] 战斗日志已保存: {}", filename);
    }
    else
    {
        LOG_ERROR("module", "[PvP教练员] 无法写入战斗日志: {}", filename);
    }

    // Write copy to deterministic path for Windows pull
    std::string latestPath = BATTLE_LOG_DIR + "/latest_duel.jsonl";
    std::ifstream src(filename, std::ios::binary);
    std::ofstream dst(latestPath, std::ios::binary);
    if (src.is_open() && dst.is_open())
    {
        dst << src.rdbuf();
        src.close();
        dst.close();
        LOG_INFO("module", "[PvP教练员] 最新决斗日志已复制到确定性路径: {}", latestPath);
    }
    else
    {
        LOG_ERROR("module", "[PvP教练员] 无法复制到确定性路径: {}", latestPath);
    }

    // Remove session
    {
        std::lock_guard<std::mutex> lock(sSessionMutex);
        sActiveSessions.erase(session->playerGuid.GetCounter());
    }
}

// ============================================================================
// PvP Trainer NPC — Gossip CreatureScript
// ============================================================================

// Track pending trainer duels: player GUID -> { botGUID, difficulty }
static std::unordered_map<uint32, std::pair<ObjectGuid, uint8>> sPendingDuels;
static std::mutex sPendingMutex;

class PvPTrainerNPC : public CreatureScript
{
public:
    PvPTrainerNPC() : CreatureScript("pvp_trainer_npc") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        // Show class selection menu
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_Sword_04:20|t 战士 (Warrior)",     GOSSIP_SENDER_CLASS_BASE + CLASS_WARRIOR, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_Hammer_01:20|t 圣骑士 (Paladin)",   GOSSIP_SENDER_CLASS_BASE + CLASS_PALADIN, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_Weapon_Bow_07:20|t 猎人 (Hunter)", GOSSIP_SENDER_CLASS_BASE + CLASS_HUNTER, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_ThrowingKnife_04:20|t 盗贼 (Rogue)", GOSSIP_SENDER_CLASS_BASE + CLASS_ROGUE, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_Staff_30:20|t 牧师 (Priest)",       GOSSIP_SENDER_CLASS_BASE + CLASS_PRIEST, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\Spell_Deathknight_ClassIcon:20|t 死亡骑士 (DK)", GOSSIP_SENDER_CLASS_BASE + CLASS_DEATH_KNIGHT, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\Spell_Nature_BloodLust:20|t 萨满 (Shaman)",   GOSSIP_SENDER_CLASS_BASE + CLASS_SHAMAN, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\INV_Staff_13:20|t 法师 (Mage)",         GOSSIP_SENDER_CLASS_BASE + CLASS_MAGE, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\Spell_Nature_FaerieFire:20|t 术士 (Warlock)", GOSSIP_SENDER_CLASS_BASE + CLASS_WARLOCK, 0);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface\\Icons\\Ability_Druid_Maul:20|t 德鲁伊 (Druid)", GOSSIP_SENDER_CLASS_BASE + CLASS_DRUID, 0);

        SendGossipMenuFor(player, 100100, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);

        // Stage 1: Class was selected, now show difficulty menu
        if (sender >= GOSSIP_SENDER_CLASS_BASE && sender <= GOSSIP_SENDER_CLASS_BASE + CLASS_DRUID)
        {
            uint8 selectedClass = sender - GOSSIP_SENDER_CLASS_BASE;
            uint32 difficultySender = GOSSIP_SENDER_DIFFICULTY + selectedClass;

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|TInterface\\Icons\\Achievement_PVP_A_01:20|t |cff8B4513" + std::string("青铜 (Bronze)") + "|r - 不打断，不解控",
                difficultySender, GOSSIP_ACTION_BRONZE);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|TInterface\\Icons\\Achievement_PVP_A_08:20|t |cffC0C0C0" + std::string("白金 (Platinum)") + "|r - 70%概率打断",
                difficultySender, GOSSIP_ACTION_PLATINUM);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|TInterface\\Icons\\Achievement_Arena_5v5_7:20|t |cffFF8C00" + std::string("角斗士 (Gladiator)") + "|r - 极限打断",
                difficultySender, GOSSIP_ACTION_GLADIATOR);

            SendGossipMenuFor(player, 100101, creature->GetGUID());
            return true;
        }

        // Stage 2: Difficulty was selected, spawn bot and duel
        if (sender >= GOSSIP_SENDER_DIFFICULTY && sender < GOSSIP_SENDER_DIFFICULTY + 20)
        {
            uint8 selectedClass = sender - GOSSIP_SENDER_DIFFICULTY;
            uint8 difficulty = static_cast<uint8>(action); // 1=Bronze, 2=Platinum, 3=Gladiator

            CloseGossipMenuFor(player);

            // Find a random bot of the requested class
            Player* botPlayer = FindRandomBotByClass(player, selectedClass);
            if (!botPlayer)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffFF0000[PvP教练员]|r 未找到该职业的可用陪练Bot，请稍后再试！");
                return true;
            }

            // Set difficulty on the bot
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(botPlayer);
            if (botAI)
            {
                botAI->SetPvPDifficulty(difficulty);

                // Force the bot to become the player's partner and stop random wandering
                botAI->SetMaster(player);
                botPlayer->GetMotionMaster()->Clear(false);
                botPlayer->StopMoving();
                botAI->ResetStrategies(); // Clears out random rpg/grind strategies
                botAI->ChangeStrategy("+duel,+stay", BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy("+duel", BOT_STATE_COMBAT);

                // Force the bot to stay and stop wandering
                botAI->DoSpecificAction("stay");
                // Also force an auto-gear initialization to ensure it has max level gear
                // 4 = Epic Quality, ilvl 277 max equivalent GS
                PlayerbotFactory factory(botPlayer, botPlayer->GetLevel(), 4, PlayerbotFactory::CalcMixedGearScore(277, 4));
                factory.InitEquipment(false, true); // false = not incremental, true = destroy old gear to force upgrade
            }

            // Teleport the bot to the player
            botPlayer->TeleportTo(player->GetMapId(),
                player->GetPositionX() + 3.0f * cos(player->GetOrientation()),
                player->GetPositionY() + 3.0f * sin(player->GetOrientation()),
                player->GetPositionZ(),
                player->GetOrientation() + M_PI);

            // Register pending duel for battle logger
            {
                std::lock_guard<std::mutex> lock(sPendingMutex);
                sPendingDuels[player->GetGUID().GetCounter()] = { botPlayer->GetGUID(), difficulty };
            }

            // Announce settings
            const char* diffName = "Unknown";
            switch (difficulty)
            {
                case PVP_DIFFICULTY_BRONZE:    diffName = "青铜 (Bronze)"; break;
                case PVP_DIFFICULTY_PLATINUM:  diffName = "白金 (Platinum)"; break;
                case PVP_DIFFICULTY_GLADIATOR: diffName = "角斗士 (Gladiator)"; break;
            }

            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00FF00[PvP教练员]|r 已召唤 {} ({}) 作为你的对手！难度：{}",
                botPlayer->GetName(),
                GetClassName(selectedClass),
                diffName);

            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00FF00[PvP教练员]|r 请手动对其发起决斗 (/duel)。祝你好运！");

            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00BFFF[PvP教练员]|r 🤖 AI战斗分析已启用！决斗结束后将自动分析你的表现。");

            return true;
        }

        return false;
    }

private:
    Player* FindRandomBotByClass(Player* requester, uint8 classId)
    {
        // Search through all active random bots using GetAllBots()
        std::vector<Player*> candidates;
        PlayerBotMap allBots = sRandomPlayerbotMgr.GetAllBots();

        for (auto const& pair : allBots)
        {
            Player* bot = pair.second;
            if (!bot || !bot->IsInWorld() || !bot->IsAlive())
                continue;

            if (bot->getClass() != classId)
                continue;

            // Prefer high-level bots for PvP
            if (bot->GetLevel() >= 70)
                candidates.push_back(bot);
        }

        if (candidates.empty())
        {
            // Relax level requirement
            for (auto const& pair : allBots)
            {
                Player* bot = pair.second;
                if (!bot || !bot->IsInWorld() || !bot->IsAlive())
                    continue;

                if (bot->getClass() != classId)
                    continue;

                candidates.push_back(bot);
            }
        }

        if (candidates.empty())
            return nullptr;

        // Pick a random one
        return candidates[urand(0, candidates.size() - 1)];
    }
};

// ============================================================================
// PvP Battle Logger — PlayerScript (Duel lifecycle hooks)
// ============================================================================

class PvPBattleLogger : public PlayerScript
{
public:
    PvPBattleLogger() : PlayerScript("pvp_battle_logger") {}

    void OnPlayerDuelStart(Player* player1, Player* player2) override
    {
        std::lock_guard<std::mutex> lock(sPendingMutex);

        auto it1 = sPendingDuels.find(player1->GetGUID().GetCounter());
        auto it2 = sPendingDuels.find(player2->GetGUID().GetCounter());

        if (it1 != sPendingDuels.end())
        {
            uint8 difficulty = it1->second.second;
            RegisterSession(player1, player2, difficulty);
            sPendingDuels.erase(it1);
            LOG_INFO("module", "[PvP教练员] 战斗记录开始: {} vs {}", player1->GetName(), player2->GetName());
            LOG_INFO("module", "[PvPTrainer] Session started: {} vs {}", player1->GetName(), player2->GetName());
        }
        else if (it2 != sPendingDuels.end())
        {
            uint8 difficulty = it2->second.second;
            RegisterSession(player2, player1, difficulty);
            sPendingDuels.erase(it2);
            LOG_INFO("module", "[PvP教练员] 战斗记录开始: {} vs {}", player2->GetName(), player1->GetName());
            LOG_INFO("module", "[PvPTrainer] Session started: {} vs {}", player2->GetName(), player1->GetName());
        }
    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        auto session = FindSessionByGuid(winner->GetGUID());
        if (!session)
            session = FindSessionByGuid(loser->GetGUID());

        if (!session)
            return;

        std::string winnerName = winner ? winner->GetName() : "Unknown";
        std::string loserName = loser ? loser->GetName() : "Unknown";

        FinalizeSession(session, winnerName, loserName);

        Player* trainerUser = (winner->GetGUID() == session->playerGuid) ? winner : loser;
        if (trainerUser && trainerUser->GetSession())
        {
            ChatHandler(trainerUser->GetSession()).PSendSysMessage(
                "|cff00BFFF[PvP教练员]|r 决斗结束！战斗日志已保存。AI正在分析中...");
            ChatHandler(trainerUser->GetSession()).PSendSysMessage(
                "|cff00BFFF[PvP教练员]|r 分析结果将发送到 Telegram，请耐等约30秒。");
        }

        LOG_INFO("module", "[PvP教练员] 战斗记录完成: {} 胜 {} 负", winnerName, loserName);
        LOG_INFO("module", "[PvPTrainer] Session finished: {} beat {}", winnerName, loserName);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool skipCheck) override
    {
        if (!player || !spell)
            return;

        auto session = FindSessionByGuid(player->GetGUID());
        if (!session)
            return;

        SpellInfo const* spellInfo = spell->GetSpellInfo();
        if (!spellInfo)
            return;

        Unit* target = spell->m_targets.GetUnitTarget();
        std::string spellName = GetSafeSpellName(spellInfo);
        std::string targetName = target ? target->GetName() : "";
        float now = session->GetElapsed();

        session->RememberSpellCast(player->GetGUID(), target ? target->GetGUID() : ObjectGuid(), spellInfo->Id, spellName, targetName, now);
        ++session->spellCastStartCount;

        std::ostringstream ss;
        ss << "{\"event\":\"SPELL_CAST_START\",\"time\":" << std::fixed << std::setprecision(1) << now
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(player->GetName()) << "\""
           << ",\"target\":\"" << JsonEscape(targetName) << "\""
           << ",\"spell\":\"" << JsonEscape(spellName) << "\""
           << ",\"spell_id\":" << spellInfo->Id
           << ",\"cast_time_ms\":" << spell->GetCastTime()
           << ",\"skip_check\":" << (skipCheck ? "true" : "false")
           << "}";
        session->AddEvent(ss.str());

        if (spell->GetCastTime() <= 0)
            TryLogResolvedSpellSuccess(session, player, target, spellInfo, "instant_cast");
    }
};

class PvPCombatLogger : public UnitScript
{
public:
    PvPCombatLogger() : UnitScript("pvp_combat_logger", true,
        { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
          UNITHOOK_MODIFY_HEAL_RECEIVED, UNITHOOK_ON_HEAL,
          UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE }) {}

    // Called for ALL damage events (melee + spell)
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim)
            return;

        Player* attackerPlayer = attacker->ToPlayer();
        Player* victimPlayer = victim->ToPlayer();
        if (!attackerPlayer && !victimPlayer)
            return;

        auto session = FindSessionForUnit(attacker, victim);
        if (!session)
            return;

        session->AddDamage(attacker->GetGUID(), damage);

        std::ostringstream ss;
        ss << "{\"event\":\"DAMAGE\",\"time\":" << std::fixed << std::setprecision(1) << session->GetElapsed()
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(attacker->GetName()) << "\""
           << ",\"target\":\"" << JsonEscape(victim->GetName()) << "\""
           << ",\"damage\":" << damage
           << ",\"target_hp\":" << (victim->GetHealth() > damage ? victim->GetHealth() - damage : 0)
           << ",\"target_max_hp\":" << victim->GetMaxHealth()
           << "}";
        session->AddEvent(ss.str());
    }

    // Called for spell damage specifically (with spell info)
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!attacker || !target || !spellInfo || damage <= 0)
            return;

        auto session = FindSessionForUnit(attacker, target);
        if (!session)
            return;

        TryLogResolvedSpellSuccess(session, attacker, target, spellInfo, "spell_damage");

        std::string spellName = spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "Unknown";

        std::ostringstream ss;
        ss << "{\"event\":\"SPELL_DAMAGE\",\"time\":" << std::fixed << std::setprecision(1) << session->GetElapsed()
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(attacker->GetName()) << "\""
           << ",\"target\":\"" << JsonEscape(target->GetName()) << "\""
           << ",\"spell\":\"" << JsonEscape(spellName) << "\""
           << ",\"spell_id\":" << spellInfo->Id
           << ",\"damage\":" << damage
           << "}";
        session->AddEvent(ss.str());
    }

    // Called when healing occurs
    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        if (!healer || !receiver || gain == 0)
            return;

        auto session = FindSessionForUnit(healer, receiver);
        if (!session)
            return;

        session->AddHealing(healer->GetGUID(), gain);

        std::ostringstream ss;
        ss << "{\"event\":\"HEAL\",\"time\":" << std::fixed << std::setprecision(1) << session->GetElapsed()
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(healer->GetName()) << "\""
           << ",\"target\":\"" << JsonEscape(receiver->GetName()) << "\""
           << ",\"amount\":" << gain
           << ",\"target_hp\":" << receiver->GetHealth()
           << ",\"target_max_hp\":" << receiver->GetMaxHealth()
           << "}";
        session->AddEvent(ss.str());
    }

    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo) override
    {
        if (!target || !healer || !spellInfo || heal == 0)
            return;

        auto session = FindSessionForUnit(healer, target);
        if (!session)
            return;

        TryLogResolvedSpellSuccess(session, healer, target, spellInfo, "heal");
    }

    // Called when an aura (buff/debuff) is applied
    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!unit || !aura)
            return;

        SpellInfo const* spellInfo = aura->GetSpellInfo();
        if (!spellInfo)
            return;

        std::string ccType;
        if (!TryGetCrowdControlType(spellInfo, ccType))
            return;

        auto session = FindSessionForUnit(unit, nullptr);
        if (!session)
            return;

        Unit* caster = aura->GetCaster();
        TryLogResolvedSpellSuccess(session, caster, unit, spellInfo, "aura_apply");
        ++session->auraApplyCount;

        std::string casterName = caster ? caster->GetName() : "Unknown";
        std::string spellName = GetSafeSpellName(spellInfo);
        int32 duration = aura->GetDuration();

        std::ostringstream ss;
        ss << "{\"event\":\"AURA_APPLY\",\"time\":" << std::fixed << std::setprecision(1) << session->GetElapsed()
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(casterName) << "\""
           << ",\"target\":\"" << JsonEscape(unit->GetName()) << "\""
           << ",\"spell\":\"" << JsonEscape(spellName) << "\""
           << ",\"spell_id\":" << spellInfo->Id
           << ",\"type\":\"" << ccType << "\""
           << ",\"duration_ms\":" << duration
           << "}";
        session->AddEvent(ss.str());
    }

    void OnAuraRemove(Unit* unit, AuraApplication* aurApp, AuraRemoveMode mode) override
    {
        if (!unit || !aurApp || !aurApp->GetBase())
            return;

        Aura* aura = aurApp->GetBase();
        SpellInfo const* spellInfo = aura->GetSpellInfo();
        if (!spellInfo)
            return;

        std::string ccType;
        if (!TryGetCrowdControlType(spellInfo, ccType))
            return;

        auto session = FindSessionForUnit(unit, nullptr);
        if (!session)
            return;

        Unit* caster = aura->GetCaster();
        TryLogResolvedSpellSuccess(session, caster, unit, spellInfo, "aura_remove");
        ++session->auraRemoveCount;

        std::string casterName = caster ? caster->GetName() : "Unknown";
        std::string spellName = GetSafeSpellName(spellInfo);

        std::ostringstream ss;
        ss << "{\"event\":\"AURA_REMOVE\",\"time\":" << std::fixed << std::setprecision(1) << session->GetElapsed()
           << ",\"scenario_id\":\"" << session->scenarioId << "\""
           << ",\"source\":\"" << JsonEscape(casterName) << "\""
           << ",\"target\":\"" << JsonEscape(unit->GetName()) << "\""
           << ",\"spell\":\"" << JsonEscape(spellName) << "\""
           << ",\"spell_id\":" << spellInfo->Id
           << ",\"type\":\"" << ccType << "\""
           << ",\"remove_mode\":" << static_cast<uint32>(mode)
           << "}";
        session->AddEvent(ss.str());
    }

private:
    // Find session involving either unit
    std::shared_ptr<PvPBattleSession> FindSessionForUnit(Unit* a, Unit* b)
    {
        if (a)
        {
            auto s = FindSessionByGuid(a->GetGUID());
            if (s) return s;
        }
        if (b)
        {
            auto s = FindSessionByGuid(b->GetGUID());
            if (s) return s;
        }
        return nullptr;
    }
};

class TeleporterNPC : public CreatureScript
{
public:
    TeleporterNPC() : CreatureScript("teleporter_npc") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "联盟主城", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_ALLIANCE);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "部落主城", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_HORDE);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "中立主城", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_NEUTRAL);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "经典旧世副本", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_CLASSIC);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "外域副本", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_OUTLAND);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "诺森德副本", GOSSIP_SENDER_TELEPORT_ROOT, GOSSIP_ACTION_TELEPORT_NORTHREND);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (sender == GOSSIP_SENDER_TELEPORT_ROOT)
        {
            switch (action)
            {
                case GOSSIP_ACTION_TELEPORT_ALLIANCE:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_ALLIANCE, ALLIANCE_CITIES);
                    return true;
                case GOSSIP_ACTION_TELEPORT_HORDE:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_HORDE, HORDE_CITIES);
                    return true;
                case GOSSIP_ACTION_TELEPORT_NEUTRAL:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_NEUTRAL, NEUTRAL_CITIES);
                    return true;
                case GOSSIP_ACTION_TELEPORT_CLASSIC:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_CLASSIC, CLASSIC_DUNGEONS);
                    return true;
                case GOSSIP_ACTION_TELEPORT_OUTLAND:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_OUTLAND, OUTLAND_DUNGEONS);
                    return true;
                case GOSSIP_ACTION_TELEPORT_NORTHREND:
                    ShowTeleportDestinationMenu(player, creature, GOSSIP_SENDER_TELEPORT_NORTHREND, NORTHREND_DUNGEONS);
                    return true;
                case GOSSIP_ACTION_TELEPORT_BACK:
                    return OnGossipHello(player, creature);
                default:
                    break;
            }
        }

        switch (sender)
        {
            case GOSSIP_SENDER_TELEPORT_ALLIANCE:
                if (action < ArraySize(ALLIANCE_CITIES))
                    return TeleportPlayerToLocation(player, creature, ALLIANCE_CITIES[action].teleName);
                break;
            case GOSSIP_SENDER_TELEPORT_HORDE:
                if (action < ArraySize(HORDE_CITIES))
                    return TeleportPlayerToLocation(player, creature, HORDE_CITIES[action].teleName);
                break;
            case GOSSIP_SENDER_TELEPORT_NEUTRAL:
                if (action < ArraySize(NEUTRAL_CITIES))
                    return TeleportPlayerToLocation(player, creature, NEUTRAL_CITIES[action].teleName);
                break;
            case GOSSIP_SENDER_TELEPORT_CLASSIC:
                if (action < ArraySize(CLASSIC_DUNGEONS))
                    return TeleportPlayerToLocation(player, creature, CLASSIC_DUNGEONS[action].teleName);
                break;
            case GOSSIP_SENDER_TELEPORT_OUTLAND:
                if (action < ArraySize(OUTLAND_DUNGEONS))
                    return TeleportPlayerToLocation(player, creature, OUTLAND_DUNGEONS[action].teleName);
                break;
            case GOSSIP_SENDER_TELEPORT_NORTHREND:
                if (action < ArraySize(NORTHREND_DUNGEONS))
                    return TeleportPlayerToLocation(player, creature, NORTHREND_DUNGEONS[action].teleName);
                break;
            default:
                break;
        }

        return false;
    }
};

// ============================================================================
// Script Registration
// ============================================================================

void AddPvPTrainerNPCScripts()
{
    new PvPTrainerNPC();
    new PvPBattleLogger();
    new PvPCombatLogger();
    new TeleporterNPC();
}
