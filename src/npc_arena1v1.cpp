/*
 *   Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU AGPL3 v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *   Copyright (C) 2013      Emu-Devstore <http://emu-devstore.com/>
 *
 *   Written by Teiby <http://www.teiby.de/>
 *   Adjusted by fr4z3n for azerothcore
 *   Reworked by XDev
 */

#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "Chat.h"

// TalentTab.dbc -> TalentTabID
/* const uint32 FORBIDDEN_TALENTS_IN_1V1_ARENA[] =
{
    // Healer
    //201, // PriestDiscipline
    //202, // PriestHoly
    //382, // PaladinHoly
    //262, // ShamanRestoration
    //282, // DruidRestoration

    // Tanks
    //383, // PaladinProtection
    //163, // WarriorProtection

    0 // End
};
*/
class arena1v1announce : public PlayerScript
{
public:
    arena1v1announce() : PlayerScript("arena1v1announce") { }

    void OnLogin(Player* pPlayer) override
    {
        if (sConfigMgr->GetBoolDefault("Arena1v1Announcer.Enable", true))
            ChatHandler(pPlayer->GetSession()).SendSysMessage("This server is running the |cff4CFF00Arena 1v1 |rmodule.");
    }
};

class npc_1v1arena : public CreatureScript
{
public:
    npc_1v1arena() : CreatureScript("npc_1v1arena") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player || !creature)
            return true;

        if (sConfigMgr->GetBoolDefault("Arena1v1.Enable", true) == false)
        {
            ChatHandler(player->GetSession()).SendSysMessage("1v1 disabled!");
            return true;
        }

        if (player->InBattlegroundQueueForBattlegroundQueueType(BATTLEGROUND_QUEUE_1v1))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue leave 1v1 Arena", GOSSIP_SENDER_MAIN, 3, "Are you sure?", 0, false);
        else
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena (UnRated)", GOSSIP_SENDER_MAIN, 20);

        if (!player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1v1)))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Create new 1v1 Arena Team", GOSSIP_SENDER_MAIN, 1, "Are you sure?", sConfigMgr->GetIntDefault("Arena1v1Costs", 400000), false);
        else
        {
            if (!player->InBattlegroundQueueForBattlegroundQueueType(BATTLEGROUND_QUEUE_1v1))
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena (Rated)", GOSSIP_SENDER_MAIN, 2);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Arenateam Clear", GOSSIP_SENDER_MAIN, 5, "Are you sure?", 0, false);
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Shows your statistics", GOSSIP_SENDER_MAIN, 4);
        }

        SendGossipMenuFor(player, 68, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (!player || !creature)
            return true;

        ClearGossipMenuFor(player);

        ChatHandler handler(player->GetSession());

        switch (action)
        {
        case 1: // Create new Arenateam
        {
            if (sConfigMgr->GetIntDefault("Arena1v1MinLevel", 80) <= player->getLevel())
            {
                if (player->GetMoney() >= uint32(sConfigMgr->GetIntDefault("Arena1v1Costs", 400000)) && CreateArenateam(player, creature))
                    player->ModifyMoney(sConfigMgr->GetIntDefault("Arena1v1Costs", 400000) * -1);
            }
            else
            {
                handler.PSendSysMessage("You have to be level %u + to create a 1v1 arena team.", sConfigMgr->GetIntDefault("Arena1v1MinLevel", 70));
                CloseGossipMenuFor(player);
                return true;
            }
        }
        break;

        case 2: // Join Queue Arena (rated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, true))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 20: // Join Queue Arena (unrated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, false))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 3: // Leave Queue
        {
            uint8 arenaType = ARENA_TYPE_1v1;

            if (!player->InBattlegroundQueueForBattlegroundQueueType(BATTLEGROUND_QUEUE_1v1))
                return true;

            WorldPacket data;
            data << arenaType << (uint8)0x0 << (uint32)BATTLEGROUND_AA << (uint16)0x0 << (uint8)0x0;
            player->GetSession()->HandleBattleFieldPortOpcode(data);
            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 4: // get statistics
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1v1)));
            if (at)
            {
                std::stringstream s;
                s << "Rating: " << at->GetStats().Rating;
                s << "\nRank: " << at->GetStats().Rank;
                s << "\nSeason Games: " << at->GetStats().SeasonGames;
                s << "\nSeason Wins: " << at->GetStats().SeasonWins;
                s << "\nWeek Games: " << at->GetStats().WeekGames;
                s << "\nWeek Wins: " << at->GetStats().WeekWins;

                ChatHandler(player->GetSession()).PSendSysMessage(SERVER_MSG_STRING, s.str().c_str());
            }
        }
        break;

        case 5: // Disband arenateam
        {
            WorldPacket Data;
            Data << (uint32)player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1v1));
            player->GetSession()->HandleArenaTeamLeaveOpcode(Data);
            handler.SendSysMessage("Arenateam deleted!");
            CloseGossipMenuFor(player);
            return true;
        }
        break;
        }

        OnGossipHello(player, creature);
        return true;
    }

private:
    bool JoinQueueArena(Player* player, Creature* me, bool isRated)
    {
        if (!player || !me)
            return false;

        if (sConfigMgr->GetIntDefault("Arena1v1MinLevel", 80) > player->getLevel())
            return false;
        uint8 arenaslot = ArenaTeam::GetSlotByType(ARENA_TEAM_1v1);
        uint8 arenatype = ARENA_TYPE_1v1;
        uint32 arenaRating = 0;
        uint32 matchmakerRating = 0;

        // ignore if we already in BG or BG queue
        if (player->InBattleground())
            return false;

        //check existance
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        if (!bg)
        {
            sLog->outDebug(LOG_FILTER_NETWORKIO, "Battleground: template bg (all arenas) not found");
            return false;
        }

        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, NULL))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
            return false;
        }

        BattlegroundTypeId bgTypeId = bg->GetBgTypeID();
        BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, arenatype);
        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
        if (!bracketEntry)
            return false;

        // check if already in queue
        if (player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            return false; //player is already in this queue

        // check if has free queue slots
        if (!player->HasFreeBattlegroundQueueId())
            return false;

        uint32 ateamId = 0;

        if (isRated)
        {
            ateamId = player->GetArenaTeamId(arenaslot);
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
            if (!at)
            {
                player->GetSession()->SendNotInArenaTeamPacket(arenatype);
                return false;
            }

            // get the team rating for queueing
            arenaRating = at->GetRating();
            matchmakerRating = arenaRating;
            // the arenateam id must match for everyone in the group

            if (arenaRating <= 0)
                arenaRating = 1;
        }

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        bg->SetRated(isRated);

        GroupQueueInfo* ginfo = bgQueue.AddGroup(player, NULL, bracketEntry, isRated, false, arenaRating, matchmakerRating, ateamId);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        // send status packet (in queue)
        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, TEAM_NEUTRAL);
        player->GetSession()->SendPacket(&data);

        sBattlegroundMgr->ScheduleArenaQueueUpdate(matchmakerRating, bgQueueTypeId, bracketEntry->GetBracketId());

        return true;
    }

    bool CreateArenateam(Player* player, Creature* me)
    {
        if (!player || !me)
            return false;

        uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_1v1);
        if (slot >= MAX_ARENA_SLOT)
            return false;

        // Check if player is already in an arena team
        if (player->GetArenaTeamId(slot))
        {
            player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "You are already in an arena team!", ERR_ALREADY_IN_ARENA_TEAM);
            return false;
        }

        // Teamname = playername
        std::stringstream teamName;
        teamName << player->GetName();

        if (sArenaTeamMgr->GetArenaTeamByName(teamName.str()) != NULL) // teamname exist, so choose another name
        {
            player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "You are already in an arena team! -  But it was not in you arena teams.", ERR_ALREADY_IN_ARENA_TEAM);
            return false;
        }

        // Create arena team
        ArenaTeam* arenaTeam = new ArenaTeam();
        if (!arenaTeam->Create(player->GetGUID(), ARENA_TEAM_1v1, teamName.str(), 4283124816, 45, 4294242303, 5, 4294705149))
        {
            delete arenaTeam;
            return false;
        }

        // Register arena team
        sArenaTeamMgr->AddArenaTeam(arenaTeam);

        ChatHandler(player->GetSession()).SendSysMessage("1v1 Arenateam successfully created!");

        return true;
    }

    bool Arena1v1CheckTalents(Player* player)
    {
        if (!player)
            return false;

        if (sConfigMgr->GetBoolDefault("Arena1v1BlockForbiddenTalents", true) == false)
            return true;

        uint32 count = 0;

        for (uint32 talentId = 0; talentId < sTalentStore.GetNumRows(); ++talentId)
        {
            TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentId);

            if (!talentInfo)
                continue;

            for (int8 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
                if (talentInfo->RankID[rank] == 0)
                    continue;
        }

        if (count >= 36)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You can not join because you have too many talent points in a forbidden tree. (Heal / Tank)");
            return false;
        }

        return true;
    }
};

void AddSC_npc_1v1arena()
{
    new arena1v1announce();
    new npc_1v1arena();
}
