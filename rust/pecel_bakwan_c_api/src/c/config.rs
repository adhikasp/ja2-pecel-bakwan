//! This module contains the C interface for [`pecel_bakwan::config`].
//!
//! [`pecel_bakwan::config`]: ../../pecel_bakwan/config/index.html

use std::ptr;

use pecel_bakwan::config::{
    Cli, EngineOptions, EngineOptionsError, Ja2Json, Resolution, ScalingQuality, UiScale,
    VanillaVersion, WindowMode, WorldZoom, find_pecel_bakwan_home,
};

use crate::c::common::*;

/// Creates `EngineOptions` with the provided command line arguments.
/// Loads values from `(pecel_bakwan_home)/ja2.json`, creating it if it does not exist.
/// Logs relevant information when creating failed
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_create(
    pecel_bakwan_home: *const c_char,
    args: *const *const c_char,
    length: size_t,
) -> *mut EngineOptions {
    let pecel_bakwan_home = path_buf_from_c_str_or_panic(unsafe_c_str(pecel_bakwan_home));
    let args: Vec<String> = unsafe_slice(args, length)
        .iter()
        .map(|&x| str_from_c_str_or_panic(unsafe_c_str(x)).to_owned())
        .collect();

    match EngineOptions::from_home_and_args(&pecel_bakwan_home, &args) {
        Ok(engine_options) => {
            if engine_options.show_help {
                print!("{}", Cli::usage());
            }
            no_rust_error();
            into_ptr(engine_options)
        }
        Err(err) => {
            if let EngineOptionsError::Cli(_) = err {
                log::info!("{}", Cli::usage())
            }
            log::error!("{}", err);
            remember_rust_error(err.to_string());
            ptr::null_mut()
        }
    }
}

/// Creates empty engine options
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_default() -> *mut EngineOptions {
    into_ptr(EngineOptions::default())
}

/// Writes `EngineOptions` to `(pecel_bakwan_home)/ja2.json`.
/// Returns true on success, false otherwise.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_write(ptr: *mut EngineOptions) -> bool {
    let engine_options = unsafe_mut(ptr);
    let ja2_json = Ja2Json::from_pecel_bakwan_home(&engine_options.pecel_bakwan_home);
    ja2_json.write(engine_options).is_ok()
}

/// Deletes `EngineOptions`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_destroy(ptr: *mut EngineOptions) {
    let _drop_me = from_ptr(ptr);
}

/// Gets the `EngineOptions.pecel_bakwan_home` path.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getPecelBakwanHome() -> *mut c_char {
    let pecel_bakwan_home = find_pecel_bakwan_home();

    forget_rust_error();
    match pecel_bakwan_home {
        Ok(pecel_bakwan_home) => {
            let pecel_bakwan_home = c_string_from_path_or_panic(&pecel_bakwan_home);
            pecel_bakwan_home.into_raw()
        }
        Err(e) => {
            remember_rust_error(format!("EngineOptions_getPecelBakwanHome: {e:?}"));
            std::ptr::null_mut()
        }
    }
}

/// Gets the `EngineOptions.vanilla_game_dir` path.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getVanillaGameDir(ptr: *const EngineOptions) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    let vanilla_game_dir = c_string_from_path_or_panic(&engine_options.vanilla_game_dir);
    vanilla_game_dir.into_raw()
}

/// Gets the `EngineOptions.assets_dir` path.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getAssetsDir(ptr: *const EngineOptions) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    let vanilla_game_dir = c_string_from_path_or_panic(&engine_options.assets_dir);
    vanilla_game_dir.into_raw()
}

/// Sets the `EngineOptions.vanilla_game_dir` path.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setVanillaGameDir(
    ptr: *mut EngineOptions,
    game_dir_ptr: *const c_char,
) {
    let engine_options = unsafe_mut(ptr);
    let vanilla_game_dir = path_buf_from_c_str_or_panic(unsafe_c_str(game_dir_ptr));
    engine_options.vanilla_game_dir = vanilla_game_dir;
}

/// Gets the `EngineOptions.save_game_dir` path.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getSaveGameDir(ptr: *const EngineOptions) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    let save_game_dir = c_string_from_path_or_panic(&engine_options.save_game_dir);
    save_game_dir.into_raw()
}

/// Sets the `EngineOptions.save_game_dir` path.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setSaveGameDir(
    ptr: *mut EngineOptions,
    save_game_dir_ptr: *const c_char,
) {
    let engine_options = unsafe_mut(ptr);
    let save_game_dir = path_buf_from_c_str_or_panic(unsafe_c_str(save_game_dir_ptr));
    engine_options.save_game_dir = save_game_dir;
}

/// Checks if mod is enabled
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_isModEnabled(ptr: *mut EngineOptions, name: *const c_char) -> bool {
    let engine_options = unsafe_mut(ptr);
    let name = str_from_c_str_or_panic(unsafe_c_str(name)).to_owned();
    engine_options.is_mod_enabled(&name)
}

/// Gets the length of `EngineOptions.mods`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getModsLength(ptr: *const EngineOptions) -> u32 {
    let engine_options = unsafe_ref(ptr);
    engine_options.mods.len() as u32
}

/// Gets the target index of `EngineOptions.mods`.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getMod(ptr: *const EngineOptions, index: u32) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    match engine_options.mods.get(index as usize) {
        Some(str_mod) => {
            let c_str_mod = c_string_from_str(str_mod);
            c_str_mod.into_raw()
        }
        None => {
            let len = engine_options.mods.len();
            panic!("Invalid mod index {index}, len = {len}");
        }
    }
}

/// Clears `EngineOptions.mods`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_clearMods(ptr: *mut EngineOptions) {
    let engine_options = unsafe_mut(ptr);
    engine_options.mods.clear();
}

/// Adds a mod to `EngineOptions.mods`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_pushMod(ptr: *mut EngineOptions, name: *const c_char) {
    let engine_options = unsafe_mut(ptr);
    let name = str_from_c_str_or_panic(unsafe_c_str(name)).to_owned();
    engine_options.mods.push(name);
}

/// Gets the width of `EngineOptions.resolution`. 0 (with height 0) means auto: the desktop size.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getResolutionX(ptr: *const EngineOptions) -> u16 {
    let engine_options = unsafe_ref(ptr);
    engine_options.resolution.0
}

/// Gets the height of `EngineOptions.resolution`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getResolutionY(ptr: *const EngineOptions) -> u16 {
    let engine_options = unsafe_ref(ptr);
    engine_options.resolution.1
}

/// Sets `EngineOptions.resolution`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setResolution(ptr: *mut EngineOptions, x: u16, y: u16) {
    let engine_options = unsafe_mut(ptr);
    engine_options.resolution = Resolution(x, y);
}

/// Gets `EngineOptions.brightness`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getBrightness(ptr: *const EngineOptions) -> f32 {
    let engine_options = unsafe_ref(ptr);
    engine_options.brightness
}

/// Sets `EngineOptions.brightness`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setBrightness(ptr: *mut EngineOptions, brightness: f32) {
    let engine_options = unsafe_mut(ptr);
    engine_options.brightness = brightness
}

/// Gets `EngineOptions.resource_version`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getResourceVersion(ptr: *const EngineOptions) -> VanillaVersion {
    let engine_options = unsafe_ref(ptr);
    engine_options.resource_version
}

/// Sets `EngineOptions.resource_version`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setResourceVersion(ptr: *mut EngineOptions, res: VanillaVersion) {
    let engine_options = unsafe_mut(ptr);
    engine_options.resource_version = res;
}

/// Gets `EngineOptions.run_unittests`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldRunUnittests(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.run_unittests
}

/// Gets `EngineOptions.show_help`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldShowHelp(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.show_help
}

/// Gets `EngineOptions.run_editor`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldRunEditor(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.run_editor
}

/// Gets `EngineOptions.window_mode`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getWindowMode(ptr: *const EngineOptions) -> WindowMode {
    let engine_options = unsafe_ref(ptr);
    engine_options.window_mode
}

/// Sets `EngineOptions.window_mode`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setWindowMode(ptr: *mut EngineOptions, mode: WindowMode) {
    let engine_options = unsafe_mut(ptr);
    engine_options.window_mode = mode
}

/// Gets `EngineOptions.ui_scale`: 0 means auto, otherwise 1 to 4.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getUiScale(ptr: *const EngineOptions) -> u8 {
    let engine_options = unsafe_ref(ptr);
    engine_options.ui_scale.0
}

/// Sets `EngineOptions.ui_scale`: 0 means auto, otherwise 1 to 4 (larger values are clamped).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setUiScale(ptr: *mut EngineOptions, scale: u8) {
    let engine_options = unsafe_mut(ptr);
    engine_options.ui_scale = UiScale(scale.min(pecel_bakwan::config::MAX_UI_SCALE));
}

/// Gets `EngineOptions.world_zoom`: 0 means match the UI scale (single layer), otherwise 1 to 4.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getWorldZoom(ptr: *const EngineOptions) -> u8 {
    let engine_options = unsafe_ref(ptr);
    engine_options.world_zoom.0
}

/// Sets `EngineOptions.world_zoom`: 0 means match the UI scale, otherwise 1 to 4 (larger values are clamped).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setWorldZoom(ptr: *mut EngineOptions, zoom: u8) {
    let engine_options = unsafe_mut(ptr);
    engine_options.world_zoom = WorldZoom(zoom.min(pecel_bakwan::config::MAX_WORLD_ZOOM));
}

/// Gets `EngineOptions.ui_mode` as `screen=mode` pairs separated by newlines (for example "credits=native\nmsgbox=legacy").
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getUiModes(ptr: *const EngineOptions) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    let pairs: Vec<String> = engine_options
        .ui_mode
        .iter()
        .map(|(k, v)| format!("{k}={v}"))
        .collect();
    c_string_from_str(&pairs.join("\n")).into_raw()
}

/// Gets `EngineOptions.world_renderer` ("" when not set).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getWorldRenderer(ptr: *const EngineOptions) -> *mut c_char {
    let engine_options = unsafe_ref(ptr);
    c_string_from_str(&engine_options.world_renderer).into_raw()
}

/// Sets `EngineOptions.world_renderer` ("" = the engine's default).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setWorldRenderer(ptr: *mut EngineOptions, value: *const c_char) {
    let engine_options = unsafe_mut(ptr);
    engine_options.world_renderer = str_from_c_str_or_panic(unsafe_c_str(value)).to_owned();
}

/// Sets `EngineOptions.ui_mode[screen]`; an empty mode removes the entry (the engine default applies again).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setUiMode(
    ptr: *mut EngineOptions,
    screen: *const c_char,
    mode: *const c_char,
) {
    let engine_options = unsafe_mut(ptr);
    let screen = str_from_c_str_or_panic(unsafe_c_str(screen)).to_owned();
    let mode = str_from_c_str_or_panic(unsafe_c_str(mode)).to_owned();
    if mode.is_empty() {
        engine_options.ui_mode.remove(&screen);
    } else {
        engine_options.ui_mode.insert(screen, mode);
    }
}

/// Gets `EngineOptions.native_ui_scale` (1.0 = 100%).
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getNativeUiScale(ptr: *const EngineOptions) -> f32 {
    let engine_options = unsafe_ref(ptr);
    engine_options.native_ui_scale
}

/// Sets `EngineOptions.native_ui_scale`, clamped to 0.5 .. 3.0.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setNativeUiScale(ptr: *mut EngineOptions, scale: f32) {
    let engine_options = unsafe_mut(ptr);
    engine_options.native_ui_scale = if scale.is_finite() {
        scale.clamp(0.5, 3.0)
    } else {
        1.0
    };
}

/// Gets `EngineOptions.reduced_motion`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getReducedMotion(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.reduced_motion
}

/// Sets `EngineOptions.reduced_motion`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setReducedMotion(ptr: *mut EngineOptions, value: bool) {
    let engine_options = unsafe_mut(ptr);
    engine_options.reduced_motion = value;
}

/// Gets `EngineOptions.scaling_quality`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_getScalingQuality(ptr: *const EngineOptions) -> ScalingQuality {
    let engine_options = unsafe_ref(ptr);
    engine_options.scaling_quality
}

/// Sets `EngineOptions.scaling_quality`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setScalingQuality(
    ptr: *mut EngineOptions,
    scaling_quality: ScalingQuality,
) {
    let engine_options = unsafe_mut(ptr);
    engine_options.scaling_quality = scaling_quality
}

/// Gets `EngineOptions.start_in_debug_mode`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldStartInDebugMode(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.start_in_debug_mode
}

/// Gets `EngineOptions.start_without_sound`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldStartWithoutSound(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.start_without_sound
}

/// Sets `EngineOptions.start_without_sound`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_setStartWithoutSound(ptr: *mut EngineOptions, val: bool) {
    let engine_options = unsafe_mut(ptr);
    engine_options.start_without_sound = val
}

/// Gets `EngineOptions.run_enum_gen`.
#[unsafe(no_mangle)]
pub extern "C" fn EngineOptions_shouldRunEnumGen(ptr: *const EngineOptions) -> bool {
    let engine_options = unsafe_ref(ptr);
    engine_options.run_enum_gen
}

/// Gets the string representation of the `ScalingQuality` value.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn ScalingQuality_toString(quality: ScalingQuality) -> *mut c_char {
    let c_string = c_string_from_str(&quality.to_string());
    c_string.into_raw()
}

/// Gets the string represntation of the `VanillaVersion` value.
/// The caller is responsible for the returned memory.
#[unsafe(no_mangle)]
pub extern "C" fn VanillaVersion_toString(version: VanillaVersion) -> *mut c_char {
    let c_string = c_string_from_str(&version.to_string());
    c_string.into_raw()
}

#[cfg(test)]
mod tests {
    use std::fs;

    use pecel_bakwan::config::{EngineOptions, Resolution};
    use tempfile::TempDir;

    use crate::c::common::*;
    use crate::c::config::*;
    use crate::c::misc::CString_destroy;

    fn write_temp_folder_with_ja2_json(contents: &[u8]) -> TempDir {
        let dir = TempDir::new().unwrap();
        let ja2_home_dir = dir.path().join(".ja2");
        let file_path = ja2_home_dir.join("ja2.json");

        fs::create_dir(ja2_home_dir).unwrap();
        fs::write(file_path, contents).unwrap();

        dir
    }

    #[test]
    fn write_engine_options_should_write_a_json_file_that_can_be_serialized_again() {
        let mut engine_options = EngineOptions::default();
        let temp_dir = write_temp_folder_with_ja2_json(b"Invalid JSON");
        let pecel_bakwan_home = temp_dir.path().join(".ja2");

        engine_options.pecel_bakwan_home = pecel_bakwan_home;
        engine_options.resolution = Resolution(100, 100);

        assert!(EngineOptions_write(&mut engine_options));

        let mut got_engine_options = EngineOptions::default();
        Ja2Json::from_pecel_bakwan_home(&engine_options.pecel_bakwan_home)
            .apply_to_engine_options(&mut got_engine_options)
            .unwrap();

        assert_eq!(got_engine_options.resolution, engine_options.resolution);
    }

    #[test]
    fn write_engine_options_should_write_a_pretty_json_file() {
        let mut engine_options = EngineOptions::default();
        let temp_dir = write_temp_folder_with_ja2_json(b"Invalid JSON");
        let pecel_bakwan_home = temp_dir.path().join(".ja2");
        let pecel_bakwan_json = temp_dir.path().join(".ja2/ja2.json");

        engine_options.pecel_bakwan_home = pecel_bakwan_home;
        engine_options.resolution = Resolution(100, 100);

        EngineOptions_write(&mut engine_options);

        let config_file_contents = fs::read_to_string(pecel_bakwan_json).unwrap();

        assert_eq!(
            config_file_contents,
            r##"{
  "game_dir": "",
  "save_game_dir": "",
  "mods": [],
  "res": "100x100",
  "brightness": -1.0,
  "resversion": "ENGLISH",
  "ui_scale": "auto",
  "world_zoom": "match_ui",
  "window_mode": "BorderlessDesktop",
  "scaling": "PERFECT",
  "debug": false,
  "nosound": false
}"##
        );
    }

    #[test]
    fn vanilla_version_to_string_should_return_the_correct_resource_version_string() {
        macro_rules! t {
            ($version:expr, $expected:expr) => {
                let got = VanillaVersion_toString($version);
                assert_eq!(str_from_c_str_or_panic(unsafe_c_str(got)), $expected);
                CString_destroy(got);
            };
        }
        t!(VanillaVersion::DUTCH, "Dutch");
        t!(VanillaVersion::ENGLISH, "English");
        t!(VanillaVersion::FRENCH, "French");
        t!(VanillaVersion::GERMAN, "German");
        t!(VanillaVersion::ITALIAN, "Italian");
        t!(VanillaVersion::POLISH, "Polish");
        t!(VanillaVersion::RUSSIAN, "Russian");
        t!(VanillaVersion::RUSSIAN_GOLD, "Russian (Gold)");
        t!(VanillaVersion::SIMPLIFIED_CHINESE, "Simplified Chinese");
    }
}
