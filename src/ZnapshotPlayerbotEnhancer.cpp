/*
 * This file is part of the Znapshot Playerbot Enhancer module for AzerothCore.
 * It is released under the GNU GPL v2 license; you may redistribute it and/or
 * modify it under the terms of that license.
 */

#include "AiFactory.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupScript.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotFactory.h"
#include "PlayerbotMgr.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "RandomPlayerbotFactory.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "World.h"
#include "WorldScript.h"
#include "Containers.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <ctime>
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
constexpr uint32 MAX_CHARACTERS_PER_ACCOUNT = 10;
constexpr uint32 MAX_NEW_CHARACTERS_PER_COMMAND = 10;
constexpr uint32 OFFLINE_GROUP_CHECK_INTERVAL_MS = 30000;
constexpr uint8 MAX_FACTION_CREATION_ATTEMPTS = 3;
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
std::unordered_map<ObjectGuid, uint8> trackedBotLevels;
std::unordered_set<ObjectGuid> pendingBotLogins;
std::unordered_set<ObjectGuid> pendingDismissals;
std::unordered_set<ObjectGuid> generatedBotGuids;
std::unordered_set<ObjectGuid> generatedBotLoginRequested;
std::unordered_map<uint32, uint32> accountCharacterBaselines;
std::unordered_map<uint32, uint32> accountCharacterReservations;
std::atomic<bool> autoDismissOnDisband = false;
std::atomic<uint32> autoDisbandOfflineMasterSeconds = 300;

struct BotCreationContext
{
    uint32 createdCharacters = 0;
    bool addClassAccountsLoaded = false;
    std::vector<uint32> addClassAccounts;
};

void TrackBot(ObjectGuid ownerGuid, ObjectGuid botGuid, uint8 targetLevel)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    trackedBots[botGuid] = ownerGuid;
    trackedBotLevels[botGuid] = targetLevel;
    pendingBotLogins.insert(botGuid);
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

uint32 GetPendingTrackedBotCount(ObjectGuid ownerGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    uint32 count = 0;
    for (ObjectGuid const& botGuid : pendingBotLogins)
    {
        auto itr = trackedBots.find(botGuid);
        if (itr != trackedBots.end() && itr->second == ownerGuid)
            ++count;
    }

    return count;
}

bool IsTrackedBot(ObjectGuid botGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    return trackedBots.contains(botGuid);
}

bool IsPendingBotLogin(ObjectGuid botGuid)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    return pendingBotLogins.contains(botGuid);
}

uint32 GetAccountCharacterCount(uint32 accountId)
{
    uint32 databaseCount = AccountMgr::GetCharactersCount(accountId);
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    auto baselineItr = accountCharacterBaselines.try_emplace(accountId, databaseCount).first;
    uint32 reservedCount = accountCharacterReservations[accountId];
    return std::max(databaseCount, baselineItr->second + reservedCount);
}

void ReserveAccountCharacterSlot(uint32 accountId)
{
    std::lock_guard<std::mutex> lock(trackedBotsMutex);
    ++accountCharacterReservations[accountId];
}

void LoadAddClassAccounts(BotCreationContext& context)
{
    if (context.addClassAccountsLoaded)
        return;

    context.addClassAccountsLoaded = true;
    QueryResult accounts = LoginDatabase.Query("SELECT id FROM account WHERE username LIKE '{}%%' ORDER BY id",
                                                sPlayerbotAIConfig.RandomBotAccountPrefix.c_str());
    if (!accounts)
        return;

    do
    {
        uint32 accountId = accounts->Fetch()[0].Get<uint32>();
        if (sRandomPlayerbotMgr.IsAddClassAccount(accountId))
            context.addClassAccounts.push_back(accountId);
    } while (accounts->NextRow());
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
    struct PendingLogin
    {
        ObjectGuid botGuid;
        ObjectGuid ownerGuid;
        uint8 targetLevel;
        bool generated;
        bool loginRequested;
    };

    std::vector<PendingLogin> pendingLogins;
    {
        std::lock_guard<std::mutex> lock(trackedBotsMutex);
        for (ObjectGuid const& botGuid : pendingBotLogins)
        {
            auto ownerItr = trackedBots.find(botGuid);
            auto levelItr = trackedBotLevels.find(botGuid);
            if (ownerItr != trackedBots.end() && levelItr != trackedBotLevels.end())
                pendingLogins.push_back({botGuid, ownerItr->second, levelItr->second,
                                         generatedBotGuids.contains(botGuid),
                                         generatedBotLoginRequested.contains(botGuid)});
        }
    }

    for (PendingLogin const& pendingLogin : pendingLogins)
    {
        Player* owner = ObjectAccessor::FindConnectedPlayer(pendingLogin.ownerGuid);
        PlayerbotMgr* manager = owner ? GET_PLAYERBOT_MGR(owner) : nullptr;
        if (pendingLogin.generated && !pendingLogin.loginRequested && manager && owner->GetSession() &&
            !CharacterDatabase.QueueSize())
        {
            manager->AddPlayerBot(pendingLogin.botGuid, owner->GetSession()->GetAccountId());
            std::lock_guard<std::mutex> lock(trackedBotsMutex);
            generatedBotLoginRequested.insert(pendingLogin.botGuid);
            continue;
        }

        Player* bot = manager ? manager->GetPlayerBot(pendingLogin.botGuid) : nullptr;
        if (bot)
        {
            if (pendingLogin.generated || bot->GetLevel() != pendingLogin.targetLevel)
            {
                PlayerbotFactory factory(bot, pendingLogin.targetLevel);
                factory.Randomize(false);
                bot->SaveToDB(false, false);
            }

            std::lock_guard<std::mutex> lock(trackedBotsMutex);
            pendingBotLogins.erase(pendingLogin.botGuid);
            generatedBotGuids.erase(pendingLogin.botGuid);
            generatedBotLoginRequested.erase(pendingLogin.botGuid);
        }
    }

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

        Group* group = bot ? bot->GetGroup() : (owner ? owner->GetGroup() : nullptr);
        if (group && group->IsMember(botGuid))
        {
            PlayerbotAI* botAI = bot ? sPlayerbotsMgr.GetPlayerbotAI(bot) : nullptr;
            if (sPlayerbotAIConfig.TellMasterInGroupChat && owner && botAI && botAI->GetMaster() == owner)
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "goodbye", "Goodbye!", {}), PLAYERBOT_SECURITY_TALK);

            group->RemoveMember(botGuid);
        }

        if (bot)
            manager->LogoutPlayerBot(botGuid);

        if (!manager || bot || !IsPendingBotLogin(botGuid))
        {
            std::lock_guard<std::mutex> lock(trackedBotsMutex);
            trackedBots.erase(botGuid);
            trackedBotLevels.erase(botGuid);
            pendingBotLogins.erase(botGuid);
            pendingDismissals.erase(botGuid);
            generatedBotGuids.erase(botGuid);
            generatedBotLoginRequested.erase(botGuid);
        }
    }
}

void DisbandExpiredOfflineMasterGroups()
{
    uint32 timeoutSeconds = autoDisbandOfflineMasterSeconds.load();
    if (!timeoutSeconds)
        return;

    QueryResult result = CharacterDatabase.Query(
        "SELECT gm.guid, gm.memberGuid, c.logout_time "
        "FROM group_member gm INNER JOIN characters c ON c.guid = gm.memberGuid "
        "WHERE c.online = 0 AND c.logout_time > 0");
    if (!result)
        return;

    time_t now = std::time(nullptr);
    std::unordered_set<uint32> processedGroups;
    do
    {
        Field* fields = result->Fetch();
        uint32 groupId = fields[0].Get<uint32>();
        ObjectGuid masterGuid = ObjectGuid::Create<HighGuid::Player>(fields[1].Get<uint32>());
        uint32 logoutTime = fields[2].Get<uint32>();
        if (static_cast<uint64>(now) < logoutTime || uint64(now - logoutTime) < timeoutSeconds ||
            ObjectAccessor::FindConnectedPlayer(masterGuid))
            continue;

        Group* group = sGroupMgr->GetGroupByGUID(groupId);
        if (!group || group->isLFGGroup() || group->isBGGroup() || group->isBFGroup() ||
            !group->IsMember(masterGuid))
            continue;

        bool hasAddClassBot = false;
        uint32 nonBotMembers = 0;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(slot.guid);
            if (sRandomPlayerbotMgr.IsAddClassAccount(accountId))
                hasAddClassBot = true;
            else
            {
                ++nonBotMembers;
                if (slot.guid != masterGuid)
                {
                    nonBotMembers = 2;
                    break;
                }
            }
        }

        if (!hasAddClassBot || nonBotMembers != 1 || !processedGroups.insert(groupId).second)
            continue;

        LOG_INFO("playerbots", "ZPBE disbanding group {} after master {} was offline for {} seconds",
                 groupId, masterGuid.ToString(), now - logoutTime);
        group->Disband();
    } while (result->NextRow());
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

ObjectGuid AddClassBotAtLevel(Player* master, PlayerbotMgr* manager, uint8 classId, uint8 targetLevel,
                              bool allowLevelChange = false)
{
    if (!master || !master->GetSession() ||
        (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster()))
        return ObjectGuid::Empty;

    if (classId == CLASS_DEATH_KNIGHT &&
        master->GetLevel() < sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
        return ObjectGuid::Empty;

    if (manager->GetPlayerbotsCount() + GetPendingTrackedBotCount(master->GetGUID()) >=
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
        if ((!allowLevelChange && sCharacterCache->GetCharacterLevelByGuid(guid) != targetLevel) ||
            ObjectAccessor::FindConnectedPlayer(guid) || IsTrackedBot(guid))
            continue;

        uint32 guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
        if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
            continue;

        manager->AddPlayerBot(guid, master->GetSession()->GetAccountId());
        return guid;
    }

    return ObjectGuid::Empty;
}

ObjectGuid CreateClassBotAtLevel(Player* master, uint8 classId, uint8 targetLevel, BotCreationContext& context)
{
    if (!master || context.createdCharacters >= MAX_NEW_CHARACTERS_PER_COMMAND ||
        !sChrClassesStore.LookupEntry(classId) ||
        ((1 << (classId - 1)) & sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK)) ||
        (classId == CLASS_DEATH_KNIGHT &&
         targetLevel < uint32(sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))))
        return ObjectGuid::Empty;

    LoadAddClassAccounts(context);
    std::vector<uint32> accounts = context.addClassAccounts;
    if (accounts.empty())
        return ObjectGuid::Empty;

    Acore::Containers::RandomShuffle(accounts);
    for (uint8 attempt = 0; attempt < MAX_FACTION_CREATION_ATTEMPTS; ++attempt)
    {
        for (uint32 accountId : accounts)
        {
            if (GetAccountCharacterCount(accountId) >= MAX_CHARACTERS_PER_ACCOUNT)
                continue;

            WorldSession botSession(accountId, std::string{}, 0, nullptr, SEC_PLAYER,
                                    EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), sWorld->GetDefaultDbcLocale(),
                                    0, false, false, 0);
            RandomPlayerbotFactory randomFactory;
            std::unordered_map<RandomPlayerbotFactory::NameRaceAndGender, std::vector<std::string>> nameCache;
            Player* bot = randomFactory.CreateRandomBot(&botSession, classId, nameCache);
            if (!bot)
                continue;

            TeamId const botTeam = bot->GetTeamId(true);
            uint8 const generatedClass = bot->getClass();
            uint8 const gender = bot->getGender();
            uint8 const race = bot->getRace();
            ObjectGuid const botGuid = bot->GetGUID();
            std::string const name = bot->GetName();
            bot->GiveLevel(targetLevel);
            bot->SetUInt32Value(PLAYER_XP, 0);
            bot->SaveToDB(true, false);
            sCharacterCache->AddCharacterCacheEntry(botGuid, accountId, name, gender, race,
                                                    generatedClass, targetLevel);
            sRandomPlayerbotMgr.addclassCache[
                RandomPlayerbotMgr::GetTeamClassIdx(botTeam == TEAM_ALLIANCE, classId)]
                .insert(botGuid);

            bot->CleanupsBeforeDelete();
            delete bot;
            ReserveAccountCharacterSlot(accountId);
            ++context.createdCharacters;

            if (botTeam == master->GetTeamId(true))
            {
                std::lock_guard<std::mutex> lock(trackedBotsMutex);
                generatedBotGuids.insert(botGuid);
                return botGuid;
            }

            break;
        }
    }

    return ObjectGuid::Empty;
}

template <size_t Size>
ObjectGuid AddFromClassList(Player* master, PlayerbotMgr* manager, std::array<uint8, Size> const& classes,
                            uint8 targetLevel, bool createMissing, BotCreationContext& creationContext)
{
    if (manager->GetPlayerbotsCount() + GetPendingTrackedBotCount(master->GetGUID()) >=
        uint32(sPlayerbotAIConfig.MaxAddedBots))
        return ObjectGuid::Empty;

    for (uint8 classId : classes)
    {
        ObjectGuid botGuid = AddClassBotAtLevel(master, manager, classId, targetLevel);
        if (!botGuid.IsEmpty())
            return botGuid;
    }

    if (createMissing)
    {
        for (uint8 classId : classes)
        {
            ObjectGuid botGuid = AddClassBotAtLevel(master, manager, classId, targetLevel, true);
            if (!botGuid.IsEmpty())
                return botGuid;
        }

        for (uint8 classId : classes)
        {
            ObjectGuid botGuid = CreateClassBotAtLevel(master, classId, targetLevel, creationContext);
            if (!botGuid.IsEmpty())
            {
                manager->AddPlayerBot(botGuid, master->GetSession()->GetAccountId());
                return botGuid;
            }
        }
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

static bool HandleFillInternal(ChatHandler* handler, char const* args, bool createMissing)
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
        handler->SendErrorMessage(createMissing ? "Usage: .zpbe populate [5|10|25|40] [level]"
                                                : "Usage: .zpbe fill [5|10|25|40] [level]");
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
    uint32 pendingBots = GetPendingTrackedBotCount(master->GetGUID());
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
    BotCreationContext creationContext;
    while (currentSize + added < targetSize)
    {
        size_t role = missingRoles[TANK_ROLE_INDEX]
                          ? TANK_ROLE_INDEX
                          : (missingRoles[HEALER_ROLE_INDEX] ? HEALER_ROLE_INDEX : DPS_ROLE_INDEX);
        ObjectGuid botGuid = role == TANK_ROLE_INDEX
                                 ? AddFromClassList(master, manager, TANK_CLASSES, targetLevel, createMissing,
                                                    creationContext)
                                 : role == HEALER_ROLE_INDEX
                                       ? AddFromClassList(master, manager, HEALER_CLASSES, targetLevel, createMissing,
                                                          creationContext)
                                       : AddFromClassList(master, manager, DPS_CLASSES, targetLevel, createMissing,
                                                          creationContext);
        bool usedFallback = false;

        if (botGuid.IsEmpty() && role != DPS_ROLE_INDEX)
        {
            botGuid = AddFromClassList(master, manager, DPS_CLASSES, targetLevel, createMissing, creationContext);
            usedFallback = !botGuid.IsEmpty();
        }
        if (botGuid.IsEmpty())
            break;

        TrackBot(master->GetGUID(), botGuid, targetLevel);
        ++added;
        if (usedFallback)
            missingRoles[role] = 0;
        else if (missingRoles[role])
            --missingRoles[role];
    }

    handler->PSendSysMessage("ZPBE scheduled {} level-{} bot(s). Group target: {}.", added, targetLevel,
                             targetSize);
    if (creationContext.createdCharacters)
        handler->PSendSysMessage("ZPBE created {} character(s) in existing AddClass accounts.",
                                 creationContext.createdCharacters);
    if (added < targetSize - currentSize)
        handler->SendSysMessage(createMissing
                                     ? "Could not fill the group: no more matching bots or free AddClass character slots are available."
                                     : "Not enough eligible existing bots are available at that level.");
    handler->SendSysMessage("Role balance is based on class/spec data; tank and healer availability may vary.");
    return true;
}

static bool HandleFill(ChatHandler* handler, char const* args)
{
    return HandleFillInternal(handler, args, false);
}

static bool HandlePopulate(ChatHandler* handler, char const* args)
{
    return HandleFillInternal(handler, args, true);
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
        handler->SendSysMessage("ZPBE has no tracked bots to dismiss.");
        return true;
    }

    handler->PSendSysMessage("ZPBE queued dismissal for {} bot(s).", requested);
    handler->SendSysMessage("Still-loading ZPBE bots will log out after they finish logging in.");
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
    uint32 pendingBots = GetPendingTrackedBotCount(master->GetGUID());
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
    handler->PSendSysMessage("ZPBE tracked bots: {}; pending bot logins: {}.",
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

    void OnUpdate(uint32 diff) override
    {
        ProcessPendingDismissals();
        if (_offlineGroupCheckTimerMs > 0)
        {
            _offlineGroupCheckTimerMs -= std::min(_offlineGroupCheckTimerMs, diff);
            if (_offlineGroupCheckTimerMs > 0)
                return;
        }

        _offlineGroupCheckTimerMs = OFFLINE_GROUP_CHECK_INTERVAL_MS;
        DisbandExpiredOfflineMasterGroups();
    }

private:
    uint32 _offlineGroupCheckTimerMs = OFFLINE_GROUP_CHECK_INTERVAL_MS;

    static void LoadConfig()
    {
        autoDismissOnDisband.store(
            sConfigMgr->GetOption<bool>("ZnapshotPlayerbotEnhancer.AutoDismissOnDisband", false));
        autoDisbandOfflineMasterSeconds.store(
            sConfigMgr->GetOption<uint32>("ZnapshotPlayerbotEnhancer.AutoDisbandOfflineMasterSeconds", 300));
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
            {"populate", HandlePopulate, SEC_PLAYER, Console::No},
            {"dismiss", HandleDismiss, SEC_PLAYER, Console::No},
            {"status", HandleStatus, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable rootCommandTable = {
            {"zpbe", commandTable},
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
