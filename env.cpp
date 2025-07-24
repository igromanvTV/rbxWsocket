namespace WebSocket {
	class websocket_object {
	private:

		std::string url;
	public:
		rbxwsocket::WebSocket::pointer websocket_client = nullptr;
		lua_State* L;
		int L_ref;
		std::thread pollThread;

		std::atomic<bool> running = false;
		bool connected = false;
		bool closed;

		int on_message_ref;
		int on_close_ref;

		bool initialize(const std::string& url, int ref_1, int ref_2);

		void fireClose() {
			if (!connected || !L) {
				return;
			}

			connected = false;
			running = false;

			lua_getref(L, on_close_ref);
			lua_getfield(L, -1, "Fire");
			lua_getref(L, on_close_ref);
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				luaL_error(L, lua_tostring(L, -1));
				return;
			}
			lua_settop(L, 0);

			lua_unref(L, on_message_ref);
			lua_unref(L, on_close_ref);
			lua_unref(L, L_ref);
		}

		void fireMessage(const std::string& message) {
			if (!connected || !L) {
				return;
			}

			lua_getref(L, on_message_ref);
			lua_getfield(L, -1, "Fire");
			lua_getref(L, on_message_ref);
			lua_pushlstring(L, message.c_str(), message.size());

			if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
				lua_settop(L, 0);
				return;
			}

			lua_settop(L, 0);
		}

		void pollMessages() {
			while (running) {
				if (!websocket_client || websocket_client->getReadyState() != rbxwsocket::WebSocket::OPEN) {
					fireClose();
					break;
				}

				websocket_client->poll(10);
				websocket_client->dispatch([this](const std::string& message) {
					fireMessage(message);
					});

				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		int websocket_index(lua_State* L) {
			luaL_checktype(L, 1, LUA_TUSERDATA);
			luaL_checktype(L, 2, LUA_TSTRING);

			if (!L || !connected) return 0;

			void* userdata = lua_touserdata(L, 1);
			const std::string key = lua_tostring(L, 2);

			if (key == xorstr_("OnMessage")) {
				lua_getref(L, this->on_message_ref);
				lua_getfield(L, -1, xorstr_("Event"));
				return 1;
			}
			else if (key == xorstr_("OnClose")) {
				lua_getref(L, this->on_close_ref);
				lua_getfield(L, -1, xorstr_("Event"));
				return 1;
			}
			// METHODS
			else if (key == xorstr_("Send")) {
				lua_pushvalue(L, -10003);
				lua_pushcclosure(L,
					[](lua_State* L) -> int {
						if (!L) return 0;

						luaL_checktype(L, 1, LUA_TUSERDATA);
						std::string data = luaL_checkstring(L, 2);

						websocket_object* ws = reinterpret_cast<websocket_object*>(lua_touserdata(L, -10003));
						if (ws && ws->websocket_client && ws->connected) {
							ws->websocket_client->send(data);
						}
						return 0;
					}, "websocketinstance_send", 1);
				return 1;
			}
			else if (key == xorstr_("Close")) {
				lua_pushvalue(L, -10003);
				lua_pushcclosure(L,
					[](lua_State* L) -> int {
						if (!L) return 0;

						websocket_object* ws = reinterpret_cast<websocket_object*>(lua_touserdata(L, -10003));
						if (ws && ws->websocket_client) {
							ws->websocket_client->close();
							ws->fireClose();
						}
						return 0;
					}, "websocketinstance_close", 1);
				return 1;
			}

			return 0;
		}

	};

	int websocket_connect(lua_State* L) {

		luaL_checktype(L, 1, LUA_TSTRING);

		const std::string url = lua_tostring(L, 1);

		if ((url == xorstr_("ws://") || url == xorstr_("wss://")))
			luaL_argerror(L, 1, xorstr_("invalid protocol specified ('ws://' or 'wss://' expected)"));

		lua_getglobal(L, xorstr_("Instance"));
		lua_getfield(L, -1, xorstr_("new"));
		lua_pushstring(L, xorstr_("BindableEvent"));
		lua_call(L, 1, 1);
		if (lua_isnoneornil(L, -1))
			luaL_errorL(L, xorstr_("failed to create first reference"));
		int on_message = lua_ref(L, -1);
		lua_pop(L, 2);


		lua_getglobal(L, xorstr_("Instance"));
		lua_getfield(L, -1, xorstr_("new"));
		lua_pushstring(L, xorstr_("BindableEvent"));
		lua_call(L, 1, 1);
		if (lua_isnoneornil(L, -1))
			luaL_errorL(L, xorstr_("failed to create second reference"));
		int on_close = lua_ref(L, -1);
		lua_pop(L, 2);

		websocket_object* ws = (websocket_object*)lua_newuserdata(L, sizeof(websocket_object));
		new (ws) websocket_object{};

		//std::shared_ptr<websocket_object> websocket = std::make_shared<websocket_object>();
		bool success = ws->initialize(url, on_message, on_close);

		if (!success)
		{
			luaL_error(L, xorstr_("failed to connect to %s"), url.c_str());
			return 0;
		}
		
		ws->running = true;
		ws->connected = true;

		ws->L = lua_newthread(L);
		ws->L_ref = lua_ref(L, -1);
		lua_pop(L, 1);

		ws->pollThread = std::thread(&websocket_object::pollMessages, ws);

		lua_newtable(L);

		lua_pushstring(L, xorstr_("WebsocketObject"));
		lua_setfield(L, -2, xorstr_("__type"));
		lua_pushvalue(L, -2);
		lua_pushcclosure(L,
			[](lua_State* L) -> int {
				websocket_object* ws = reinterpret_cast<websocket_object*>(lua_touserdata(L, lua_upvalueindex(1)));
				return ws->websocket_index(L);
			},
			"__index", 1);
		lua_setfield(L, -2, xorstr_("__index"));

		lua_setmetatable(L, -2);

		return 1;
	}

	bool websocket_object::initialize(const std::string& _url, int ref_1, int ref_2) {
		this->on_message_ref = ref_1;
		this->on_close_ref = ref_2;


		
		constexpr int maxRetries = 5;
		for (int i = 0; i < maxRetries; ++i) {

			if (!_url.find("wss://"))
			{
				if (!TLS_rbxwsocket::TLSrbxwsocket::initTLS_Main(_url))
				{
					return false;
				}
			}

			websocket_client = rbxwsocket::WebSocket::from_url(_url);
			if (websocket_client && websocket_client->getReadyState() == rbxwsocket::WebSocket::OPEN) {
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		return false;
	}
}
