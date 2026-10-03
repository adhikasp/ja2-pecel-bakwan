use std::default::Default;
use std::fmt;
use std::fmt::Display;
use std::str::FromStr;

use serde::{Deserialize, Deserializer, Serialize, Serializer};

/// Highest supported UI scale factor
pub const MAX_UI_SCALE: u8 = 4;

/// Integer factor by which the whole game (world and UI) is scaled up.
///
/// `UiScale(0)` is [`UiScale::AUTO`]: the engine picks the largest scale that keeps the
/// logical canvas at least 1280x720. Otherwise the value is in `1..=4`.
#[derive(Debug, PartialEq, Eq, Copy, Clone, Default)]
pub struct UiScale(pub u8);

impl UiScale {
    /// Let the engine choose (serialized as `"auto"`).
    pub const AUTO: UiScale = UiScale(0);

    /// Whether this is the special `auto` value
    pub fn is_auto(&self) -> bool {
        self.0 == 0
    }
}

impl FromStr for UiScale {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim();
        if s.eq_ignore_ascii_case("auto") {
            return Ok(UiScale::AUTO);
        }
        match s.parse::<u8>() {
            Ok(n) if (1..=MAX_UI_SCALE).contains(&n) => Ok(UiScale(n)),
            _ => Err(format!(
                "UI scale must be auto or an integer between 1 and {MAX_UI_SCALE}"
            )),
        }
    }
}

impl Display for UiScale {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.is_auto() {
            write!(f, "auto")
        } else {
            write!(f, "{}", self.0)
        }
    }
}

impl Serialize for UiScale {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        if self.is_auto() {
            serializer.serialize_str("auto")
        } else {
            serializer.serialize_u8(self.0)
        }
    }
}

#[derive(Deserialize)]
#[serde(untagged)]
enum UiScaleRepr {
    Number(u8),
    Text(String),
}

impl<'de> Deserialize<'de> for UiScale {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        match UiScaleRepr::deserialize(deserializer)? {
            UiScaleRepr::Number(n) => n.to_string().parse(),
            UiScaleRepr::Text(s) => s.parse(),
        }
        .map_err(serde::de::Error::custom)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_auto_and_range() {
        assert_eq!("auto".parse(), Ok(UiScale::AUTO));
        assert_eq!("1".parse(), Ok(UiScale(1)));
        assert_eq!("4".parse(), Ok(UiScale(4)));
        assert!("0".parse::<UiScale>().is_err());
        assert!("5".parse::<UiScale>().is_err());
        assert!("big".parse::<UiScale>().is_err());
    }

    #[test]
    fn displays_like_it_parses() {
        assert_eq!(UiScale::AUTO.to_string(), "auto");
        assert_eq!(UiScale(3).to_string(), "3");
    }

    #[test]
    fn default_is_auto() {
        assert!(UiScale::default().is_auto());
    }
}
