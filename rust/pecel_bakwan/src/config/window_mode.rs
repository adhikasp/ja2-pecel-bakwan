use std::default::Default;
use std::fmt;
use std::fmt::Display;
use std::str::FromStr;

use serde::{Deserialize, Serialize};

/// How the game window is presented on the desktop
#[derive(Debug, PartialEq, Eq, Copy, Clone, Serialize, Deserialize, Default)]
#[repr(C)]
pub enum WindowMode {
    /// A regular window with decorations
    Windowed,
    /// Exclusive fullscreen with a display mode change
    Fullscreen,
    /// A borderless window covering the desktop, no display mode change
    #[default]
    BorderlessDesktop,
}

impl FromStr for WindowMode {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_ascii_lowercase().as_str() {
            "windowed" | "window" => Ok(WindowMode::Windowed),
            "fullscreen" => Ok(WindowMode::Fullscreen),
            "borderless" | "borderlessdesktop" | "borderless_desktop" => {
                Ok(WindowMode::BorderlessDesktop)
            }
            _ => Err(format!(
                "Window mode {s} is unknown, use windowed, fullscreen or borderless"
            )),
        }
    }
}

impl Display for WindowMode {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                WindowMode::Windowed => "Windowed",
                WindowMode::Fullscreen => "Fullscreen (exclusive)",
                WindowMode::BorderlessDesktop => "Borderless desktop",
            }
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_names() {
        assert_eq!("windowed".parse(), Ok(WindowMode::Windowed));
        assert_eq!("Fullscreen".parse(), Ok(WindowMode::Fullscreen));
        assert_eq!("borderless".parse(), Ok(WindowMode::BorderlessDesktop));
        assert!("maximised".parse::<WindowMode>().is_err());
    }

    #[test]
    fn default_is_borderless() {
        assert_eq!(WindowMode::default(), WindowMode::BorderlessDesktop);
    }
}
