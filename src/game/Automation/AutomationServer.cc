#include "AutomationLua.h"
#include "Automation.h"
#include "AutomationSession.h"

#include "Json.h"
#include "SGP.h"
#include "Logger.h"

#include <SDL3/SDL_events.h>
#include <string_theory/format>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <process.h>
	using socket_t = SOCKET;
#	define CLOSE_SOCKET closesocket
#	define GET_PID _getpid
#else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/select.h>
#	include <sys/socket.h>
#	include <unistd.h>
	using socket_t = int;
#	define INVALID_SOCKET (-1)
#	define CLOSE_SOCKET close
#	define GET_PID getpid
#endif

/* Protocol: one JSON object per line in each direction.
 *
 *   {"id": 1, "lua": "return ja2.screen()"}          run a Lua chunk
 *   {"id": 2, "call": "click", "args": ["Load Game"]} call ja2.<name>(args...)
 *   {"id": 3, "call": "shutdown"}                     end the session
 *
 * Every response carries the request id, "ok", and the game's position:
 *
 *   {"id": 1, "ok": true, "result": "MAP_SCREEN", "screen": "MAP_SCREEN", "frame": 812, "ms": 13533}
 *   {"id": 2, "ok": false, "error": "timed out ...", "kind": "timeout", ...}
 *
 * Between requests the game does not advance: time only moves while a request
 * steps it. */

namespace Automation
{

namespace
{
	void AppendEscaped(std::string& out, std::string const& s)
	{
		out += '"';
		for (unsigned char const c : s)
		{
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n";  break;
				case '\r': out += "\\r";  break;
				case '\t': out += "\\t";  break;
				default:
					if (c < 0x20)
					{
						out += ST::format("\\u{04x}", static_cast<unsigned>(c)).to_std_string();
					}
					else out += static_cast<char>(c);
			}
		}
		out += '"';
	}

	void AppendJson(std::string& out, sol::object const& o, int depth = 0)
	{
		if (depth > 32) { out += "null"; return; }
		switch (o.get_type())
		{
			case sol::type::lua_nil:
			case sol::type::none:
				out += "null";
				break;
			case sol::type::boolean:
				out += o.as<bool>() ? "true" : "false";
				break;
			case sol::type::number:
			{
				double const d = o.as<double>();
				if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9e15) out += std::to_string(static_cast<long long>(d));
				else if (std::isfinite(d)) out += std::to_string(d);
				else out += "null";
				break;
			}
			case sol::type::string:
				AppendEscaped(out, o.as<std::string>());
				break;
			case sol::type::table:
			{
				sol::table const t = o.as<sol::table>();
				size_t const n = t.size();
				size_t count = 0;
				for (auto const& kv : t) { (void)kv; ++count; }
				if (count == n)
				{
					// Array (including the empty table).
					out += '[';
					for (size_t i = 1; i <= n; ++i)
					{
						if (i > 1) out += ',';
						AppendJson(out, t[i], depth + 1);
					}
					out += ']';
				}
				else
				{
					out += '{';
					bool first = true;
					for (auto const& kv : t)
					{
						if (!first) out += ',';
						first = false;
						std::string key = kv.first.is<std::string>() ? kv.first.as<std::string>()
							: Lua()["tostring"](kv.first).get<std::string>();
						AppendEscaped(out, key);
						out += ':';
						AppendJson(out, kv.second, depth + 1);
					}
					out += '}';
				}
				break;
			}
			default:
				AppendEscaped(out, Lua()["tostring"](o).get<std::string>());
				break;
		}
	}

	sol::object ToLua(JsonValue const& v)
	{
		sol::state& L = Lua();
		if (v.isBool())   return sol::make_object(L, v.toBool());
		if (v.isInt())    return sol::make_object(L, v.toInt());
		if (v.isUInt())   return sol::make_object(L, v.toUInt());
		if (v.isDouble()) return sol::make_object(L, v.toDouble());
		if (v.isString()) return sol::make_object(L, v.toString().to_std_string());
		if (v.isVec())
		{
			sol::table t = L.create_table();
			int i = 1;
			for (auto const& e : v.toVec()) t[i++] = ToLua(e);
			return t;
		}
		if (v.isObject())
		{
			sol::table t = L.create_table();
			JsonObject const obj = v.toObject();
			for (auto const& k : obj.keys()) t[k.to_std_string()] = ToLua(obj[k.c_str()]);
			return t;
		}
		return sol::lua_nil;
	}

	char const* KindName(FailureKind const k)
	{
		switch (k)
		{
			case FailureKind::Expectation: return "expectation";
			case FailureKind::Timeout:     return "timeout";
			case FailureKind::Crash:       return "crash";
			case FailureKind::Exited:      return "exited";
			default:                       return "script";
		}
	}

	struct Reply
	{
		std::string json;
		bool shutdown = false;
	};

	// Run a {"lua": ...} or {"call": ..., "args": [...]} request.
	sol::protected_function_result Execute(JsonObject const& obj)
	{
		if (obj.has("lua"))
		{
			std::string const chunk = obj.GetString("lua").to_std_string();
			return Lua().safe_script(chunk, sol::script_pass_on_error, "=request");
		}
		if (!obj.has("call")) throw std::invalid_argument("request needs \"lua\" or \"call\"");

		std::string const name = obj.GetString("call").to_std_string();
		sol::object fn = Lua()["ja2"][name];
		if (fn.get_type() != sol::type::function) throw std::invalid_argument("no such function: ja2." + name);
		std::vector<sol::object> args;
		if (obj.has("args"))
		{
			JsonValue const a = obj["args"];
			if (!a.isVec()) throw std::invalid_argument("args must be an array");
			for (auto const& v : a.toVec()) args.push_back(ToLua(v));
		}
		sol::protected_function pf = fn;
		return pf(sol::as_args(args));
	}

	// null, the single value, or an array of values.
	void AppendResult(std::string& out, sol::protected_function_result const& r)
	{
		int const n = r.return_count();
		if (n == 0) { out += "null"; return; }
		if (n == 1) { AppendJson(out, r.get<sol::object>()); return; }
		out += '[';
		for (int i = 0; i < n; ++i)
		{
			if (i) out += ',';
			AppendJson(out, r.get<sol::object>(i));
		}
		out += ']';
	}

	Reply Handle(std::string const& line)
	{
		std::string id = "null";
		std::string body;
		bool ok = false;
		bool shutdown = false;
		try
		{
			JsonValue const req = JsonValue::deserialize(line);
			if (!req.isObject()) throw std::invalid_argument("request must be a JSON object");
			JsonObject const obj = req.toObject();
			if (obj.has("id")) id = obj["id"].serialize().to_std_string();

			if (obj.has("call") && obj.GetString("call") == "shutdown")
			{
				shutdown = true;
				ok = true;
				body = "\"result\":true";
			}
			else
			{
				sol::protected_function_result const r = Execute(obj);
				if (!r.valid())
				{
					sol::error const err = r;
					FailureKind const kind = LastFailure() == FailureKind::None ? FailureKind::Script : LastFailure();
					body = "\"error\":";
					AppendEscaped(body, err.what());
					body += ",\"kind\":\"";
					body += KindName(kind);
					body += '"';
				}
				else
				{
					ok = true;
					body = "\"result\":";
					AppendResult(body, r);
				}
			}
		}
		catch (std::exception const& e)
		{
			body = "\"error\":";
			AppendEscaped(body, e.what());
			body += ",\"kind\":\"script\"";
		}

		std::string out = "{\"id\":" + id + ",\"ok\":" + (ok ? "true" : "false") + "," + body;
		out += ",\"screen\":";
		AppendEscaped(out, Session::ScreenName());
		out += ",\"frame\":" + std::to_string(Session::Frame());
		out += ",\"ms\":" + std::to_string(Session::ElapsedMs());
		out += ",\"idle\":";
		out += Session::IsIdle() ? "true" : "false";
		out += "}\n";
		return { out, shutdown };
	}

	bool SendAll(socket_t const s, std::string const& data)
	{
		size_t sent = 0;
		while (sent < data.size())
		{
			int const n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
			if (n <= 0) return false;
			sent += n;
		}
		return true;
	}

	// Wait for a socket to become readable, keeping a window responsive meanwhile.
	bool WaitReadable(socket_t const s)
	{
		for (;;)
		{
			fd_set set;
			FD_ZERO(&set);
			FD_SET(s, &set);
			timeval tv{ 0, 100'000 };
			int const n = select(static_cast<int>(s + 1), &set, nullptr, nullptr, &tv);
			if (n > 0) return true;
			if (n < 0) return false;
			if (!GetOptions().Headless()) SDL_PumpEvents();
		}
	}
}


int RunServer(std::string const& port, std::string const& sessionFile)
{
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		std::fprintf(stderr, "automation: WSAStartup failed\n");
		return EXIT_SCRIPT_ERROR;
	}
#endif
	socket_t const listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
	{
		std::fprintf(stderr, "automation: cannot create socket\n");
		return EXIT_SCRIPT_ERROR;
	}

	sockaddr_in addr{};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // never reachable from other machines
	addr.sin_port        = htons(static_cast<uint16_t>(std::stoi(port)));
	if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listener, 4) != 0)
	{
		std::fprintf(stderr, "automation: cannot listen on 127.0.0.1:%s\n", port.c_str());
		CLOSE_SOCKET(listener);
		return EXIT_SCRIPT_ERROR;
	}
	socklen_t len = sizeof(addr);
	getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
	int const actualPort = ntohs(addr.sin_port);

	std::string const info = ST::format("{{\"port\":{},\"pid\":{}}}\n", actualPort, GET_PID()).to_std_string();
	if (!sessionFile.empty())
	{
		// Write then rename, so a client polling for the file never reads half of it.
		std::string const tmp = sessionFile + ".tmp";
		{
			std::ofstream f(tmp, std::ios::trunc);
			f << info;
		}
		std::remove(sessionFile.c_str());
		std::rename(tmp.c_str(), sessionFile.c_str());
	}
	std::printf("%s", info.c_str());
	std::fflush(stdout);
	SLOGI("[automation] serving on 127.0.0.1:{}", actualPort);

	bool running = true;
	while (running)
	{
		if (!WaitReadable(listener)) break;
		socket_t const client = accept(listener, nullptr, nullptr);
		if (client == INVALID_SOCKET) continue;

		std::string buffer;
		char chunk[4096];
		while (running)
		{
			size_t nl;
			while (running && (nl = buffer.find('\n')) != std::string::npos)
			{
				std::string const line = buffer.substr(0, nl);
				buffer.erase(0, nl + 1);
				if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
				Reply const reply = Handle(line);
				SendAll(client, reply.json);
				if (reply.shutdown || Session::Crashed() || sgp::QuitRequested()) running = false;
			}
			if (!running) break;
			if (!WaitReadable(client)) break;
			int const n = recv(client, chunk, sizeof(chunk), 0);
			if (n <= 0) break;
			buffer.append(chunk, n);
		}
		CLOSE_SOCKET(client);
	}

	CLOSE_SOCKET(listener);
	if (!sessionFile.empty()) std::remove(sessionFile.c_str());
#ifdef _WIN32
	WSACleanup();
#endif
	if (Session::Crashed()) return EXIT_GAME_ERROR;
	return EXIT_PASSED;
}

}
