DELETE FROM `creature` WHERE `id1` IN (400100, 400101, 400102);

DELETE FROM `creature_template_model` WHERE `CreatureID` IN (400100, 400101, 400102, 400103, 400104);
DELETE FROM `creature_template` WHERE `entry` IN (400100, 400101, 400102, 400103, 400104);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`, `gossip_menu_id`,
    `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`,
    `speed_swim`, `speed_flight`, `detection_range`, `rank`, `dmgschool`,
    `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`,
    `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
    `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`, `MovementType`,
    `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `ExperienceModifier`, `RacialLeader`, `movementId`, `RegenHealth`,
    `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `VerifiedBuild`
)
SELECT
    400103, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, 'pvp教练员', 'PvP练习系统', `IconName`, 0,
    80, 80, `exp`, 35, 1, `speed_walk`, `speed_run`,
    `speed_swim`, `speed_flight`, `detection_range`, `rank`, `dmgschool`,
    `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`,
    `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
    `family`, `type`, `type_flags`, 0, 0, 0,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, '', 0,
    `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `ExperienceModifier`, `RacialLeader`, `movementId`, 1,
    `CreatureImmunitiesId`, `flags_extra`, 'pvp_trainer_npc', `VerifiedBuild`
FROM `creature_template`
WHERE `entry` = 6740;

INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`
)
VALUES
    (400103, 0, 5567, 1, 1, 0);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`, `gossip_menu_id`,
    `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`,
    `speed_swim`, `speed_flight`, `detection_range`, `rank`, `dmgschool`,
    `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`,
    `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
    `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`, `MovementType`,
    `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `ExperienceModifier`, `RacialLeader`, `movementId`, `RegenHealth`,
    `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `VerifiedBuild`
)
SELECT
    400104, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, '传送员', '主城与副本传送', `IconName`, 0,
    80, 80, `exp`, 35, 1, `speed_walk`, `speed_run`,
    `speed_swim`, `speed_flight`, `detection_range`, `rank`, `dmgschool`,
    `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`,
    `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
    `family`, `type`, `type_flags`, 0, 0, 0,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, '', 0,
    `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`,
    `ExperienceModifier`, `RacialLeader`, `movementId`, 1,
    `CreatureImmunitiesId`, `flags_extra`, 'teleporter_npc', `VerifiedBuild`
FROM `creature_template`
WHERE `entry` = 6740;

INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`
)
VALUES
    (400104, 0, 5567, 1, 1, 0);
