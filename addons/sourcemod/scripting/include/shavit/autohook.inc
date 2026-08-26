#if defined _shavit_autohook_included
    #endinput
#endif
#define _shavit_autohook_included


#define MAX_DISTANCE 1024.0 // Max distance from trigger tele destination can be.

#include <regex>

static char sEntityNames[][MAX_NAME_LENGTH] = {
	"trigger_multiple",
	"func_button",
    "func_rot_button"
};

enum AutohookStatus
{
    AutohookStatus_Success,
    AutohookStatus_Partial,
    AutohookStatus_NoZones,
    AutohookStatus_MaxNumberOfZones,
}

enum struct TeleportDestination {
    int     iEntity;
    float   fOrigin[3];
}

// start_zone - main surf start
// bonus1_start - bonus X start

// end_zone - main surf end
// bonus1_end - bonus X end

// checkpoint_x - surf checkpoint
// checkpoints do not show up in staged maps

// staged maps will typically follow these rules:
// all stages before the last stage have start zones only.
// the last stage is the only one to have an end zone
// bonuses have start and endzones only.

// Some staged maps have per stage start and end zones.
// How do we account for this?
zone_cache_t unhookedZoneCache[MAX_ZONES];
int numZonesCached = 0;
int numZonesFound = 0;
bool areStagesSeparateTracks = false;

void Autohook_EmptyUnhookedZoneCache() {
    numZonesCached = 0;
    for (int zone = 0; zone < MAX_ZONES; zone++) {
        unhookedZoneCache[zone].fCorner1[0] = 0.0;
        unhookedZoneCache[zone].fCorner1[1] = 0.0;
        unhookedZoneCache[zone].fCorner1[2] = 0.0;
        unhookedZoneCache[zone].fCorner2[0] = 0.0;
        unhookedZoneCache[zone].fCorner2[1] = 0.0;
        unhookedZoneCache[zone].fCorner2[2] = 0.0;
        unhookedZoneCache[zone].fDestination[0] = 0.0;
        unhookedZoneCache[zone].fDestination[1] = 0.0;
        unhookedZoneCache[zone].fDestination[2] = 0.0;
        unhookedZoneCache[zone].iData = 0;
        unhookedZoneCache[zone].iDatabaseID = -1;
        unhookedZoneCache[zone].iEntity = -1;
        unhookedZoneCache[zone].iFlags = 0;
        unhookedZoneCache[zone].iForm = -1;
        unhookedZoneCache[zone].iTrack = -1;
        unhookedZoneCache[zone].iType = -1;
        unhookedZoneCache[zone].sSource = "";
        unhookedZoneCache[zone].sTarget = "";
    }
}

void Autohook_ResetAutohook() {
    Shavit_LogMessage("Cleaning up...");
    Autohook_EmptyUnhookedZoneCache();
}

void Autohook_FillUnhookedZoneCache(AutohookStatus &result) {
    if (gI_MapZones >= MAX_ZONES) {
        Shavit_LogMessage("Too many map zones (%d/%d)! Cannot hook any more zones!", gI_MapZones, MAX_ZONES);
        result = AutohookStatus_MaxNumberOfZones;
        return;
    }

    int ent = 0;
    bool foundMatch = false;
    for (int entType = 0; entType < 3; entType++) {
        Shavit_LogMessage("Parsing %ss...", sEntityNames[entType]);

        while ((ent = FindEntityByClassname(ent, sEntityNames[entType])) != -1) {
            if (gI_MapZones >= MAX_ZONES) {
                Shavit_LogMessage("Too many map zones (%d/%d)! Cannot hook any more zones!", gI_MapZones, MAX_ZONES);
                result = AutohookStatus_MaxNumberOfZones;
                break;
            }
            char entityName[MAX_NAME_LENGTH];
            GetEntPropString(ent, Prop_Data, "m_iName", entityName, MAX_NAME_LENGTH);

            if (IsValidEdict(ent)) {
                if (StrContains(entityName, "shavit_zones_") != -1) {
                    continue;
                }

                if (!Autohook_IsZoneEntity(entityName)) {
                    continue;
                }

                numZonesFound++;
                if (gI_MapZones > 0) {
                    for (int i = 0; i < gI_MapZones; i++) {
                        if (Autohook_DoesEntityMatchZone(ent, i)) {
                            foundMatch = true;
                            Shavit_LogMessage("%s already exists with Zone ID (%d)", sEntityNames[entType], gA_ZoneCache[i].iDatabaseID);
                            break;
                        }
                    }

                    if (foundMatch) {
                        foundMatch = false;
                        continue;
                    }
                }
                

                unhookedZoneCache[numZonesCached].iDatabaseID       = -1;
                unhookedZoneCache[numZonesCached].iEntity           = ent;
                unhookedZoneCache[numZonesCached].sSource           = StrContains(sEntityNames[entType], "button") != -1 ? "autobutton" : "autozone";
                unhookedZoneCache[numZonesCached].iForm             = Autohook_GetZoneFormFromClassname(sEntityNames[entType]);
                unhookedZoneCache[numZonesCached].iSpeedLimitFlags |= (StrContains(entityName, "_TH", false) != -1 || StrContains(entityName, " TH") != -1, false) ? ZSLF_TeleHop : DEFAULT_SPEEDLIMITFLAG;
                unhookedZoneCache[numZonesCached].iFlags           |= ZF_FilterIgnore;

                GetMinsFromEntity(ent, unhookedZoneCache[numZonesCached].fCorner1);
                GetMaxsFromEntity(ent, unhookedZoneCache[numZonesCached].fCorner2);

                int hammerID = GetEntProp(ent, Prop_Data, "m_iHammerID");
                IntToString(hammerID, unhookedZoneCache[numZonesCached].sTarget, sizeof(unhookedZoneCache[numZonesCached].sTarget));
                unhookedZoneCache[numZonesCached].iFlags |= ZF_Hammerid;

                float origin[3];
                GetEntPropVector(ent, Prop_Send, "m_vecOrigin", origin);
                origin[1] += 2.0; // offset the origin to prevent the player from being stuck in the ground.

                // This is a temporary variable until we can find the teleports for the zones
                Autohook_CopyVector(origin, unhookedZoneCache[numZonesCached].fDestination);
                numZonesCached++;
            }
        }
    }

    if (numZonesCached > 0) {
        Shavit_LogMessage("Found %d unhooked zones.", numZonesCached);
        Autohook_DecideStageZoningStyle(unhookedZoneCache, numZonesCached);
    } else {
        Shavit_LogMessage("No unhooked zones found!");
        result = AutohookStatus_NoZones;
    }
}

bool Autohook_DoesEntityMatchZone(int entity, int zoneIndex) {
    // there's gotta be a better way to do this lol
    // lets see what we should match:
    //     - entity index might not always match? I'm not sure.
    //     - source doesn't really matter, you can make custom zones.
    //     - zone form will be the same.
    //     - mins and maxes will be the same.
    //     - sTarget - Can be hammerid or the actual name of the trigger.
    char entName[MAX_NAME_LENGTH], entClassname[MAX_NAME_LENGTH];
    char entHammerId[64];
    int tempHammerId;
    float mins[3], maxs[3];
    
    GetMinsFromEntity(entity, mins);
    GetMaxsFromEntity(entity, maxs);
    GetEntPropString(entity, Prop_Data, "m_iClassname", entClassname, sizeof(entClassname));
    GetEntPropString(entity, Prop_Data, "m_iName", entName, sizeof(entName));
    tempHammerId = GetEntProp(entity, Prop_Data, "m_iHammerID");
    IntToString(tempHammerId, entHammerId, sizeof(entHammerId));

    return (Autohook_AreVectorsEqual(mins, gA_ZoneCache[zoneIndex].fCorner1) || Autohook_AreVectorsEqual(mins, gA_ZoneCache[zoneIndex].fCorner2)) &&
           (Autohook_AreVectorsEqual(maxs, gA_ZoneCache[zoneIndex].fCorner1) || Autohook_AreVectorsEqual(maxs, gA_ZoneCache[zoneIndex].fCorner2)) &&
           (StrEqual(entHammerId, gA_ZoneCache[zoneIndex].sTarget) || StrEqual(entName, gA_ZoneCache[zoneIndex].sTarget)) &&
           (Autohook_GetZoneFormFromClassname(entClassname) == gA_ZoneCache[zoneIndex].iForm);
}

void Autohook_DecideStageZoningStyle(const zone_cache_t[] zoneCache, const int zonesInCache) {
    Shavit_LogMessage("Figuring out how map stages are zoned. (Are zones telehops?)");

    for (int zone = 0; zone < zonesInCache; zone++) {
        char entityName[MAX_NAME_LENGTH];
        GetEntPropString(zoneCache[zone].iEntity, Prop_Data, "m_iName", entityName, sizeof(entityName));

        if (StrContains(entityName, "stage1_end") != -1) {
            Shavit_LogMessage("End zone for stage one exists, assuming stages are on separate tracks");
            areStagesSeparateTracks = true;
            break;
        }
    }

    if (!areStagesSeparateTracks) {
        Shavit_LogMessage("No end zone for stage one found, assuming stages are on Track_Main");
    }
}

int Autohook_GetZoneTypeFromString(const char[] zoneName) {
    int zoneType = -1;
    
    if (StrEqual(zoneName, "stage", false)) {
        zoneType = Zone_Stage;
    }

    else if (StrEqual(zoneName, "start", false)) {
        zoneType = Zone_Start;
    }

    else if (StrEqual(zoneName, "end", false)) {
        zoneType = Zone_End;
    }

    else if (StrEqual(zoneName, "checkpoint", false)) {
        zoneType = Zone_Checkpoint;
    }

    return zoneType;
}

void Autohook_GetTeleportDestinations(int caller) {
    Shavit_LogMessage("Searching for zone teleports...");
    TeleportDestination destinationEntities[MAX_STAGES*2+1];
    int entitiesFound = 0;

    int ent = 0;
    while ((ent = FindEntityByClassname(ent, "info_teleport_destination")) != -1) {
        if (IsValidEdict(ent)) {
            destinationEntities[entitiesFound].iEntity = ent;
            GetEntPropVector(ent, Prop_Send, "m_vecOrigin", destinationEntities[entitiesFound].fOrigin);
            entitiesFound++;
        }
    }

    Shavit_LogMessage("Found %d teleports!", entitiesFound);
    float triggerCenter[3], mins[3], maxs[3];
    ArrayList matches = new ArrayList();
    for (int zone = 0; zone < numZonesCached; zone++) {
        if (unhookedZoneCache[zone].iType == Zone_Stage ||
            unhookedZoneCache[zone].iType == Zone_Start) {
            int data = unhookedZoneCache[zone].iTrack;
            if (unhookedZoneCache[zone].iData > 0) {
                data = unhookedZoneCache[zone].iData;
            }

            Autohook_NormalizeBox(unhookedZoneCache[zone].fCorner1, unhookedZoneCache[zone].fCorner2, mins, maxs);
            Autohook_AddOriginToCorners(unhookedZoneCache[zone].fDestination, mins, maxs);
            Autohook_GetTriggerVolumeCenter(mins, maxs, triggerCenter);
            for (int teleEntIdx = 0; teleEntIdx < entitiesFound; teleEntIdx++) {
                if (Autohook_CheckIfDestinationIsInZone2DBounds(destinationEntities[teleEntIdx].fOrigin,
                                                                mins,
                                                                maxs)) {
                // Checking first if there are any teleport destinations
                // in triggers on the x/y axis.
                    matches.Push(teleEntIdx);
                }
            }

            if (matches.Length == 0) {
                Shavit_LogMessage("No teleports for stage / track %d", data);
            } else {
                // We got teleports, now we need to go through
                // the matches and find the closest teleport destination
                // on the Z axis
                int closestMatchIndex = -1;
                float closestDistance = MAX_DISTANCE;
                for (int match = 0; match < matches.Length; match++) {
                    // Get the center of the trigger
                    // Find the distance between teleport and trigger
                    float closestDistanceTemp = Autohook_GetDistanceBetweenPoints(triggerCenter, destinationEntities[matches.Get(match)].fOrigin);
                    
                    if (closestDistanceTemp > closestDistance) {
                        continue;
                    } else {
                        closestDistance = closestDistanceTemp;
                        closestMatchIndex = match;
                    }
                }
                
                if (closestMatchIndex == -1) {
                    matches.Clear();
                    ReplyToCommand(caller, "[Timer]: Unable to find a teleport for stage / track %d, you may have to set a custom one.", data);
                    continue;
                }
                
                Shavit_LogMessage("Found a teleport for stage / track %d  with distance %.2f", data, closestDistance);
                Autohook_CopyVector(destinationEntities[matches.Get(closestMatchIndex)].fOrigin, unhookedZoneCache[zone].fDestination);
            }

            matches.Clear();
        }
        
    }
    matches.Close();
}

void Autohook_AddOriginToCorners(float origin[3], float mins[3], float maxs[3]) {
    for (int i = 0; i < 3; i++) {
        mins[i] += origin[i];
        maxs[i] += origin[i];
    }
}

void Autohook_CopyVector(float source[3], float dest[3]) {
    for (int i = 0; i < 3; i++) {
        dest[i] = source[i];
    }
}

void Autohook_GetTriggerVolumeCenter(float zoneMins[3], float zoneMaxs[3], float buffer[3]) {
    for (int i = 0; i < 3; i++) {
        buffer[i] = (zoneMins[i] + zoneMaxs[i]) / 2.0;
    }
}

float Autohook_GetDistanceBetweenPoints(float left[3], float right[3]) {
    return Abs(SquareRoot(
        Pow(left[0] - right[0], 2.0) +
        Pow(left[1] - right[1], 2.0) +
        Pow(left[2] - right[2], 2.0)
    ));
}

void Autohook_NormalizeBox(float corner1[3], float corner2[3], float mins[3], float maxs[3]) { 
    for (int i = 0; i < 3; i++) {
        mins[i] = (corner1[i] < corner2[i]) ? corner1[i] : corner2[i];
        maxs[i] = (corner1[i] > corner2[i]) ? corner1[i] : corner2[i];
    }
}

bool Autohook_AreVectorsEqual(float left[3], float right[3]) {
    bool result = true;

    for (int i = 0; i < 3; i++) {
        if (left[i] != right[i]) {
            result = false;
            break;
        }
    }

    return result;
}

bool Autohook_CheckIfDestinationIsInZone2DBounds(float destination[3], float mins[3], float maxs[3]) {
    float destX = destination[0], 
          destY = destination[1],
          minX = mins[0], 
          minY = mins[1],
          maxX = maxs[0], 
          maxY = maxs[1];

    bool result = (destX >= minX && destX <= maxX) &&
                  (destY >= minY && destY <= maxY);

    return result;
}

bool Autohook_IsZoneEntity(const char[] entityName) {
    return (StrContains(entityName, "stage", false) != -1) ||
           (StrContains(entityName, "start", false) != -1) ||
           (StrContains(entityName, "end", false) != -1) ||
           (StrContains(entityName, "checkpoint", false) != -1);
}

bool Autohook_IsBonusZone(const char[] entityName) {
    return (StrContains(entityName, "bonus", false) != -1);
}

bool Autohook_EntityNameHasNumber(const char[] entityName) {
    for (int i = 0; i < strlen(entityName); i++) {
        if (IsCharNumeric(entityName[i])) {
            return true;
        }
    }

    return false;
}

void Autohook_ExtractZoneInfoFromName(const char[] entityName, int &track, int &type, AutohookStatus &result, int &stage = -1,  bool areStagesTracks = false) {
    //Shavit_LogMessage("Entity Name: %s", entityName);

    char error[128],
         prefix[64],
         suffix[64];
    Regex regex = null;
    RegexError errorCode = REGEX_ERROR_NONE;

    if (Autohook_EntityNameHasNumber(entityName)) {
        // Capture Group 1 ([a-zA-Z]+)  match one or more letters
        // Capture Group 2 (\d+)        match one or more digits
        // Capture Group 3 ([a-zA-Z]+)  match one or more letters optional
        // Capture Group 4 (?:th|TH)    non-capturing optional group checking for telehop prefix.

        static const char numberPattern[] = "^([a-zA-Z]+)_?(\\d+)_?([a-zA-Z]+)?[_|\\s]?(?:th|TH)?$";
        char number[64];
        
        regex = new Regex(numberPattern, _, error, sizeof(error), errorCode);
        if (errorCode != REGEX_ERROR_NONE) {
            track = Track_Main;
            type = Zone_Unknown;
            stage = 0;
            Shavit_LogMessage("ENCOUNTERED REGEX ERROR ERROR WHILE PARSING ZONE INFO: %s", error);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        }

        regex.MatchAll(entityName, errorCode)
        if (errorCode != REGEX_ERROR_NONE) {
            track = Track_Main;
            type = Zone_Unknown;
            stage = 0;
            Shavit_LogMessage("ENCOUNTERED REGEX ERROR WHILE PARSING ZONE INFO: %s", error);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        } 

        if (regex.MatchCount() < 1) {
            Shavit_LogMessage("ERROR WHILE PARSING ZONE INFO: Unable to extract stage information from %s", entityName);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        }
        
        regex.GetSubString(1, prefix, sizeof(prefix));
        regex.GetSubString(2, number, sizeof(number));

        int tempTrack = -1

        int captures = regex.CaptureCount();
        if (captures == 4) {
            regex.GetSubString(3, suffix, sizeof(suffix));
            tempTrack = StringToInt(number);

            if (tempTrack == 0) {
                Shavit_LogMessage("ERROR WHILE PARSING ZONE INFO: Stage number extracted was 0! %s", entityName);
                regex.Close();
                result = AutohookStatus_Partial;
                return;
            }
        } else {
            tempTrack = Track_Main;
        }
        track = tempTrack;

        int tempType = Autohook_GetZoneTypeFromString(prefix);
        switch (tempType) {
            case -1: {
                tempType = Autohook_GetZoneTypeFromString(suffix); // stage_1_start, stage_3_end, bonus4_end
                if (tempType == -1) {
                    Shavit_LogMessage("ERROR WHILE PARSING ZONE INFO: Unable to extract zone from string %s (%s)", suffix, entityName);
                    regex.Close();
                    result = AutohookStatus_Partial;
                    return;
                }

                if (StrEqual(prefix, "bonus", false)) {
                    tempTrack = tempTrack + Track_Bonus;
                } else if (StrEqual(prefix, "main", false)) {
                    tempTrack = Track_Main;
                }
            }

            case Zone_Stage: {
                if (areStagesTracks) {
                    track = tempTrack;
                } else {
                    stage = tempTrack;
                    track = Track_Main;
                }
            }

            case Zone_Checkpoint: {
                stage = StringToInt(number);
            }

            // This checks for prefixes like
            // start_zone
            // end_zone
            case Zone_Start, Zone_End: {
                track = Track_Main;
            }
        }
        type = tempType;
    } else {
        static const char noNumberPattern[] = "^([a-zA-Z]+)_([a-zA-Z]+)$";
        regex = new Regex(noNumberPattern, _, error, sizeof(error), errorCode);
        if (errorCode != REGEX_ERROR_NONE) {
            track = Track_Main;
            type = Zone_Unknown;
            stage = 0;
            Shavit_LogMessage("ENCOUNTERED REGEX ERROR WHILE PARSING ZONE INFO: %s", error);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        }

        regex.MatchAll(entityName, errorCode);
        

        if (errorCode != REGEX_ERROR_NONE) {
            track = Track_Main;
            type = Zone_Unknown;
            stage = 0;
            Shavit_LogMessage("ENCOUNTERED REGEX ERROR WHILE PARSING ZONE INFO: %s", error);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        }

        
        if (regex.MatchCount() < 1) {
            Shavit_LogMessage("ERROR WHILE PARSING ZONE INFO: Unable to extract stage information from %s", entityName);
            regex.Close();
            result = AutohookStatus_Partial;
            return;
        }

        regex.GetSubString(1, prefix, sizeof(prefix));
        regex.GetSubString(2, suffix, sizeof(suffix));

        int tempType = Autohook_GetZoneTypeFromString(prefix);
        if (tempType == -1) {
            tempType = Autohook_GetZoneTypeFromString(suffix);

            if (tempType == -1) {
                Shavit_LogMessage("ERROR WHILE PARSING ZONE INFO: Unable to extract zone from string %s (%s)", suffix, entityName);
                regex.Close();
                result = AutohookStatus_Partial;
                type = Zone_Unknown;
                return;
            }
        }
        type = tempType;

        if (StrEqual(prefix, "main", false) || StrEqual(prefix, "start", false) || StrEqual(prefix, "end", false)) {
            track = Track_Main;
        } else if (StrEqual(prefix, "bonus", false)) {
            track = Track_Bonus;
        }
    }
    
    regex.Close();
}

void Autohook_AssignZoneInfo(zone_cache_t[] zoneCache, int numZonesInCache, AutohookStatus &result) {
    int track = Track_Main,
        stage = 0,
        type = Zone_Unknown;
    char entityName[MAX_NAME_LENGTH];
    
    for (int i = 0; i < numZonesInCache; i++) {
        GetEntPropString(zoneCache[i].iEntity, Prop_Data, "m_iName", entityName, sizeof(entityName));
        Autohook_ExtractZoneInfoFromName(entityName, track, type, result, stage, areStagesSeparateTracks);

        if (track == -1 || type == Zone_Unknown) {
            Shavit_LogMessage("ERROR WHILE ASSIGNING ZONE INFO! INVALID TRACK OR TYPE ENCOUNTERED!");
            result = AutohookStatus_Partial;
            zoneCache[i].iType = Zone_Unknown;
            zoneCache[i].iTrack = Track_Main;
            continue;
        }

        zoneCache[i].iTrack = track;
        zoneCache[i].iType = type;
        zoneCache[i].iData = stage;
    }
}

int Autohook_GetZoneFormFromClassname(const char[] classname) {
    if (StrEqual(classname, "trigger_multiple")) {
        return ZoneForm_trigger_multiple;
    }

    if (StrEqual(classname, "func_button")||
        StrEqual(classname, "func_rot_button")) {
        return ZoneForm_func_button;
    }

    return -1;
}

void GetMinsFromEntity(const int entity, float buffer[3]) {
    GetEntPropVector(entity, Prop_Send, "m_vecMins", buffer);
}

void GetMaxsFromEntity(const int entity, float buffer[3]) {
    GetEntPropVector(entity, Prop_Send, "m_vecMaxs", buffer);
}

void Autohook_InsertZones() {
    if (numZonesCached == 0) {
        return;
    }

    for (int i = 0; i < numZonesCached; i++) {
        if (unhookedZoneCache[i].iType == Zone_Unknown) {
            continue;
        }

        gA_EditCache[SERVER] = unhookedZoneCache[i];
        InsertZone(SERVER);
    }
    
    //RefreshZones();
}

void Autohook_SubmitAutohookStatus(AutohookStatus result) {
    char sQuery[1024];
    char error[255],
         map[255];

    GetCurrentMap(map, sizeof(map));
    switch(result) {
        case AutohookStatus_NoZones: {
            FormatEx(error, sizeof(error),
            "No valid zones found to hook.");
        }

        case AutohookStatus_MaxNumberOfZones: {
            FormatEx(error, sizeof(error),
            "Reached the max limit of zones for a map.");
        }

        case AutohookStatus_Partial: {
            FormatEx(error, sizeof(error),
            "Hooked (%d/%d) zones, check your error logs for more information.",
            numZonesCached, numZonesFound);
        }
    }

    FormatEx(sQuery, sizeof(sQuery),
    "INSERT INTO %sautohooks (`map`, `zones`, `status`, `info`) VALUES ('%s', %d, %d, '%s');",
    gS_MySQLPrefix, map, numZonesCached, result, error);
}

Action Autohook_Start(int client) {
    ReplyToCommand(client, "called");
    if (!gH_SQL) {
        ReplyToCommand(client, "[Timer] ERROR! Database is not currently loaded.")
        return Plugin_Handled;
    }

	ReplyToCommand(client, "[Timer]: Searching for unhooked zones...");

    AutohookStatus result = AutohookStatus_Success;
	Autohook_FillUnhookedZoneCache(result);
    if (result > AutohookStatus_Partial) {
        Autohook_ResetAutohook();
        return Plugin_Handled;
    }

    ReplyToCommand(client, "[Timer]: Gathering info about zones...")
    Autohook_AssignZoneInfo(unhookedZoneCache, numZonesCached, result);
    RecalcHighestStage();

    ReplyToCommand(client, "[Timer]: Setting track / stage teleport destinations to nearest info_teleport_destination...");
    Autohook_GetTeleportDestinations(client);

    ReplyToCommand(client, "[Timer]: Inserting zones into database..");
    Autohook_InsertZones();
    Autohook_SubmitAutohookStatus(result);

    FindEntitiesToHook("trigger_multiple", ZoneForm_trigger_multiple);
    FindEntitiesToHook("func_button", ZoneForm_func_button);

    ReplyToCommand(client, "[Timer]: Resetting autohook system...");
	Autohook_ResetAutohook();

	ReplyToCommand(client, "[Timer]: Finished!");

    if (result == AutohookStatus_Partial) {
        ReplyToCommand(client, "[Timer]: Autohook was partial success (%d/%d), check your error logs for more information.",
        numZonesCached, numZonesFound);    
    }

    return Plugin_Handled;
}

public Action Command_AutohookZones(int client, int args) {
    return Autohook_Start(client);
}