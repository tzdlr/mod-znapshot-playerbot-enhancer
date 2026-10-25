/*
 * This file is part of the Znapshot Playerbot Enhancer module for AzerothCore.
 * It is released under the GNU GPL v2 license; you may redistribute it and/or
 * modify it under the terms of that license.
 */

#include "AiFactory.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Group.h"
#include "GroupScript.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "World.h"
#include "WorldScript.h"
#include "Containers.h"

#include <array>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
constexpr uint32 GROUP_OF_FIVE_SIZE = 5;
constexpr uint32 RAID_OF_TEN_SIZE = 10;
constexpr uint32 RAID_OF_TWENTY_FIVE_SIZE = 25;
constexpr uint32 RAID_OF_FORTY_SIZE = 40;
constexpr size_t ROLE_COUNT = 3;
constexpr size_t TANK_ROLE_INDEX = 0;
constexpr size_t HEALER_ROLE_INDEX = 1;
constexpr size_t DPS_ROLE_INDEX = 2;

constexpr std::array<uint8, 4> TANK_CLASSES = {
    CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_DEATH_KNIGHT};
constexpr std::array<uint8, 4> HEALER_CLASSES = {
    CLASS_PRIEST, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN};
constexpr std::array<uint8, 10> DPS_CLASSES = {CLASS_HUNTER, CLASS_ROGUE, CLASS_MAGE, CLASS_WARLOCK,
                                               CLASS_SHAMAN, CLASS_DEATH_KNIGHT, CLASS_WARRIOR,
                                               CLASS_PALADIN, CLASS_DRUID, CLASS_PRIEST};
constexpr std::array<uint32, ROLE_COUNT> GROUP_OF_FIVE_ROLES = {1, 1, 3};
constexpr std::array<uint32, ROLE_COUNT> RAID_OF_TEN_ROLES = {2, 3, 5};
constexpr std::array<uint32, ROLE_COUNT> RAID_OF_TWENTY_FIVE_ROLES = {3, 5, 17};
constexpr std::array<uint32, ROLE_COUNT> RAID_OF_FORTY_ROLES = {4, 8, 28};
std::mutex trackedBotsMutex;
std::unordered_map<ObjectGuid, ObjectGuid> trackedBots;
std::unordered_set<ObjectGuid> pendingDismissals;
std::atomic<bool> autoDismissOnDisband = false;

void TrackBot(ObjectGuid ownerGuid, ObjectGuid botGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    trackedBots[botGuid] = ownerGuid;
}

uint32 GetTrackedBotCount(ObjectGuid ownerGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    uint32 count = 0;
    for (auto const& [botGuid, trackedOwnerGuid] : trackedBots)
        if (trackedOwnerGuid == ownerGuid)
            ++count;

    return count;
}

uint32 RequestOwnerDismissal(ObjectGuid ownerGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    uint32 count = 0;
    for (auto const& [botGuid, trackedOwnerGuid] : trackedBots)
        if (trackedOwnerGuid == ownerGuid)
        {
            pendingDismissals.insert(botGuid);
            ++count;
        }

    return count;
}

void QueueGroupDismissals(Group* group)
{
    if (!autoDismissOnDisband.load())
        return;

    std::unordered_set<ObjectGuid> members;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        members.insert(slot.guid);

    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    for (auto const& [botGuid, ownerGuid] : trackedBots)
        if (members.contains(botGuid) || members.contains(ownerGuid))
            pendingDismissals.insert(botGuid);
}

void ProcessPendingDismissals()
{
    std::vector<std::pair<ObjectGuid, ObjectGuid>> dismissals;
    {
        std::lock_guard<std::mutex> lock(trackedBotsMutex);
        for (auto itr = pendingDismissals.begin(); itr != pendingDismissals.end();)
        {
            auto botItr = trackedBots.find(*itr);
            if (botItr == trackedBots.end())
                itr = pendingDismissals.erase(itr);
            else
            {
                dismissals.emplace_back(*itr, botItr->second);
                ++itr;
            }
        }
    }

    for (auto const& [botGuid, ownerGuid] : dismissals)
    {
        Player* owner = ObjectAccessor::FindConnectedPlayer(ownerGuid);
        PlayerbotMgr* manager = owner ? GET_PLAYERBOT_MGR(owner) : nullptr;
        Player* bot = manager ? manager->GetPlayerBot(botGuid) : nullptr;
        if (bot)
            manager->LogoutPlayerBot(botGuid);

        bool dismissalComplete = !manager || bot || manager->GetPendingBotLogins() == 0;
        if (dismissalComplete)
        {
            std::lock_guard<std::mutex> lock(trackedBotsMutex);
            trackedBots.erase(botGuid);
            pendingDismissals.erase(botGuid);
        }
    }
}

void CountRole(Player* member, std::array<uint32, ROLE_COUNT>& roleCounts)
{
    BotRoles roles = AiFactory::GetPlayerRoles(member);
    if (roles & BOT_ROLE_TANK)
        ++roleCounts[TANK_ROLE_INDEX];
    else if (roles & BOT_ROLE_HEALER)
        ++roleCounts[HEALER_ROLE_INDEX];
    else
        ++roleCounts[DPS_ROLE_INDEX];
}

ObjectGuid AddClassBotAtLevel(Player* master, PlayerbotMgr* manager, uint8 classId, uint8 targetLevel)
{
    if (!master || !master->GetSession() ||
        (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster()))
        return ObjectGuid::Empty;

    if (classId == CLASS_DEATH_KNIGHT &&
        master->GetLevel() < sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
        return ObjectGuid::Empty;

    if (manager->GetPlayerbotsCount() + manager->GetPendingBotLogins() >=
        uint32(sPlayerbotAIConfig.MaxAddedBots))
        return ObjectGuid::Empty;

    uint8 teamId = master->GetTeamId(true);
    std::unordered_set<ObjectGuid> const& candidates =
        sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(teamId == TEAM_ALLIANCE, classId)];
    std::vector<ObjectGuid> orderedCandidates(candidates.begin(), candidates.end());
    if (sPlayerbotAIConfig.AddClassRandomCharacter)
        Acore::Containers::RandomShuffle(orderedCandidates);

    for (ObjectGuid const& guid : orderedCandidates)
    {
        if (sCharacterCache->GetCharacterLevelByGuid(guid) != targetLevel ||
            ObjectAccessor::FindConnectedPlayer(guid))
            continue;

        uint32 guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
        if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
            continue;

        uint32 pendingBefore = manager->GetPendingBotLogins();
        manager->AddPlayerBot(guid, master->GetSession()->GetAccountId());
        if (manager->GetPendingBotLogins() <= pendingBefore)
            continue;

        return guid;
    }

    return ObjectGuid::Empty;
}

template <size_t Size>
ObjectGuid AddFromClassList(Player* master, PlayerbotMgr* manager, std::array<uint8, Size> const& classes,
                            uint8 targetLevel)
{
    for (uint8 classId : classes)
    {
        ObjectGuid botGuid = AddClassBotAtLevel(master, manager, classId, targetLevel);
        if (!botGuid.IsEmpty())
            return botGuid;
    }

    return ObjectGuid::Empty;
}

uint8 GetAutomaticTargetLevel(Player* master, Group* group)
{
    uint32 totalLevel = 0;
    uint32 memberCount = 0;
    if (group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
            if (!member || sPlayerbotsMgr.GetPlayerbotAI(member))
                continue;

            totalLevel += member->GetLevel();
            ++memberCount;
        }
    }

    if (!memberCount)
        return master->GetLevel();

    return static_cast<uint8>((totalLevel + memberCount / 2) / memberCount);
}

static bool HandleFill(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.Enabled)
    {
        handler->SendErrorMessage("Playerbot system is currently disabled!");
        return false;
    }

    WorldSession* session = handler->GetSession();
    Player* master = session ? session->GetPlayer() : nullptr;
    PlayerbotMgr* manager = master ? GET_PLAYERBOT_MGR(master) : nullptr;
    if (!master || !manager)
    {
        handler->SendErrorMessage("You must be logged in and able to control playerbots.");
        return false;
    }

    if (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster())
    {
        handler->SendErrorMessage("You do not have permission to add class bots.");
        return false;
    }

    uint32 targetSize = GROUP_OF_FIVE_SIZE;
    uint32 requestedLevel = 0;
    uint32 parsedArguments = 0;
    bool invalidArguments = false;
    std::istringstream input(args ? args : "");
    if (input >> targetSize)
    {
        if (input >> requestedLevel)
        {
            parsedArguments = 2;
            std::string extraArgument;
            invalidArguments = static_cast<bool>(input >> extraArgument);
        }
        else if (input.eof())
            parsedArguments = 1;
        else
            invalidArguments = true;
    }
    else if (!input.eof())
        invalidArguments = true;

    if (invalidArguments ||
        (targetSize != GROUP_OF_FIVE_SIZE && targetSize != RAID_OF_TEN_SIZE &&
         targetSize != RAID_OF_TWENTY_FIVE_SIZE && targetSize != RAID_OF_FORTY_SIZE))
    {
        handler->SendErrorMessage("Usage: .zpe fill [5|10|25|40] [level]");
        return false;
    }

    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    if (parsedArguments == 2 && (!requestedLevel || requestedLevel > maxLevel))
    {
        handler->PSendSysMessage("The target level must be between 1 and {}.", maxLevel);
        return false;
    }

    Group* group = master->GetGroup();
    if (group && (group->isLFGGroup() || group->isBGGroup() || group->isBFGroup()))
    {
        handler->SendErrorMessage("Leave the LFG, battleground, or battlefield group first.");
        return false;
    }

    uint32 currentSize = group ? group->GetMembersCount() : 1;
    uint32 pendingBots = manager->GetPendingBotLogins();
    currentSize += pendingBots;
    if (currentSize > targetSize)
    {
        handler->SendErrorMessage("The requested size is smaller than your current group and pending bots.");
        return false;
    }

    std::array<uint32, 3> roleCounts = {0, 0, 0};
    uint8 targetLevel = parsedArguments == 2 ? static_cast<uint8>(requestedLevel)
                                             : GetAutomaticTargetLevel(master, group);
    if (group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
            if (member)
                CountRole(member, roleCounts);
            else
                ++roleCounts[DPS_ROLE_INDEX];
        }
    }
    else
        CountRole(master, roleCounts);
    roleCounts[DPS_ROLE_INDEX] += pendingBots;

    std::array<uint32, ROLE_COUNT> desiredRoles = GROUP_OF_FIVE_ROLES;
    if (targetSize == RAID_OF_TEN_SIZE)
        desiredRoles = RAID_OF_TEN_ROLES;
    else if (targetSize == RAID_OF_TWENTY_FIVE_SIZE)
        desiredRoles = RAID_OF_TWENTY_FIVE_ROLES;
    else if (targetSize == RAID_OF_FORTY_SIZE)
        desiredRoles = RAID_OF_FORTY_ROLES;

    std::array<uint32, ROLE_COUNT> missingRoles = {
        desiredRoles[TANK_ROLE_INDEX] > roleCounts[TANK_ROLE_INDEX]
            ? desiredRoles[TANK_ROLE_INDEX] - roleCounts[TANK_ROLE_INDEX]
            : 0,
        desiredRoles[HEALER_ROLE_INDEX] > roleCounts[HEALER_ROLE_INDEX]
            ? desiredRoles[HEALER_ROLE_INDEX] - roleCounts[HEALER_ROLE_INDEX]
            : 0,
        desiredRoles[DPS_ROLE_INDEX] > roleCounts[DPS_ROLE_INDEX]
            ? desiredRoles[DPS_ROLE_INDEX] - roleCounts[DPS_ROLE_INDEX]
            : 0};
    uint32 added = 0;
    while (currentSize + added < targetSize)
    {
        size_t role = missingRoles[TANK_ROLE_INDEX]
                          ? TANK_ROLE_INDEX
                          : (missingRoles[HEALER_ROLE_INDEX] ? HEALER_ROLE_INDEX : DPS_ROLE_INDEX);
        ObjectGuid botGuid = role == TANK_ROLE_INDEX
                                 ? AddFromClassList(master, manager, TANK_CLASSES, targetLevel)
                                 : role == HEALER_ROLE_INDEX
                                       ? AddFromClassList(master, manager, HEALER_CLASSES, targetLevel)
                                       : AddFromClassList(master, manager, DPS_CLASSES, targetLevel);
        bool usedFallback = false;

        if (botGuid.IsEmpty() && role != DPS_ROLE_INDEX)
        {
            botGuid = AddFromClassList(master, manager, DPS_CLASSES, targetLevel);
            usedFallback = !botGuid.IsEmpty();
        }
        if (botGuid.IsEmpty())
            break;

        TrackBot(master->GetGUID(), botGuid);
        ++added;
        if (usedFallback)
            missingRoles[role] = 0;
        else if (missingRoles[role])
            --missingRoles[role];
    }

    handler->PSendSysMessage("ZPE scheduled {} level-{} bot(s). Group target: {}.", added, targetLevel, targetSize);
    if (added < targetSize - currentSize)
        handler->SendSysMessage("Not enough eligible existing bots are available at that level.");
    handler->SendSysMessage("Role balance is based on class/spec data; tank and healer availability may vary.");
    return true;
}

static bool HandleDismiss(ChatHandler* handler, char const* /*args*/)
{
    WorldSession* session = handler->GetSession();
    Player* master = session ? session->GetPlayer() : nullptr;
    if (!master || !GET_PLAYERBOT_MGR(master))
    {
        handler->SendErrorMessage("You must be logged in and able to control playerbots.");
        return false;
    }

    uint32 requested = RequestOwnerDismissal(master->GetGUID());
    if (!requested)
    {
        handler->SendSysMessage("ZPE has no tracked bots to dismiss.");
        return true;
    }

    handler->PSendSysMessage("ZPE queued dismissal for {} bot(s).", requested);
    handler->SendSysMessage("Still-loading ZPE bots will log out after they finish logging in.");
    return true;
}

static bool HandleStatus(ChatHandler* handler, char const* /*args*/)
{
    WorldSession* session = handler->GetSession();
    Player* master = session ? session->GetPlayer() : nullptr;
    PlayerbotMgr* manager = master ? GET_PLAYERBOT_MGR(master) : nullptr;
    if (!master || !manager)
    {
        handler->SendErrorMessage("You must be logged in and able to control playerbots.");
        return false;
    }

    Group* group = master->GetGroup();
    uint32 groupSize = group ? group->GetMembersCount() : 1;
    uint32 pendingBots = manager->GetPendingBotLogins();
    std::array<uint32, ROLE_COUNT> roleCounts = {0, 0, 0};
    if (group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
            if (member)
                CountRole(member, roleCounts);
            else
                ++roleCounts[DPS_ROLE_INDEX];
        }
    }
    else
        CountRole(master, roleCounts);
    roleCounts[DPS_ROLE_INDEX] += pendingBots;

    handler->PSendSysMessage("Group: {}/{} (tank {}, healer {}, DPS {}).", groupSize + pendingBots,
                             RAID_OF_FORTY_SIZE, roleCounts[TANK_ROLE_INDEX], roleCounts[HEALER_ROLE_INDEX],
                             roleCounts[DPS_ROLE_INDEX]);
    handler->PSendSysMessage("ZPE tracked bots: {}; pending bot logins: {}.",
                             GetTrackedBotCount(master->GetGUID()), pendingBots);
    handler->PSendSysMessage("Auto-dismiss on disband: {}.", autoDismissOnDisband.load() ? "on" : "off");
    return true;
}

class ZnapshotPlayerbotEnhancerGroupScript : public GroupScript
{
public:
    ZnapshotPlayerbotEnhancerGroupScript()
        : GroupScript("ZnapshotPlayerbotEnhancerGroupScript", {GROUPHOOK_ON_DISBAND})
    {
    }

    void OnDisband(Group* group) override
    {
        QueueGroupDismissals(group);
    }
};

class ZnapshotPlayerbotEnhancerWorldScript : public WorldScript
{
public:
    ZnapshotPlayerbotEnhancerWorldScript()
        : WorldScript("ZnapshotPlayerbotEnhancerWorldScript",
                      {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD})
    {
    }

    void OnStartup() override
    {
        LoadConfig();
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadConfig();
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        ProcessPendingDismissals();
    }

private:
    static void LoadConfig()
    {
        autoDismissOnDisband.store(
            sConfigMgr->GetOption<bool>("ZnapshotPlayerbotEnhancer.AutoDismissOnDisband", false));
    }
};
} // namespace

class znapshot_playerbot_enhancer_commandscript : public CommandScript
{
public:
    znapshot_playerbot_enhancer_commandscript() : CommandScript("znapshot_playerbot_enhancer_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable = {
            {"fill", HandleFill, SEC_PLAYER, Console::No},
            {"dismiss", HandleDismiss, SEC_PLAYER, Console::No},
            {"status", HandleStatus, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable rootCommandTable = {
            {"zpe", commandTable},
        };

        return rootCommandTable;
    }
};

void AddSC_znapshot_playerbot_enhancer()
{
    new znapshot_playerbot_enhancer_commandscript();
    new ZnapshotPlayerbotEnhancerGroupScript();
    new ZnapshotPlayerbotEnhancerWorldScript();
}

void Addmod_znapshot_playerbot_enhancerScripts()
{
    AddSC_znapshot_playerbot_enhancer();
}
