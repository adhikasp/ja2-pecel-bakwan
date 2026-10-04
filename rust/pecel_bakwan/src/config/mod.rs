//! This module contains code to configure the ja2-pecel-bakwan engine

mod cli;
mod engine_options;
mod ja2_json;
mod resolution;
mod scaling_quality;
mod pecel_bakwan_home;
mod ui_scale;
mod vanilla_version;
mod window_mode;
mod world_zoom;

pub use self::cli::{Cli, CliError};
pub use self::engine_options::{EngineOptions, EngineOptionsError};
pub use self::ja2_json::{Ja2Json, Ja2JsonError};
pub use self::resolution::Resolution;
pub use self::scaling_quality::ScalingQuality;
pub use self::pecel_bakwan_home::find_pecel_bakwan_home;
pub use self::ui_scale::{MAX_UI_SCALE, UiScale};
pub use self::vanilla_version::VanillaVersion;
pub use self::window_mode::WindowMode;
pub use self::world_zoom::{MAX_WORLD_ZOOM, WorldZoom};
