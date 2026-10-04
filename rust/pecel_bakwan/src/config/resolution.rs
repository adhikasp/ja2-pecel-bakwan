use std::default::Default;
use std::fmt;
use std::fmt::Display;
use std::str::FromStr;

use serde::{Deserialize, Deserializer, Serialize, Serializer};

/// Struct that contains a specific resolution for the game.
///
/// `Resolution(0, 0)` is the special value [`Resolution::AUTO`]: use the desktop size.
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub struct Resolution(pub u16, pub u16);

impl Resolution {
    /// Use the size of the desktop (serialized as `"auto"`).
    pub const AUTO: Resolution = Resolution(0, 0);

    /// The classic 640x480 resolution.
    pub const CLASSIC: Resolution = Resolution(640, 480);

    /// Whether this is the special `auto` value
    pub fn is_auto(&self) -> bool {
        *self == Resolution::AUTO
    }
}

impl FromStr for Resolution {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s.trim().eq_ignore_ascii_case("auto") {
            return Ok(Resolution::AUTO);
        }

        let mut resolutions = s.split('x').filter_map(|r_str| r_str.parse::<u16>().ok());

        match (resolutions.next(), resolutions.next()) {
            (Some(x), Some(y)) => Ok(Resolution(x, y)),
            _ => Err(String::from(
                "Incorrect resolution format, should be WIDTHxHEIGHT or auto.",
            )),
        }
    }
}

impl Display for Resolution {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.is_auto() {
            write!(f, "auto")
        } else {
            write!(f, "{}x{}", self.0, self.1)
        }
    }
}

impl Serialize for Resolution {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&format!("{self}"))
    }
}

impl<'de> Deserialize<'de> for Resolution {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let deserialized_string = String::deserialize(deserializer)?;
        Resolution::from_str(&deserialized_string).map_err(serde::de::Error::custom)
    }
}

impl Default for Resolution {
    fn default() -> Self {
        Resolution::AUTO
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_widthxheight_and_auto() {
        assert_eq!("1280x720".parse(), Ok(Resolution(1280, 720)));
        assert_eq!("auto".parse(), Ok(Resolution::AUTO));
        assert_eq!("AUTO".parse(), Ok(Resolution::AUTO));
        assert!("wide".parse::<Resolution>().is_err());
    }

    #[test]
    fn displays_auto_and_sizes() {
        assert_eq!(Resolution::AUTO.to_string(), "auto");
        assert_eq!(Resolution(640, 480).to_string(), "640x480");
    }

    #[test]
    fn default_is_auto() {
        assert!(Resolution::default().is_auto());
    }
}
