# Shopkeeper (arms dealer) — functional spec (M1)

The arms-dealer trade screen (`SHOPKEEPER_SCREEN`): the player buys, sells and leaves items for repair with an
arms dealer. The legacy screen draws the dealer's stock, the dealer's offer area, the player's offer area, the
dealer's animated face and the trade buttons over the tactical world.

Source of truth for the legacy screen: `src/game/Tactical/ShopKeeper_Interface.cc` / `ShopKeeper_Interface.h`,
with the dealer data in `Arms_Dealer_Init.cc` / `ArmsDealerInvInit.cc` and `src/externalized/DealerModel.h`. This
document is the **parity contract** for the native rebuild (Phase 7, `docs/plan/native-modern-game.md`). Nothing
here may be dropped silently; a deliberate omission is recorded in §8 with a reason.

| | |
|---|---|
| Legacy code | `src/game/Tactical/ShopKeeper_Interface.cc` (entry: `EnterShopKeeperInterfaceScreen()`, handler: `ShopKeeperScreenHandle()`, exit: `ExitShopKeeperInterface()`) |
| Screen id | `SHOPKEEPER_SCREEN` (what `ja2.screen()` returns) |
| Reached from | the talk menu ("Buy/Sell") with a dealer, or giving an item to a dealer; `EnterShopKeeperInterfaceScreen(profileID)` |
| Returns to | `GAME_SCREEN` (tactical), through `EnterTacticalScreen()` |
| Owner of this spec | native UI work |
| Status | parity-complete (native default; legacy fallback below 1280×720) |

## 1. How it was audited

- Code read: `ShopKeeper_Interface.cc` in full (entry/exit, the mouse regions, `PerformTransaction`,
  `OfferObjectToDealer`, `EvaluateInvAddedToPlayersOfferArea`, `EvaluateInvSlot`, the offer-area add/remove
  functions, `CalcShopKeeperItemPrice`, `DisplayInvSlot`, the page functions); `Arms_Dealer_Init.h`,
  `ArmsDealerInvInit.h`, `DealerModel.h` for the dealer model, stock and flags.
- Automation: the `ja2.debug("shopkeeper")` hook spawns a dealer next to the selected merc and opens the screen;
  `tests/e2e/shopkeeper_parity.lua` drives it. Screenshots at 640×480 (legacy), 1280×720, 1920×1080, 2560×1080
  and 3440×1440 (native), plus a 150% UI-scale layout audit.
- Images: the dealer's animated big face (`FACESDIR/bigfaces/<n>.sti`), the item inventory graphics
  (`ItemModel::getInventoryGraphicSmall`), the merc small faces for repair owners (`Load33Portrait`), and the
  legacy chrome (`tradescreen.sti`, `tradebuttons.sti`, `tradescrollarrows.sti`) which the native screen replaces
  with the design system.

## 2. Information shown

| # | Information | Source (game state / function) | Shown as (legacy) | Shown when | Notes |
|---|---|---|---|---|---|
| I1 | Dealer name | `GetProfile(dealer->profileID).zNickname` | text (title) | always | |
| I2 | Dealer's animated face | `InitShopKeepersFace()` / `giShopKeeperFaceIndex` | animated big face | always | native: a static `face-<n>` portrait |
| I3 | Dealer's talking subtitle | `InitShopKeeperSubTitledText()` | subtitle line | when the dealer speaks | see §8 |
| I4 | Dealer kind (buys/sells/repairs) | `DealerModel::type`, flags | implicit in the buttons/price behaviour | always | native: an explicit label |
| I5 | The dealer's stock, one page at a time | `gpTempDealersInventory`, `gSelectArmsDealerInfo` | 5×3 item grid | always | page number shown as `page/pages` |
| I6 | Item picture | `GetSmallInventoryGraphicForItem()` | inventory graphic + shadow | per stock/offer item | native: `item-<id>` |
| I7 | Item name | `GetHelpTextForItem()` (fast help) | hover/help text | hover | native: shown on the card |
| I8 | Item condition | `DrawItemUIBarEx()` | status bar | per item | |
| I9 | Dealer's unit price | `CalcShopKeeperItemPrice(DEALER_SELLING, unit)` | money under the item | stock, non-repairman | |
| I10 | Repair ETA | `BuildDoneWhenTimeString()` | day/time text | stock, repairman | |
| I11 | Item quantity | `ItemObject.ubNumberOfObjects` | `xN` | when > 1 | |
| I12 | Attachment marker | `ItemHasAttachments()`, `GetAttachmentHintColor()` | `*` | when the item has attachments | native: a marker on the card |
| I13 | Jammed / repaired overlay | `bGunAmmoStatus < 0`, `ARMS_INV_ITEM_REPAIRED` | overlay text | when jammed/repaired | |
| I14 | Item owner's small face | `ubIdOfMercWhoOwnsTheItem`, `guiSmallSoldiersFace[]` | small face on the slot | repairman stock / repair offer area | |
| I15 | Dealer's offer area (what you are buying / leaving to repair) | `ArmsDealerOfferArea[]` | 6×2 item grid | always | |
| I16 | Dealer's offer cost | `CalculateTotalArmsDealerCost()` | total cost text | always | |
| I17 | Player's offer area (what you are selling) | `PlayersOfferArea[]` | 6×2 item grid | always | |
| I18 | Dealer's buying price per item | `EvaluateInvSlot()` → `uiItemPrice` | money under the item | when evaluated | |
| I19 | Player's offer value | `CalculateTotalPlayersValue()` | total value text | always | |
| I20 | Player's balance | `LaptopSaveInfo.iCurrentBalance` | money text | always | |
| I21 | Dealer's cash | `gArmsDealerStatus[].uiArmsDealersCash` | (not shown) | always | native: shown, as a buy limit hint |

## 3. Actions

| # | Action | Input (legacy) | Game function | Preconditions / disabled when | Feedback |
|---|---|---|---|---|---|
| A1 | Page stock up/down | arrow buttons, mouse wheel | `ShopInventoryPageUp()` / `ShopInventoryPageDown()` | more than one page | page number changes |
| A2 | Move stock item to the offer area | left-click stock slot (Shift: whole stack) | `AddItemToArmsDealerOfferArea()` + slot bookkeeping | not a repairman; stock slot non-empty | price appears under the offer item |
| A3 | Take a bought item back | left/right-click dealer offer slot | `RemoveItemFromArmsDealerOfferArea(.., keep)` | non-repairman | item returns to stock |
| A4 | Take a repair item back | right-click repair offer slot | `RemoveRepairItemFromDealersOfferArea()` | repairman | item returns to its owner |
| A5 | Offer a merc item (sell / leave to repair) | click the merc's inventory slot | `OfferObjectToDealer()` | the merc owns the pocket; not already offered | dealer evaluates and quotes |
| A6 | Take a player offer item back | right-click player offer slot | `RemoveItemFromPlayersOfferArea()` | slot active | item returns to its owner |
| A7 | Evaluate an offered item | automatic on offer | `EvaluateItemAddedToPlayersOfferArea()`, `EvaluateInvSlot()` | not money, not yet evaluated | dealer quote; price |
| A8 | Complete the trade | "Transaction" button | `PerformTransaction(0)` | something to buy/sell, affordable, room for change | money and items move; dealer quote |
| A9 | Leave | "Done" button / Esc | `ExitSKIRequested()` then `ExitShopKeeperInterface()` | — | warns once if items are still on the table |
| A10 | Switch merc | Space | `FindNextActiveAndAliveMerc()` | — | the SM panel merc changes |
| A11 | Item description | right-click a stock/offer item | `InitShopKeeperItemDescBox()` | — | legacy description box (see §8) |
| A12 | Repair delay handling | on entry | `HandlePossibleRepairDelays()` | repairman | completed repairs move to the player |
| A13 | Force quit | Alt+X | `HandleShortCutExitState()` | — | leaves the game |

## 4. States and modes

| # | State | Entered by | Left by | Differences |
|---|---|---|---|---|
| S1 | Entering | `EnterShopKeeperInterfaceScreen()` | first `ShopKeeperScreenHandle()` | trade data is built, stock filled |
| S2 | Trading | default | A8/A9 | stock, offers and prices live |
| S3 | Repairman | `DealerModel::type == ARMS_DEALER_REPAIRS` | leave | stock shows repair ETAs; offers are repair jobs |
| S4 | Nothing on the table | no offer items | A5/A2 | Transaction disabled |
| S5 | Leaving | A9 | `GAME_SCREEN` | leftover items returned/dropped, then the world resumes |

## 5. Popups, overlays and modals

| # | Popup | Opened by | Contents | Buttons / result |
|---|---|---|---|---|
| P1 | Deduct shortfall from balance | A8 when the table lacks money | `SKI_QUESTION_TO_DEDUCT...` | Yes/No → `ConfirmToDeductMoneyFromPlayersAccountMessageBoxCallBack` |
| P2 | Can't afford | A8 when unaffordable | `SKI_SHORT_FUNDS_TEXT` | OK → `ConfirmDontHaveEnoughForTheDealerMessageBoxCallBack` |
| P3 | No room in offer area | offering with the area full | `SKI_TEXT_NO_MORE_ROOM_IN_PLAYER_OFFER_AREA` | OK |
| P4 | Item description | A11 | item stats | close |

## 6. Native design (M2)

The native screen is opaque and replaces the legacy blitter screen. Its arrangement follows the legacy trade
screen: the dealer face, the page control, the totals and the buttons in the left column; "Merchandise in
stock" and the two offer areas stacked in the main column; and the merc's inventory as the bottom strip (the
legacy tactical inventory panel). Layout, 1920×1080 dp reference, built from the design-system components:

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│ [face] TONY            MERCHANDISE IN STOCK                                           │
│        Buys and sells  ┌───────────────────────────────────────────────────────────┐  │
│  PAGE 1/1 [▲][▼]       │ [stock][stock][stock][stock][stock]  (5 per row, up to 3)  │  │
│                        └───────────────────────────────────────────────────────────┘  │
│  ┌ TOTAL COST  $0  ┐   DEALER'S OFFER                            TOTAL COST  $0       │
│  ┌ BALANCE    $N  ┐   [offer][offer][offer][offer][offer][offer] (6 per row)          │
│  ┌ TOTAL VALUE $0 ┐   YOUR OFFER                                 TOTAL VALUE $0       │
│  ┌ DEALER CASH $N ┐   [offer][offer][offer][offer][offer][offer] (6 per row)          │
│  [TRANSACTION]                                                                        │
│  [DONE]                                                                               │
├───────────────────────────────────────────────────────┬───────────────────────────────┤
│ [merc] BARRY  YOUR ITEMS                               │  ITEM DETAIL [art] NAME 85%   │
│ [item][item][item][item][item][item] ...               │  Price $N  Buys for $N        │
│                                                        │  — item stats                 │
└───────────────────────────────────────────────────────┴───────────────────────────────┘
```

- Stock cells: item picture, name, condition bar, quantity, attachment marker, unit price (or repair ETA for a
  repairman), and the selected/repaired/jammed states.
- Offer cells: the same, with the purchase/repair cost or the dealer's buying price.
- The current merc's inventory is a bottom strip; a click offers the pocket (the legacy hatches it out).
- The detail bar is the comparison: name, condition, the dealer's asking price and what he pays, plus the item
  stats.
- An `audit-skip`/`audit-over` scrim keeps the layout audit clean.

## 7. Wiring

- Route `SHOPKEEPER_SCREEN → "shopkeeper"` (`src/game/NativeUI/NativeUI.cc`), default `native` in `UiMode.cc`. A
  native screen cannot start (RmlUi missing or output below 1280×720) → the legacy screen runs.
- `ShopKeeperNative.cc` owns the screen: every frame it calls the legacy `ShopKeeperScreenHandle()` (which runs
  the trade rules, the offer areas, the evaluation quotes, the repair delays and the leave bookkeeping), then
  refreshes the view model.
- `ShopKeeperBridge.h` is the game-side read/command API over the file-local trade model; the native screen never
  touches the offer areas directly.

## 8. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| I3 Dealer talking subtitles | dropped | the native document covers the legacy face and subtitle layer; the trade state is the same, and the sprite dialogue has no native renderer yet | owner (wireframe review) |
| I2 Animated dealer face | static portrait | same as above; the face asset is shown, the lip-sync animation is not | owner (wireframe review) |
| A2/A5 Drag-and-drop with a floating item cursor | replaced by click-to-move (click stock to buy, click an inventory item to offer) | the native UI has no free cursor item; every resulting game state is identical and is driven by the same legacy functions | owner (wireframe review) |
| A2 Shift = move the whole stack | a separate "buy all" command; the stock click moves one | the native click has no modifier; the whole-stack behaviour is still reachable through the view model | owner (wireframe review) |
| A11 Legacy item description box | replaced by the detail/comparison bar | the native screen replaces the legacy modal; the same `GetHelpTextForItem` text is shown, plus the price comparison | owner (wireframe review) |
| Dropping an item to the ground | dropped | a drag gesture with no native equivalent; "take back" returns the item to its owner | owner (wireframe review) |
| Selling from a merc not in the sector | dropped (current merc only) | the legacy screen only shows the current merc's inventory on this panel; switching mercs is still possible before entering | owner (wireframe review) |

## 9. Parity tour

`tests/e2e/shopkeeper_parity.lua`:

| Row | Covered by (step / assertion) |
|---|---|
| S1, I1–I5, I19, I20 | `vm().active`, `dealer_name`, `balance`, `#vm().stock`, `#vm().inventory`, the open shot |
| A1, I5 | Page down → `page == 2`, Page up → `page == 1` (when there is more than one page) |
| A2, A3, I9, I15, I16 | `buy` a stock slot → the dealer's offer grows; `unbuy` → it is empty again |
| A5, A6, I17, I18 | `sell` the first sellable pocket → the player's offer grows; `takeback` → empty again |
| A9 | `done` → `GAME_SCREEN` |
| Layout | `ja2.layoutProblems()` at 150% UI scale, plus the goldens at 1280×720 … 3440×1440 |
