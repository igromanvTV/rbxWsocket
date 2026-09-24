#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace WebSocket {
	class exploit_websocket {
	public:
		lua_State* th = nullptr;
		unsigned int sock = 0xFFFFFFFFu;
		bool secure = false;
		void* hSession = nullptr;
		void* hConnect = nullptr;
		void* hRequest = nullptr;
		void* hWS = nullptr;
		std::thread recvThread;
		std::atomic<bool> running = false;
		std::atomic<bool> connected = false;

		int onMessageRef = 0;
		int onCloseRef = 0;
		int threadRef = 0;

		exploit_websocket() = default;
		~exploit_websocket();

		void pollMessages();
		void fireMessage(const std::string& message, bool is_binary = false);
		void fireClose();
		bool do_connect(const std::string& url);
	};

	struct SpinLock {
		std::atomic_flag flag = ATOMIC_FLAG_INIT;

		void lock() {
			while (flag.test_and_set(std::memory_order_acquire)) {
			}
		}

		void unlock() {
			flag.clear(std::memory_order_release);
		}
	};

	static SpinLock luaLock;

	class SpinGuard {
		SpinLock& l;
	public:
		explicit SpinGuard(SpinLock& lk) : l(lk) {
			l.lock();
		}
		~SpinGuard() {
			l.unlock();
		}
	};

	exploit_websocket::~exploit_websocket() {
		running = false;
		if (recvThread.joinable()) recvThread.join();
		if (secure && hWS) {
			WinHttpWebSocketClose(static_cast<HINTERNET>(hWS), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
			WinHttpCloseHandle(static_cast<HINTERNET>(hWS));
			WinHttpCloseHandle(static_cast<HINTERNET>(hRequest));
			WinHttpCloseHandle(static_cast<HINTERNET>(hConnect));
			WinHttpCloseHandle(static_cast<HINTERNET>(hSession));
			hWS = hRequest = hConnect = hSession = nullptr;
		}
		if (sock != 0xFFFFFFFFu) {
			closesocket(sock);
			sock = 0xFFFFFFFFu;
		}
	}

	static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	static std::string b64enc(const unsigned char* data, int size) {
		std::string out;
		int val = 0;
		int valb = -6;

		for (int i = 0; i < size; ++i) {
			val = (val << 8) + data[i];
			valb += 8;

			while (valb >= 0) {
				out.push_back(b64tab[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}

		if (valb > -6) {
			out.push_back(b64tab[((val << 8) >> (valb + 8)) & 0x3F]);
		}

		while (out.size() % 4) {
			out.push_back('=');
		}

		return out;
	}

	static std::string gen_key() {
		unsigned char raw[16];
		for (int i = 0; i < 16; ++i) {
			raw[i] = static_cast<unsigned char>(rand() % 256);
		}
		return b64enc(raw, 16);
	}

	void exploit_websocket::pollMessages() {
		while (running) {
			unsigned char h[2];
			int rcv = recv(sock, (char*)h, 2, MSG_WAITALL);
			if (rcv != 2) break;
			int opcode = h[0] & 0x0F;
			bool is_binary = (opcode == 0x02);
			uint64_t len = h[1] & 0x7F;
			if (len == 126) {
				unsigned char ext[2];
				if (recv(sock, reinterpret_cast<char*>(ext), 2, MSG_WAITALL) != 2) break;
				len = (ext[0] << 8) | ext[1];
			}
			else if (len == 127) {
				unsigned char ext[8];
				if (recv(sock, reinterpret_cast<char*>(ext), 8, MSG_WAITALL) != 8) break;
				len = 0;
				for (int i = 0; i < 8; ++i) {
					len = (len << 8) | ext[i];
				}
			}
			if (h[1] & 0x80) {
				unsigned char mask[4];
				if (recv(sock, reinterpret_cast<char*>(mask), 4, MSG_WAITALL) != 4) break;
			}
			std::string payload(len, '\0');
			if (len > 0) {
				int got = recv(sock, payload.data(), static_cast<int>(len), MSG_WAITALL);
				if (got != static_cast<int>(len)) break;
			}
			if (opcode == 0x01) {
				fireMessage(payload, false);
			}
			else if (opcode == 0x02) {
				fireMessage(payload, true);
			}
			else if (opcode == 8) { break; }
		}
		fireClose();
	}

	void exploit_websocket::fireMessage(const std::string& message, bool is_binary) {
		if (!connected || !th) {
			return;
		}

		SpinGuard guard(luaLock);

		lua_getref(th, onMessageRef);
		lua_getfield(th, -1, xorstr_("Fire"));
		if (!lua_isfunction(th, -1)) {
			lua_settop(th, 0);
			return;
		}
		lua_getref(th, onMessageRef);

		lua_pushlstring(th, message.c_str(), message.size());
		lua_pushboolean(th, is_binary);

		if (lua_pcall(th, 3, 0, 0) != LUA_OK) {
			lua_settop(th, 0);
			return;
		}

		lua_settop(th, 0);
	}

	void exploit_websocket::fireClose() {
		if (!connected || !th) {
			return;
		}
		connected = false;

		SpinGuard guard(luaLock);

		lua_getref(th, onCloseRef);
		lua_getfield(th, -1, xorstr_("Fire"));
		lua_getref(th, onCloseRef);
		if (lua_pcall(th, 1, 0, 0) != LUA_OK) {
			luaL_error(th, lua_tostring(th, -1));
			return;
		}
		lua_settop(th, 0);

		lua_unref(th, onMessageRef);
		lua_unref(th, onCloseRef);
		lua_unref(th, threadRef);
		if (sock != 0xFFFFFFFFu) { closesocket(sock); sock = 0xFFFFFFFFu; }
	}

	bool exploit_websocket::do_connect(const std::string& url) {
		std::string u = url;
		if (u.rfind(("wss://"), 0) == 0) {
			secure = true;
			u = u.substr(6);
		}
		else secure = false;
		if (u.rfind(("ws://"), 0) != 0 && !secure) return false;
		if (!secure) u = u.substr(5);
		std::string hostPath = u;
		std::string path = "/";
		auto s = hostPath.find('/');
		if (s != std::string::npos) { path = hostPath.substr(s); hostPath = hostPath.substr(0, s); }
		std::string host = hostPath;
		std::string portStr = secure ? "443" : "80";
		auto c = host.find(':');
		if (c != std::string::npos) { portStr = host.substr(c + 1); host = host.substr(0, c); }
		if (secure) {
			std::wstring hostW(host.begin(), host.end());
			std::wstring pathW(path.begin(), path.end());
			hSession = WinHttpOpen(L"ws", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
			if (!hSession)
				return false;

			hConnect = WinHttpConnect(
				static_cast<HINTERNET>(hSession),
				hostW.c_str(),
				static_cast<INTERNET_PORT>(std::stoi(portStr)),
				0);
			if (!hConnect) {
				WinHttpCloseHandle(static_cast<HINTERNET>(hSession));
				return false;
			}

			hRequest = WinHttpOpenRequest(
				static_cast<HINTERNET>(hConnect),
				L"GET",
				pathW.c_str(),
				nullptr,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE);

			if (!hRequest) {
				WinHttpCloseHandle(static_cast<HINTERNET>(hConnect));
				WinHttpCloseHandle(static_cast<HINTERNET>(hSession));
				return false;
			}

			WinHttpSetOption(
				static_cast<HINTERNET>(hRequest),
				WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
				nullptr,
				0);

			bool sent = WinHttpSendRequest(
				static_cast<HINTERNET>(hRequest),
				nullptr,
				0,
				nullptr,
				0,
				0,
				0) != FALSE;

			bool responded = sent &&
				WinHttpReceiveResponse(static_cast<HINTERNET>(hRequest), nullptr) != FALSE;

			if (!responded) {
				WinHttpCloseHandle(static_cast<HINTERNET>(hRequest));
				WinHttpCloseHandle(static_cast<HINTERNET>(hConnect));
				WinHttpCloseHandle(static_cast<HINTERNET>(hSession));
				return false;
			}

			hWS = WinHttpWebSocketCompleteUpgrade(static_cast<HINTERNET>(hRequest), 0);
			if (!hWS) {
				WinHttpCloseHandle(static_cast<HINTERNET>(hRequest));
				WinHttpCloseHandle(static_cast<HINTERNET>(hConnect));
				WinHttpCloseHandle(static_cast<HINTERNET>(hSession));
				return false;
			}

			connected = true;
			running = true;

			recvThread = std::thread([this]() {
				BYTE buf[4096];
				DWORD bytesRead = 0;
				WINHTTP_WEB_SOCKET_BUFFER_TYPE frameType;
				while (running) {
					if (WinHttpWebSocketReceive(static_cast<HINTERNET>(hWS), buf, sizeof(buf), &bytesRead, &frameType) != NO_ERROR)
						break;
					if (frameType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
						break;
					//if (frameType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
					//	continue;

					if (frameType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
						std::string msg(reinterpret_cast<char*>(buf), bytesRead);
						fireMessage(msg, true);
					}
					else if (frameType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
						std::string msg(reinterpret_cast<char*>(buf), bytesRead);
						fireMessage(msg, false);
					}
				}
				fireClose();
				});
			return true;
		}

		static std::atomic<bool> init = false;
		bool e = false;
		if (init.compare_exchange_strong(e, true)) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false; }
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET) return false;
		sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((u_short)std::stoi(portStr));
		if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
			hostent* he = gethostbyname(host.c_str());
			if (!he) {
				closesocket(sock);
				sock = INVALID_SOCKET;
				return false;
			}
			addr.sin_addr = *reinterpret_cast<in_addr*>(he->h_addr);
		}
		if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			closesocket(sock);
			sock = INVALID_SOCKET;
			return false;
		}
		std::string key = gen_key();
		std::string req =
			"GET " + path + " HTTP/1.1\r\n" +
			"Host: " + host + ":" + portStr + "\r\n" +
			"Upgrade: websocket\r\n" +
			"Connection: Upgrade\r\n" +
			"Sec-WebSocket-Version: 13\r\n" +
			"Sec-WebSocket-Key: " + key + "\r\n\r\n";
		send(sock, req.c_str(), (int)req.size(), 0);
		char buffer[1024];
		std::string resp;
		int n;
		while (resp.find("\r\n\r\n") == std::string::npos) {
			n = recv(sock, buffer, 1024, 0);
			if (n <= 0) {
				closesocket(sock);
				sock = INVALID_SOCKET;
				return false;
			}
			resp.append(buffer, n);
			if (resp.size() > 4096) {
				closesocket(sock);
				sock = INVALID_SOCKET;
				return false;
			}
		}
		if (resp.find(" 101 ") == std::string::npos) { closesocket(sock); sock = INVALID_SOCKET; return false; }
		connected = true;
		running = true;
		recvThread = std::thread(&exploit_websocket::pollMessages, this);
		return true;
	}

	static int websocket_send(lua_State* L) {
		exploit_websocket* w = reinterpret_cast<exploit_websocket*>(lua_touserdata(L, lua_upvalueindex(1)));
		std::string d = luaL_checkstring(L, 2);
		bool is_binary = lua_toboolean(L, 3);

		if (!w) return 0;

		if (w->secure && w->hWS) {
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type = is_binary ?
				WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE :
				WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
			WinHttpWebSocketSend(static_cast<HINTERNET>(w->hWS), type, (void*)d.data(), (DWORD)d.size());
			return 0;
		}

		if (w->connected && w->sock != INVALID_SOCKET) {
			size_t n = d.size();
			std::string f;
			f.push_back(0x80 | (is_binary ? 0x02 : 0x01));

			if (n < 126) {
				f.push_back(char(0x80 | n));
			}
			else if (n <= 65535) {
				f.push_back(char(0x80 | 126));
				f.push_back(char((n >> 8) & 0xFF));
				f.push_back(char(n & 0xFF));
			}
			else {
				f.push_back(char(0x80 | 127));
				for (int i = 7; i >= 0; --i) {
					f.push_back(char((n >> (8 * i)) & 0xFF));
				}
			}

			unsigned char mask[4];
			for (int i = 0; i < 4; ++i) {
				mask[i] = static_cast<unsigned char>(rand() % 256);
			}
			f.append(reinterpret_cast<char*>(mask), 4);

			for (size_t i = 0; i < n; ++i) {
				f.push_back(d[i] ^ mask[i % 4]);
			}
			send(w->sock, f.data(), (int)f.size(), 0);
		}
		return 0;
	}

	static int websocket_close(lua_State* L) {
		exploit_websocket* w = reinterpret_cast<exploit_websocket*>(lua_touserdata(L, lua_upvalueindex(1)));
		if (w) {
			if (w->secure && w->hWS) {
				WinHttpWebSocketShutdown(static_cast<HINTERNET>(w->hWS), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
				WinHttpWebSocketClose(static_cast<HINTERNET>(w->hWS), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
				WinHttpCloseHandle(static_cast<HINTERNET>(w->hWS));
				WinHttpCloseHandle(static_cast<HINTERNET>(w->hRequest));
				WinHttpCloseHandle(static_cast<HINTERNET>(w->hConnect));
				WinHttpCloseHandle(static_cast<HINTERNET>(w->hSession));
				w->hWS = w->hRequest = w->hConnect = w->hSession = nullptr;
			}
			if (w->sock != INVALID_SOCKET) {
				unsigned char cf[2] = { char(0x88), char(0x00) };
				send(w->sock, reinterpret_cast<char*>(cf), 2, 0);
				closesocket(w->sock);
				w->sock = INVALID_SOCKET;
			}
			w->fireClose();
		}
		return 0;
	}

	static int websocket_gc(lua_State* L) {
		exploit_websocket* ws = reinterpret_cast<exploit_websocket*>(lua_touserdata(L, 1));
		ws->~exploit_websocket();
		return 0;
	}

	static int websocket_index(lua_State* L) {
		exploit_websocket* ws = reinterpret_cast<exploit_websocket*>(lua_touserdata(L, 1));
		std::string idx = luaL_checkstring(L, 2);

		if (idx == xorstr_("OnMessage")) {
			lua_getref(L, ws->onMessageRef);
			lua_getfield(L, -1, xorstr_("Event"));
			return 1;
		}
		else if (idx == xorstr_("OnClose")) {
			lua_getref(L, ws->onCloseRef);
			lua_getfield(L, -1, xorstr_("Event"));
			return 1;
		}
		else if (idx == xorstr_("Send")) {
			lua_pushlightuserdata(L, ws);
			lua_pushcclosure(L, websocket_send, xorstr_("websocket_send"), 1);
			return 1;
		}
		else if (idx == xorstr_("Close")) {
			lua_pushlightuserdata(L, ws);
			lua_pushcclosure(L, websocket_close, xorstr_("websocket_close"), 1);
			return 1;
		}
		return 0;
	}

	int connect(lua_State* ls) {
		luaL_checktype(ls, 1, LUA_TSTRING);
		std::string url = luaL_checkstring(ls, 1);

		exploit_websocket* ws = new (lua_newuserdata(ls, sizeof(exploit_websocket))) exploit_websocket();

		ws->th = lua_newthread(ls);
		ws->threadRef = lua_ref(ls, -1);
		lua_pop(ls, 1);

		if (!ws->do_connect(url)) {
			luaL_error(ls, xorstr_("Failed to connect to WebSocket"));
			return 0;
		}

		ws->connected = true;

		lua_getglobal(ls, xorstr_("Instance"));
		lua_getfield(ls, -1, xorstr_("new"));
		lua_pushstring(ls, xorstr_("BindableEvent"));
		lua_pcall(ls, 1, 1, 0);
		ws->onMessageRef = lua_ref(ls, -1);
		lua_pop(ls, 2);

		lua_getglobal(ls, xorstr_("Instance"));
		lua_getfield(ls, -1, xorstr_("new"));
		lua_pushstring(ls, xorstr_("BindableEvent"));
		lua_pcall(ls, 1, 1, 0);
		ws->onCloseRef = lua_ref(ls, -1);
		lua_pop(ls, 2);

		lua_newtable(ls);

		lua_pushstring(ls, xorstr_("__index"));
		lua_pushcclosure(ls, websocket_index, xorstr_("websocket_index"), 0);
		lua_settable(ls, -3);

		lua_pushstring(ls, xorstr_("__gc"));
		lua_pushcclosure(ls, websocket_gc, xorstr_("websocket_gc"), 0);
		lua_settable(ls, -3);

		lua_setmetatable(ls, -2);

		return 1;
	}
}
