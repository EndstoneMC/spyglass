#pragma once

enum class MinecraftPacketIds : int {
    AddPlayer = 12,
    AddItemActor = 15,
    InventoryTransaction = 30,
    PlayerEquipment = 31,
    MobArmorEquipment = 32,
    InventoryContent = 49,
    InventorySlot = 50,
    CraftingData = 52,
    PlayerAuthInputPacket = 144,
    CreativeContent = 145,
    ItemStackRequest = 147,
    ItemRegistryPacket = 162,
};
