-- The native people & quest registry (issue #156) is readable headless and its
-- quest status tracks the real quest engine.
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")

local npcs = ja2.game.npcs()
local quests = ja2.game.quests()
ja2.expect(#npcs == 159, "the roster has 159 named characters, got " .. #npcs)
ja2.expect(#quests == 24, "24 quests are ported, got " .. #quests)

-- DELIVER_LETTER is started at game init by CheckForQuests.
local letter
for _, q in ipairs(quests) do
    if q.name == "DELIVER_LETTER" then letter = q end
end
ja2.expect(letter, "DELIVER_LETTER has a def")
ja2.expect(letter.status == "IN_PROGRESS",
    "the letter quest is started at game init, got " .. letter.status)
ja2.expect(#letter.givers == 0 and #letter.resolvers >= 1,
    "the letter quest has no giver and is resolved by a person")
ja2.expect(letter.selfResolving, "the letter quest is explicitly self-resolving")

-- Script-record links are in the registry, across the binary and JSON layers.
local function npc(name)
    for _, n in ipairs(npcs) do if n.name == name then return n end end
end

local miguel = npc("MIGUEL")
ja2.expect(miguel, "MIGUEL has a def")
ja2.expect(miguel.kind == "RPC", "MIGUEL is an RPC, got " .. tostring(miguel.kind))
local startsFoodRoute = false
for _, l in ipairs(miguel.quests) do
    if l.name == "FOOD_ROUTE" and l.role == "giver" then startsFoodRoute = true end
end
ja2.expect(startsFoodRoute, "MIGUEL gives the food route")

local maria = npc("MARIA")
ja2.expect(maria, "MARIA has a def")
local rescueMaria = false
for _, l in ipairs(maria.quests) do
    if l.name == "RESCUE_MARIA" and l.role == "dialogue" then rescueMaria = true end
end
ja2.expect(rescueMaria, "MARIA's JSON override reached the registry")

local aunties = npc("AUNTIE")
ja2.expect(aunties, "AUNTIE has a def")
local startsBloodcats, endsBloodcats = false, false
for _, l in ipairs(aunties.quests) do
    if l.name == "BLOODCATS" and l.role == "giver" then startsBloodcats = true end
    if l.name == "BLOODCATS" and l.role == "resolver" then endsBloodcats = true end
end
ja2.expect(startsBloodcats and endsBloodcats, "AUNTIE both gives and resolves BLOODCATS")
