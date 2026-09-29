use std::default::Default;
use std::fmt;
use std::fmt::Display;
use std::str::FromStr;

use serde::{Deserialize, Deserializer, Serialize, Serializer};

/// Highest supported UI scale factor
pub const MAX_WORLD_ZOOM: u8 = 4;

/// Integer factor by which the tactical world layer is scaled up, independently of the UI.
///
/// `WorldZoom(0)` is [`WorldZoom::MATCH_UI`] (serialized as `"match_ui"`): the world and the UI
/// share one layer and one scale, which is the classic behaviour. Otherwise the value is in `1..=4`
/// and the world layer is rendered at that scale while the UI keeps the UI scale.
#[derive(Debug, PartialEq, Eq, Copy, Clone, Default)]
pub struct WorldZoom(pub u8);

impl WorldZoom {
    /// Single layer, the world follows the UI scale (serialized as `"match_ui"`).
    pub const MATCH_UI: WorldZoom = WorldZoom(0);

    /// Whether this is the special `match_ui` value
    pub fn is_match_ui(&self) -> bool {
        self.0 == 0
    }
}

impl FromStr for WorldZoom {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim();
        if s.eq_ignore_ascii_case("match_ui") || s.eq_ignore_ascii_case("match") || s.eq_ignore_ascii_case("auto") {
            return Ok(WorldZoom::MATCH_UI);
        }
        match s.parse::<u8>() {
            Ok(n) if (1..=MAX_WORLD_ZOOM).contains(&n) => Ok(WorldZoom(n)),
            _ => Err(format!(
                "World zoom must be match_ui or an integer between 1 and {}",
                MAX_WORLD_ZOOM
            )),
        }
    }
}

impl Display for WorldZoom {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.is_match_ui() {
            write!(f, "match_ui")
        } else {
            write!(f, "{}", self.0)
        }
    }
}

impl Serialize for WorldZoom {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        if self.is_match_ui() {
            serializer.serialize_str("match_ui")
        } else {
            serializer.serialize_u8(self.0)
        }
    }
}

#[derive(Deserialize)]
#[serde(untagged)]
enum WorldZoomRepr {
    Number(u8),
    Text(String),
}

impl<'de> Deserialize<'de> for WorldZoom {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        match WorldZoomRepr::deserialize(deserializer)? {
            WorldZoomRepr::Number(n) => n.to_string().parse(),
            WorldZoomRepr::Text(s) => s.parse(),
        }
        .map_err(serde::de::Error::custom)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_match_ui_and_range() {
        assert_eq!("match_ui".parse(), Ok(WorldZoom::MATCH_UI));
        assert_eq!("1".parse(), Ok(WorldZoom(1)));
        assert_eq!("4".parse(), Ok(WorldZoom(4)));
        assert!("0".parse::<WorldZoom>().is_err());
        assert!("5".parse::<WorldZoom>().is_err());
        assert!("big".parse::<WorldZoom>().is_err());
    }

    #[test]
    fn displays_like_it_parses() {
        assert_eq!(WorldZoom::MATCH_UI.to_string(), "match_ui");
        assert_eq!(WorldZoom(3).to_string(), "3");
    }

    #[test]
    fn default_is_match_ui() {
        assert!(WorldZoom::default().is_match_ui());
    }
}
