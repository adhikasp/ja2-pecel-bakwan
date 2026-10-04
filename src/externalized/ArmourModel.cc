#include "ArmourModel.h"
#include "Equipment/AttachmentRules.h"
#include "Equipment/EquipmentCatalog.h"
#include "Equipment/Slots.h"
#include "Exceptions.h"
#include "GamePolicy.h"
#include "Item_Types.h"
#include "Weapons.h"
#include <cstdint>

uint8_t deserializeArmourClass(const ST::string& armourClass) {
	if (armourClass == "HELMET") return ARMOURCLASS_HELMET;
	if (armourClass == "VEST") return ARMOURCLASS_VEST;
	if (armourClass == "LEGGINGS") return ARMOURCLASS_LEGGINGS;
	if (armourClass == "PLATE") return ARMOURCLASS_PLATE;
	if (armourClass == "CREATURE") return ARMOURCLASS_MONST;
	throw DataError(ST::format("Unknown armour class '{}'", armourClass));
}

ArmourModel::ArmourModel(
			uint16_t itemIndex,
			ST::string&& internalName,
			ST::string&& shortName,
			ST::string&& name,
			ST::string&& description,
			ST::string&& bobbyRaysName,
			ST::string&& bobbyRaysDescription,
			InventoryGraphicsModel&& inventoryGraphics,
			TilesetTileIndexModel&& tileGraphic,
			uint8_t weight,
			uint8_t perPocket,
			uint16_t price,
			uint8_t coolness,
			int8_t reliability,
			int8_t repairEase,
			uint32_t flags,
			uint8_t armourClass,
			uint8_t protection,
			uint8_t explosivesProtection,
			uint8_t degradePercentage,
			bool ignoreForMaxProtection
	) : ItemModel(itemIndex, std::move(internalName), std::move(shortName), std::move(name), std::move(description), std::move(bobbyRaysName), std::move(bobbyRaysDescription), IC_ARMOUR, 0, INVALIDCURS, std::move(inventoryGraphics), std::move(tileGraphic), weight, perPocket, price, coolness, reliability, repairEase, flags) {
	this->armourClass = armourClass;
	this->protection = protection;
	this->explosivesProtection = explosivesProtection;
	this->degradePercentage = degradePercentage;
	this->ignoreForMaxProtection = ignoreForMaxProtection;
}

std::unique_ptr<ArmourModel> ArmourModel::deserialize(const JsonValue &json, TranslatableString::Loader& stringLoader) {
	auto obj = json.toObject();
	ItemModel::InitData const initData{ obj, stringLoader };

	int itemIndex = obj.GetInt("itemIndex");
	ST::string internalName = obj.GetString("internalName");
	auto shortName = ItemModel::deserializeShortName(initData);
	auto name = ItemModel::deserializeName(initData);
	auto description = ItemModel::deserializeDescription(initData);
	auto bobbyRaysName = ItemModel::deserializeBobbyRaysName(initData);
	auto bobbyRaysDescription = ItemModel::deserializeBobbyRaysDescription(initData);
	auto flags = ItemModel::deserializeFlags(obj);
	auto inventoryGraphics = InventoryGraphicsModel::deserialize(obj["inventoryGraphics"]);
	auto tileGraphic = TilesetTileIndexModel::deserialize(obj["tileGraphic"]);
	auto armourClass = deserializeArmourClass(obj.GetString("armourClass"));
	auto protection = obj.GetUInt("protection");
	auto explosivesProtection = obj.getOptionalUInt("explosivesProtection", protection);
	auto degradePercentage = obj.GetUInt("degradePercentage");
	auto ignoreForMaxProtection = obj.getOptionalBool("ignoreForMaxProtection", false);

	return std::make_unique<ArmourModel>(
		itemIndex,
		std::move(internalName),
		std::move(shortName),
		std::move(name),
		std::move(description),
		std::move(bobbyRaysName),
		std::move(bobbyRaysDescription),
		std::move(inventoryGraphics),
		std::move(tileGraphic),
		obj.GetUInt("ubWeight"),
		obj.GetUInt("ubPerPocket"),
		obj.GetUInt("usPrice"),
		obj.GetUInt("ubCoolness"),
		obj.GetInt("bReliability"),
		obj.GetInt("bRepairEase"),
		flags,
		armourClass,
		protection,
		explosivesProtection,
		degradePercentage,
		ignoreForMaxProtection
	);
}

uint8_t ArmourModel::getArmourClass() const {
	return armourClass;
}

uint8_t ArmourModel::getProtection() const {
	return protection;
}

uint8_t ArmourModel::getExplosivesProtection() const {
	return explosivesProtection;
}

uint8_t ArmourModel::getDegradePercentage() const {
	return degradePercentage;
}

bool ArmourModel::isIgnoredForMaxProtection() const {
	return ignoreForMaxProtection;
}

bool ArmourModel::canBeAttached(const GamePolicy* policy, const ItemModel* attachment) const {
	// Compatibility is by mount type: a plate goes in a plate pocket, goggles
	// on an NVG mount. No per-item lists.
	const Equipment::AttachmentDef* def = Equipment::AttachmentFor(attachment->getItemIndex());
	if (def == nullptr) return false;

	Equipment::SlotPolicy toggles = Equipment::TogglesFrom(policy);
	Equipment::Platform host = Equipment::SlotsFor(*this, toggles);
	uint16_t present[Equipment::MAX_HOST_SLOTS] = {};
	return Equipment::CanAttach(host, *def, present, true, toggles).ok;
}
