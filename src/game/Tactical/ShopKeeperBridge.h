#pragma once
// What the native shopkeeper screen (NativeUI/ShopKeeperNative.cc, docs/ui/shopkeeper.md) needs from the legacy
// arms-dealer trade screen. The trade rules, the offer areas and every transaction stay in ShopKeeper_Interface.cc;
// this is a read-only snapshot plus the commands the native buttons send, so the native and the legacy UI run
// exactly the same game code.
//
// The legacy trade model (the offer areas, the dealer's paged stock and the selected dealer) is file-local to
// ShopKeeper_Interface.cc. A native screen in another translation unit can only see the snapshot below; nothing
// here uses RmlUi.

#include <string>
#include <vector>

namespace ShopKeeperBridge
{
	/** Which list a detail panel is about. */
	enum Source
	{
		Stock = 0,
		DealerOffer = 1,
		PlayerOffer = 2,
		Inventory = 3
	};

	/** One item in a grid or a list. */
	struct Slot
	{
		int  index = 0;         // the source slot (an ArmsDealerOfferArea/PlayersOfferArea slot, a stock index, or an inv pocket)
		bool active = false;
		bool selected = false;      // already partly moved to the dealer's offer (stock)
		bool repaired = false;      // the item has finished repairing
		bool has_attachments = false;
		int  item = 0;              // item id
		int  qty = 0;
		int  condition = 0;         // 0..100, for the bar (ammo: bullets left as a percentage)
		std::string name;           // localized item name
		std::string art;            // "item-<id>@2"
		std::string price;          // formatted money (the dealer's price, the repair cost, or the dealer's buying price)
		std::string overlay;        // "repaired" | "jammed" | "" (the legacy slot overlay text)
		std::string owner_face;     // "face-<n>" when a merc owns the item, else ""
	};

	/** The comparison/detail panel: the last item the player pointed at. */
	struct Detail
	{
		bool has = false;
		int  source = Stock;
		std::string name, art, help;
		std::string price, value;         // the dealer's asking price and the dealer's buying price
		std::string condition;
		std::string overlay;
		int condition_class = 0;          // ShopKeeperModel::ConditionClass result (0 ok, 1 warn, 2 danger)
	};

	/** Everything the native screen draws and every state it needs, read from the live trade model. */
	struct View
	{
		bool active = false;
		bool repairs = false;                 // the dealer is a repairman
		bool can_buy = false, can_sell = false;

		std::string dealer_name, dealer_kind, dealer_face;
		std::string merc_name, merc_face;
		std::string balance, cash;
		std::string total_cost, total_value;

		int  page = 0, pages = 0;
		bool can_page_up = false, can_page_down = false;
		bool can_transact = false;
		bool has_stock = false, has_dealer_offer = false, has_player_offer = false, has_inventory = false;

		std::vector<Slot> stock, dealer_offer, player_offer, inventory;
		Detail detail;
	};

	/** The trade now, or active == false when the screen is not ready. */
	View GetView();

	// ---- commands (identical to the legacy mouse regions and buttons) -------------------------------
	void PageUp();
	void PageDown();
	/** The legacy left click on a dealer-stock slot: move it (one, or the whole stack with @a all) to the offer area. */
	void ClickStock(int slot, bool all);
	/** The legacy click on the dealer's offer area: take the item back (repairmen: return it to its owner). */
	void ClickDealerOffer(int slot, bool all);
	/** The legacy click on your offer area: take the item back. */
	void RemovePlayerOffer(int slot);
	/** The legacy "offer" (a click on the merc's inventory in the shop): try to sell/repair the pocket to the dealer. */
	void OfferItem(int pocket);
	/** The comparison panel follows the last item pointed at. */
	void SelectDetail(int source, int slot);
	void ClearDetail();
	/** The Transaction button. */
	void Transact();
	/** The Done button (says goodbye, then leaves; still stuff on the table is handled like the legacy Done). */
	void Done();
}
