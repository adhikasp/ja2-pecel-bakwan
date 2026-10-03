// The native shopkeeper (arms-dealer trade) screen (docs/ui/shopkeeper.md, Phase 7 of
// docs/plan/native-modern-game.md).
//
// How it works: the legacy trade screen keeps running underneath (ShopKeeperScreenHandle is called every frame:
// the trade rules, the offer areas, the evaluation quotes, the repair delays and the leave bookkeeping are
// unchanged). The native document covers the legacy blitter screen and draws the stock, the two offer areas and
// the merc's inventory from ShopKeeperBridge's snapshot; every control calls the same legacy function the legacy
// button or mouse region calls, so the native and the legacy UI run exactly the same game code.
#include "ShopKeeperBridge.h"
#include "ShopKeeperModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "ShopKeeper_Interface.h"
#include "UiCore.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace NativeUI
{

namespace
{
	struct SlotRow
	{
		int i = 0;
		int qty = 0;
		int condition = 0;
		bool active = false, selected = false, repaired = false, has_attachments = false;
		std::string name, art, price, overlay, owner_face, cls, condition_class;
		bool operator==(SlotRow const&) const = default;
		static void Describe(RowFields<SlotRow>& f)
		{
			f("i", &SlotRow::i)("qty", &SlotRow::qty)("condition", &SlotRow::condition)
			 ("active", &SlotRow::active)("selected", &SlotRow::selected)("repaired", &SlotRow::repaired)
			 ("has_attachments", &SlotRow::has_attachments)("name", &SlotRow::name)("art", &SlotRow::art)
			 ("price", &SlotRow::price)("overlay", &SlotRow::overlay)("owner_face", &SlotRow::owner_face)
			 ("cls", &SlotRow::cls)("condition_class", &SlotRow::condition_class);
		}
	};

	constexpr int DetailStock = 0, DetailDealerOffer = 1, DetailPlayerOffer = 2, DetailInventory = 3;
	[[maybe_unused]] constexpr int kDetailStub = DetailStock + DetailDealerOffer + DetailPlayerOffer + DetailInventory;

	class ShopKeeperViewModel final : public ViewModel
	{
	public:
		ShopKeeperViewModel() : ViewModel("shopkeeper", TOPIC_ALL)
		{
			Command("pageup",   [this](Args const&) { ShopKeeperBridge::PageUp();   Poke(); });
			Command("pagedown", [this](Args const&) { ShopKeeperBridge::PageDown(); Poke(); });
			Command("buy",      [this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::ClickStock(std::atoi(a[0].c_str()), false); Poke(); } });
			Command("buy_all",  [this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::ClickStock(std::atoi(a[0].c_str()), true);  Poke(); } });
			Command("unbuy",    [this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::ClickDealerOffer(std::atoi(a[0].c_str()), false); Poke(); } });
			Command("unbuy_all",[this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::ClickDealerOffer(std::atoi(a[0].c_str()), true);  Poke(); } });
			Command("takeback", [this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::RemovePlayerOffer(std::atoi(a[0].c_str())); Poke(); } });
			Command("sell",     [this](Args const& a) { if (!a.empty()) { ShopKeeperBridge::OfferItem(std::atoi(a[0].c_str())); Poke(); } });
			Command("detail",   [this](Args const& a) { if (a.size() >= 2) { ShopKeeperBridge::SelectDetail(std::atoi(a[0].c_str()), std::atoi(a[1].c_str())); Poke(); } });
			Command("transaction", [this](Args const&) { ShopKeeperBridge::Transact(); Poke(); });
			Command("done",        [this](Args const&) { ShopKeeperBridge::Done();     Poke(); });
			Labels();
		}

		/** Reads the game again now (after forwarding a command) instead of at the next frame. */
		void Poke() { Refresh(); }

		void Labels()
		{
			for (char const* k : { "merchandise", "page", "dealer_offer", "your_offer", "your_items", "transaction",
				"repair", "done", "balance", "cash", "total_cost", "total_value", "page_up", "page_down",
				"price", "value", "condition", "compare", "no_items", "jammed", "repaired", "empty" })
			{
				labels[k] = Str(std::string("sk.") + k);
			}
			labels["kind"] = Str("sk.kind." + dealerKind);
		}

		void Describe(Fields& f) override
		{
			f.Field("active", active);
			f.Field("repairs", repairs);
			f.Field("dealer_name", dealerName); f.Field("dealer_face", dealerFace);
			f.Field("dealer_kind", dealerKindText);
			f.Field("merc_name", mercName); f.Field("merc_face", mercFace);
			f.Field("balance", balance); f.Field("cash", cash);
			f.Field("total_cost", totalCost); f.Field("total_value", totalValue);
			f.Field("page", page); f.Field("pages", pages); f.Field("page_text", pageText);
			f.Field("can_page_up", canPageUp); f.Field("can_page_down", canPageDown);
			f.Field("can_transact", canTransact);
			f.Field("has_stock", hasStock); f.Field("has_dealer_offer", hasDealerOffer);
			f.Field("has_player_offer", hasPlayerOffer); f.Field("has_inventory", hasInventory);
			f.Field("detail_has", detailHas); f.Field("detail_name", detailName); f.Field("detail_art", detailArt);
			f.Field("detail_help", detailHelp); f.Field("detail_price", detailPrice);
			f.Field("detail_value", detailValue); f.Field("detail_condition", detailCondition);
			f.Field("detail_condition_class", detailConditionClass); f.Field("detail_overlay", detailOverlay);
			f.Rows("stock", stock); f.Rows("dealer_offer", dealerOffer);
			f.Rows("player_offer", playerOffer); f.Rows("inventory", inventory);
			for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
		}

		void Refresh() override
		{
			std::string const before = Snapshot().ToJson();
			ShopKeeperBridge::View const v = ShopKeeperBridge::GetView();
			active = v.active; repairs = v.repairs;
			dealerName = v.dealer_name; dealerFace = v.dealer_face; dealerKind = v.dealer_kind;
			dealerKindText = dealerKind.empty() ? std::string() : Str("sk.kind." + dealerKind);
			mercName = v.merc_name; mercFace = v.merc_face;
			balance = v.balance; cash = v.cash; totalCost = v.total_cost; totalValue = v.total_value;
			page = v.page; pages = v.pages; pageText = std::to_string(v.page) + "/" + std::to_string(v.pages);
			canPageUp = v.can_page_up; canPageDown = v.can_page_down; canTransact = v.can_transact;
			hasStock = v.has_stock; hasDealerOffer = v.has_dealer_offer;
			hasPlayerOffer = v.has_player_offer; hasInventory = v.has_inventory;
			stock = MakeRows(v.stock); dealerOffer = MakeRows(v.dealer_offer);
			playerOffer = MakeRows(v.player_offer); inventory = MakeRows(v.inventory);
			detailHas = v.detail.has; detailName = v.detail.name; detailArt = v.detail.art; detailHelp = v.detail.help;
			detailPrice = v.detail.price; detailValue = v.detail.value; detailCondition = v.detail.condition;
			detailConditionClass = BucketClass(v.detail.condition_class); detailOverlay = OverlayText(v.detail.overlay);
			if (Snapshot().ToJson() != before) Changed();
		}

	private:
		static std::string BucketClass(int const bucket)
		{
			return bucket == 2 ? "danger" : bucket == 1 ? "warn" : "ok";
		}

		static std::string OverlayText(std::string const& overlay)
		{
			if (overlay == "jammed")   return Str("sk.jammed");
			if (overlay == "repaired") return Str("sk.repaired");
			return {};
		}

		static std::vector<SlotRow> MakeRows(std::vector<ShopKeeperBridge::Slot> const& in)
		{
			std::vector<SlotRow> out;
			out.reserve(in.size());
			for (ShopKeeperBridge::Slot const& s : in)
			{
				SlotRow r;
				r.i = s.index; r.qty = s.qty; r.condition = s.condition;
				r.active = s.active; r.selected = s.selected; r.repaired = s.repaired;
				r.has_attachments = s.has_attachments;
				r.name = s.name; r.art = s.art; r.price = s.price; r.overlay = s.overlay; r.owner_face = s.owner_face;
				r.condition_class = BucketClass(ShopKeeperModel::ConditionBucket(s.condition));
				r.cls = ShopKeeperModel::SlotClass(s.active, s.selected, s.repaired, s.overlay);
				out.push_back(std::move(r));
			}
			return out;
		}

		bool active = false, repairs = false;
		std::string dealerName, dealerFace, dealerKind, dealerKindText, mercName, mercFace;
		std::string balance, cash, totalCost, totalValue, pageText;
		int page = 0, pages = 0;
		bool canPageUp = false, canPageDown = false, canTransact = false;
		bool hasStock = false, hasDealerOffer = false, hasPlayerOffer = false, hasInventory = false;
		bool detailHas = false;
		std::string detailName, detailArt, detailHelp, detailPrice, detailValue, detailCondition;
		std::string detailConditionClass, detailOverlay;
		std::vector<SlotRow> stock, dealerOffer, playerOffer, inventory;
		std::map<std::string, std::string> labels;
	};

	class ShopKeeperNative final : public Screen
	{
	public:
		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage);
			RegisterFrontEndImages();
			m_vm = std::make_unique<ShopKeeperViewModel>();
			m_binding = std::make_unique<Binding>(Context(), *m_vm);
			m_doc = LoadDocument("screens/shopkeeper.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		}

		ScreenID Handle() override
		{
			// the legacy screen runs every frame: trade rules, offer areas, evaluation and leave bookkeeping
			ScreenID const next = ShopKeeperScreenHandle();
			if (next != SHOPKEEPER_SCREEN) return next;
			m_vm->Refresh();
			return SHOPKEEPER_SCREEN;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			m_vm.reset();
		}

	private:
		Rml::ElementDocument* m_doc = nullptr;
		std::unique_ptr<ShopKeeperViewModel> m_vm;
		std::unique_ptr<Binding> m_binding;
	};
}

std::unique_ptr<Screen> CreateShopKeeperScreen()
{
	return std::make_unique<ShopKeeperNative>();
}

}
