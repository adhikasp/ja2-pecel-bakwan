-- Layered tactical: UI that is placed relative to the world or over it (dialogue, messages,
-- edge scrolling, overhead map). Run with -uiscale 2 -worldzoom 1.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.debug("message", "hello from the UI layer")
ja2.waitIdle()
shots.take("layers_message.png")

ja2.debug("quote")
ja2.wait(500)
shots.take("layers_quote.png")
ja2.key("ESC")
ja2.waitIdle()

-- edge scroll: park the mouse at the right edge of the window for a while
local size = ja2.screenSize()
local before = ja2.screenshot("layers_scroll_before.png")
ja2.move(size.w - 1, size.h // 3)
ja2.wait(1500)
ja2.move(size.w // 2, size.h // 3)
ja2.waitIdle()
shots.take("layers_scrolled.png")

-- overhead map and back
ja2.key("insert")
ja2.wait(500)
ja2.screenshot("layers_overhead.png")
ja2.key("insert")
ja2.wait(500)
shots.take("layers_after_overhead.png")
