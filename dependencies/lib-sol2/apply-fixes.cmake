# \file dependencies/lib-sol2/apply-fixes.cmake
#
# The two local sol2 fixes, applied without an external patch tool:
#
#   cmake -DSOL2_SRC=<extracted sol2> -P apply-fixes.cmake
#
# This used to be a PATCH_COMMAND running `patch` on two .patch files. The sol2 3.5.0 release zip
# ships three of those files with CRLF line endings, so their hunks failed with "different line
# endings" while the others applied - and because lib-sol2/CMakeLists.txt never checked the
# result of its execute_process, sol2 stayed half patched and the build only failed much later
# (here: `#error "unsupported Lua version"` with Lua 5.5).
#
# The replacements below are exactly what the two patch files did to the headers sol2 compiles
# against; sol2's own cmake files are not used by this project and are left alone. Every fix is
# idempotent: a file is only rewritten while the fix is still missing, and a fix that is neither
# applicable nor already present is a hard error.

# fix(<relative path> <text to replace> <replacement> [<already-applied marker>])
#
# The marker proves the fix is already in place when the original text is gone; it defaults to
# the replacement, and is only needed where the replacement's whitespace could differ.
function(fix relpath old new)
    set(marker "${new}")
    if(ARGN)
        list(GET ARGN 0 marker)
    endif()
    set(path "${SOL2_SRC}/${relpath}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "sol2 fix ${relpath}: file not found in ${SOL2_SRC}")
    endif()

    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}") # the release zip mixes CRLF and LF

    string(LENGTH "${old}" old_length)
    string(REPLACE "${old}" "" without "${text}")
    string(LENGTH "${without}" without_length)
    string(LENGTH "${text}" text_length)
    math(EXPR occurrences "((${text_length} - ${without_length}) / ${old_length})")

    if(occurrences EQUAL 1)
        string(REPLACE "${old}" "${new}" text "${text}")
        file(WRITE "${path}" "${text}")
        message(STATUS "sol2 fix ${relpath}: applied")
    elseif(occurrences EQUAL 0)
        string(FIND "${text}" "${marker}" already)
        if(already LESS 0)
            message(FATAL_ERROR "sol2 fix ${relpath}: neither the original nor the fixed text is present")
        endif()
    else()
        message(FATAL_ERROR "sol2 fix ${relpath}: the text to replace is not unique (${occurrences} matches)")
    endif()
endfunction()

if(NOT SOL2_SRC)
    message(FATAL_ERROR "SOL2_SRC is not set")
endif()

# gcc-15.patch: sol2's range end() shadowing and a missing const char* overload
fix("include/sol/usertype_container.hpp"
    "auto& end = i.end();"
    "auto& end = i.sen();")
fix("include/sol/stack_field.hpp"
    "lua_getfield(L, tableindex, &key[0]);"
    "if constexpr (std::is_same_v<std::decay_t<Key>, const char*>) {
							if (key != nullptr) {
								lua_getfield(L, tableindex, key);
							} else {
								push(L, lua_nil);
							}
						} else {
							lua_getfield(L, tableindex, key.c_str());
						}"
    "lua_getfield(L, tableindex, key.c_str());")

# lua-55.patch: accept Lua 5.5 (the headers sol2 rejects at preprocessing time otherwise)
fix("include/sol/compatibility/compat-5.3.h"
    "#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM < 501 || LUA_VERSION_NUM > 504"
    "#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM < 501 || LUA_VERSION_NUM > 505")
fix("include/sol/compatibility/compat-5.3.h"
    "#  error \"unsupported Lua version (i.e. not Lua 5.1, 5.2, 5.3, or 5.4)\""
    "#  error \"unsupported Lua version (i.e. not Lua 5.1, 5.2, 5.3, 5.4, or 5.5)\"")
fix("include/sol/compatibility/compat-5.3.h"
    "#endif /* other Lua versions except 5.1, 5.2, 5.3, and 5.4 */"
    "#endif /* other Lua versions except 5.1, 5.2, 5.3, 5.4, and 5.5 */")
fix("include/sol/compatibility/compat-5.4.h"
    "#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 504"
    "#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 504")
fix("include/sol/compatibility/compat-5.4.h"
    "/* So Lua 5.4 actually removes this, which breaks sol2..."
    "/* So Lua 5.4 or later actually removes this, which breaks sol2...")
fix("include/sol/compatibility/compat-5.4.h"
    "#endif // Lua 5.4 only"
    "#endif // Lua 5.4 or later")

# lua-55.patch: Lua 5.5 passes a seed to lua_newstate
fix("include/sol/state.hpp"
    "unique_base(lua_newstate(alfunc, alpointer))"
    "unique_base(sol_lua_newstate(alfunc, alpointer))")
fix("include/sol/state.hpp"
    "#include <sol/thread.hpp>

namespace sol {"
    "#include <sol/thread.hpp>

inline lua_State* sol_lua_newstate(lua_Alloc f, void* ud, [[maybe_unused]] unsigned seed = 0) {
#if LUA_VERSION_NUM >= 505
	return ::lua_newstate(f, ud, seed);
#else
	return ::lua_newstate(f, ud);
#endif
}

namespace sol {"
    "inline lua_State* sol_lua_newstate(lua_Alloc f, void* ud, [[maybe_unused]] unsigned seed = 0) {")
