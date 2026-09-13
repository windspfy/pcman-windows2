#include "WinHttpWebsocket.h"

#include <winhttp.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr size_t kMaxMessageSize = 1024;
constexpr DWORD kReceiveBufferSize = 4096;

enum class WinHttpWebsocketState {
	NEW = 0,
	CONNECTING = 1,
	ESTABLISHED = 2,
	CLOSED = 3,
};

using CompleteUpgradeFn = HINTERNET(WINAPI *)(HINTERNET, DWORD_PTR);
using SendFn = DWORD(WINAPI *)(
	HINTERNET, WINHTTP_WEB_SOCKET_BUFFER_TYPE, PVOID, DWORD);
using ReceiveFn = DWORD(WINAPI *)(
	HINTERNET, PVOID, DWORD, DWORD*, WINHTTP_WEB_SOCKET_BUFFER_TYPE*);
using ShutdownFn = DWORD(WINAPI *)(HINTERNET, USHORT, PVOID, DWORD);
using QueryCloseStatusFn = DWORD(WINAPI *)(
	HINTERNET, USHORT*, PVOID, DWORD, DWORD*);

struct WinHttpWebsocketApi {
	WinHttpWebsocketApi()
	{
		module = LoadLibraryW(L"winhttp.dll");
		if (!module)
			return;

		complete_upgrade = reinterpret_cast<CompleteUpgradeFn>(
			GetProcAddress(module, "WinHttpWebSocketCompleteUpgrade"));
		send = reinterpret_cast<SendFn>(
			GetProcAddress(module, "WinHttpWebSocketSend"));
		receive = reinterpret_cast<ReceiveFn>(
			GetProcAddress(module, "WinHttpWebSocketReceive"));
		shutdown = reinterpret_cast<ShutdownFn>(
			GetProcAddress(module, "WinHttpWebSocketShutdown"));
		query_close_status = reinterpret_cast<QueryCloseStatusFn>(
			GetProcAddress(module, "WinHttpWebSocketQueryCloseStatus"));
	}

	bool IsAvailable() const
	{
		return complete_upgrade && send && receive && shutdown;
	}

	HMODULE module = nullptr;
	CompleteUpgradeFn complete_upgrade = nullptr;
	SendFn send = nullptr;
	ReceiveFn receive = nullptr;
	ShutdownFn shutdown = nullptr;
	QueryCloseStatusFn query_close_status = nullptr;
};

WinHttpWebsocketApi& GetWinHttpWebsocketApi()
{
	static WinHttpWebsocketApi api;
	return api;
}

std::wstring ToWide(const CString& value)
{
	if (value.IsEmpty())
		return std::wstring();

	int length = MultiByteToWideChar(
		CP_ACP, 0, value.GetString(), value.GetLength(), nullptr, 0);
	if (length <= 0)
		return std::wstring();

	std::wstring result(length, L'\0');
	MultiByteToWideChar(
		CP_ACP, 0, value.GetString(), value.GetLength(), &result[0], length);
	return result;
}

void LogWinHttpError(const char* operation, DWORD error)
{
	char message[160];
	sprintf_s(message, "%s failed with WinHTTP error %lu\r\n", operation, error);
	OutputDebugStringA(message);
}

class CWinHttpWebsocket : public CConnIO {
public:
	CWinHttpWebsocket(
		const CAddress& address,
		std::shared_ptr<CConnEventDelegate> delegate)
		: address_(address)
		, delegate_(std::move(delegate))
		, is_secure_(address.Protocol() == _T("wss"))
	{
	}

	~CWinHttpWebsocket() override
	{
		Close();
		if (connection_thread_.joinable())
			connection_thread_.join();
		if (send_thread_.joinable())
			send_thread_.join();
		CloseHandles();
	}

	bool Connect() override
	{
		WinHttpWebsocketState expected = WinHttpWebsocketState::NEW;
		if (!state_.compare_exchange_strong(
			expected, WinHttpWebsocketState::CONNECTING))
			return false;
		if (!GetWinHttpWebsocketApi().IsAvailable()) {
			state_ = WinHttpWebsocketState::CLOSED;
			delegate_->OnConnect(false);
			return false;
		}

		try {
			connection_thread_ = std::thread(&CWinHttpWebsocket::Run, this);
		}
		catch (...) {
			state_ = WinHttpWebsocketState::CLOSED;
			delegate_->OnConnect(false);
			return false;
		}
		return true;
	}

	int Send(const void* data, size_t length) override
	{
		// Match the previous backend: input typed while connecting is discarded.
		if (state_ != WinHttpWebsocketState::ESTABLISHED)
			return static_cast<int>(length);
		if (!length)
			return 0;

		{
			std::lock_guard<std::mutex> lock(send_mu_);
			if (send_buffers_.empty() ||
				send_buffers_.back().size() >= kMaxMessageSize)
				send_buffers_.emplace_back();

			auto& buffer = send_buffers_.back();
			const auto* bytes = static_cast<const uint8_t*>(data);
			buffer.insert(buffer.end(), bytes, bytes + length);
		}
		send_cv_.notify_one();
		return static_cast<int>(length);
	}

	void Shutdown() override
	{
		Close();
	}

	void Close() override
	{
		{
			std::lock_guard<std::mutex> callback_lock(callback_mu_);
			WinHttpWebsocketState old = state_.exchange(
				WinHttpWebsocketState::CLOSED);
			if (old == WinHttpWebsocketState::CLOSED)
				return;
			RequestStop();
		}
		ShutdownAndCloseHandles(WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS);
		if (connected_)
			NotifyClose();
	}

	bool IsSecure() const override { return is_secure_; }
	std::string Description() const override { return "WinHTTP WebSocket"; }

private:
	bool AdoptHandle(HINTERNET& destination, HINTERNET handle)
	{
		if (!handle)
			return false;

		std::lock_guard<std::mutex> lock(handles_mu_);
		if (stop_requested_) {
			WinHttpCloseHandle(handle);
			return false;
		}
		destination = handle;
		return true;
	}

	HINTERNET GetWebsocketHandle()
	{
		std::lock_guard<std::mutex> lock(handles_mu_);
		return websocket_;
	}

	void CloseRequestHandle()
	{
		HINTERNET request = nullptr;
		{
			std::lock_guard<std::mutex> lock(handles_mu_);
			request = request_;
			request_ = nullptr;
		}
		if (request)
			WinHttpCloseHandle(request);
	}

	void CloseHandles()
	{
		HINTERNET websocket = nullptr;
		HINTERNET request = nullptr;
		HINTERNET connection = nullptr;
		HINTERNET session = nullptr;
		{
			std::lock_guard<std::mutex> lock(handles_mu_);
			websocket = websocket_;
			request = request_;
			connection = connection_;
			session = session_;
			websocket_ = nullptr;
			request_ = nullptr;
			connection_ = nullptr;
			session_ = nullptr;
		}

		if (websocket)
			WinHttpCloseHandle(websocket);
		if (request)
			WinHttpCloseHandle(request);
		if (connection)
			WinHttpCloseHandle(connection);
		if (session)
			WinHttpCloseHandle(session);
	}

	void RequestStop()
	{
		stop_requested_ = true;
		send_cv_.notify_all();
	}

	void ShutdownAndCloseHandles(USHORT status)
	{
		std::lock_guard<std::mutex> operation_lock(send_operation_mu_);
		HINTERNET websocket = GetWebsocketHandle();
		if (websocket) {
			DWORD error = GetWinHttpWebsocketApi().shutdown(
				websocket, status, nullptr, 0);
			if (error != NO_ERROR && error != ERROR_WINHTTP_OPERATION_CANCELLED)
				LogWinHttpError("WinHttpWebSocketShutdown", error);
		}
		CloseHandles();
	}

	void NotifyClose()
	{
		bool expected = false;
		if (close_notified_.compare_exchange_strong(expected, true))
			delegate_->OnClose();
	}

	bool Open()
	{
		HINTERNET session = WinHttpOpen(
			L"PCMan/WinHTTP WebSocket",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!session) {
			session = WinHttpOpen(
				L"PCMan/WinHTTP WebSocket",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0);
		}
		if (!session) {
			LogWinHttpError("WinHttpOpen", GetLastError());
			return false;
		}
		if (!AdoptHandle(session_, session))
			return false;

		// Connecting and sending have finite timeouts, but an idle BBS receive
		// must be allowed to wait indefinitely.
		WinHttpSetTimeouts(session, 30000, 30000, 30000, 0);

		std::wstring server = ToWide(address_.Server());
		HINTERNET connection = WinHttpConnect(
			session, server.c_str(), address_.Port(), 0);
		if (!connection) {
			LogWinHttpError("WinHttpConnect", GetLastError());
			return false;
		}
		if (!AdoptHandle(connection_, connection))
			return false;

		std::wstring path = ToWide(address_.Path());
		if (path.empty())
			path = L"/";
		else if (path.front() != L'/')
			path.insert(path.begin(), L'/');

		HINTERNET request = WinHttpOpenRequest(
			connection,
			L"GET",
			path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			is_secure_ ? WINHTTP_FLAG_SECURE : 0);
		if (!request) {
			LogWinHttpError("WinHttpOpenRequest", GetLastError());
			return false;
		}
		if (!AdoptHandle(request_, request))
			return false;

		if (!WinHttpSetOption(
			request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
			LogWinHttpError("WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET", GetLastError());
			return false;
		}

		static const wchar_t kOriginHeader[] = L"Origin: app://pcman\r\n";
		if (!WinHttpAddRequestHeaders(
			request,
			kOriginHeader,
			static_cast<DWORD>(-1L),
			WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
			LogWinHttpError("WinHttpAddRequestHeaders", GetLastError());
			return false;
		}

		if (!WinHttpSendRequest(
			request,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			0)) {
			LogWinHttpError("WinHttpSendRequest", GetLastError());
			return false;
		}
		if (!WinHttpReceiveResponse(request, nullptr)) {
			LogWinHttpError("WinHttpReceiveResponse", GetLastError());
			return false;
		}

		DWORD status = 0;
		DWORD status_size = sizeof(status);
		if (!WinHttpQueryHeaders(
			request,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&status,
			&status_size,
			WINHTTP_NO_HEADER_INDEX)) {
			LogWinHttpError("WinHttpQueryHeaders(WINHTTP_QUERY_STATUS_CODE)", GetLastError());
			return false;
		}
		if (status != HTTP_STATUS_SWITCH_PROTOCOLS) {
			char message[160];
			sprintf_s(message, "WebSocket HTTP upgrade failed with HTTP status %lu\r\n", status);
			OutputDebugStringA(message);
			return false;
		}

		HINTERNET websocket =
			GetWinHttpWebsocketApi().complete_upgrade(request, 0);
		if (!websocket) {
			LogWinHttpError("WinHttpWebSocketCompleteUpgrade", GetLastError());
			return false;
		}
		if (!AdoptHandle(websocket_, websocket))
			return false;

		CloseRequestHandle();
		return true;
	}

	void SendLoop()
	{
		for (;;) {
			std::vector<uint8_t> buffer;
			{
				std::unique_lock<std::mutex> lock(send_mu_);
				send_cv_.wait(lock, [this] {
					return stop_requested_ || !send_buffers_.empty();
				});
				if (stop_requested_)
					return;
				buffer = std::move(send_buffers_.front());
				send_buffers_.pop_front();
			}

			if (buffer.size() > (std::numeric_limits<DWORD>::max)()) {
				LogWinHttpError("WinHttpWebSocketSend", ERROR_INVALID_PARAMETER);
				RequestStop();
				CloseHandles();
				return;
			}

			DWORD error = NO_ERROR;
			{
				std::lock_guard<std::mutex> operation_lock(send_operation_mu_);
				if (stop_requested_)
					return;
				HINTERNET websocket = GetWebsocketHandle();
				if (!websocket)
					return;
				error = GetWinHttpWebsocketApi().send(
					websocket,
					WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
					buffer.data(),
					static_cast<DWORD>(buffer.size()));
			}
			if (error != NO_ERROR) {
				if (error != ERROR_WINHTTP_OPERATION_CANCELLED)
					LogWinHttpError("WinHttpWebSocketSend", error);
				RequestStop();
				CloseHandles();
				return;
			}
		}
	}

	void ReceiveLoop()
	{
		std::vector<uint8_t> message;
		std::vector<uint8_t> buffer(kReceiveBufferSize);

		while (!stop_requested_) {
			HINTERNET websocket = GetWebsocketHandle();
			if (!websocket)
				break;

			DWORD bytes_read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type =
				WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
			DWORD error = GetWinHttpWebsocketApi().receive(
				websocket,
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				&bytes_read,
				&buffer_type);
			if (error != NO_ERROR) {
				if (!stop_requested_ && error != ERROR_WINHTTP_OPERATION_CANCELLED)
					LogWinHttpError("WinHttpWebSocketReceive", error);
				break;
			}

			switch (buffer_type) {
			case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
				message.insert(
					message.end(), buffer.begin(), buffer.begin() + bytes_read);
				break;

			case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
				message.insert(
					message.end(), buffer.begin(), buffer.begin() + bytes_read);
				if (!message.empty())
					delegate_->OnReceive(message.data(), message.size());
				message.clear();
				break;

			case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
				{
					USHORT status = WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS;
					DWORD reason_length = 0;
					if (GetWinHttpWebsocketApi().query_close_status) {
						GetWinHttpWebsocketApi().query_close_status(
							websocket, &status, nullptr, 0, &reason_length);
					}
					RequestStop();
					ShutdownAndCloseHandles(status);
					return;
				}

			default:
				// PTT uses binary messages. Text messages are intentionally ignored,
				// matching PCMan's existing WebSocket behavior.
				break;
			}
		}
	}

	void Run()
	{
		if (!Open()) {
			CloseHandles();
			std::lock_guard<std::mutex> callback_lock(callback_mu_);
			WinHttpWebsocketState expected = WinHttpWebsocketState::CONNECTING;
			if (state_.compare_exchange_strong(
				expected, WinHttpWebsocketState::CLOSED))
				delegate_->OnConnect(false);
			return;
		}

		bool connection_cancelled = false;
		{
			std::lock_guard<std::mutex> callback_lock(callback_mu_);
			WinHttpWebsocketState expected = WinHttpWebsocketState::CONNECTING;
			connection_cancelled = stop_requested_ ||
				!state_.compare_exchange_strong(
					expected, WinHttpWebsocketState::ESTABLISHED);
			if (!connection_cancelled) {
				connected_ = true;
				delegate_->OnConnect(true);
			}
		}
		if (connection_cancelled) {
			CloseHandles();
			return;
		}

		try {
			send_thread_ = std::thread(&CWinHttpWebsocket::SendLoop, this);
		}
		catch (...) {
			RequestStop();
			CloseHandles();
			state_ = WinHttpWebsocketState::CLOSED;
			NotifyClose();
			return;
		}

		ReceiveLoop();
		RequestStop();
		CloseHandles();
		state_ = WinHttpWebsocketState::CLOSED;
		NotifyClose();
	}

	CAddress address_;
	std::shared_ptr<CConnEventDelegate> delegate_;
	bool is_secure_ = false;

	std::atomic<WinHttpWebsocketState> state_ = WinHttpWebsocketState::NEW;
	std::atomic<bool> stop_requested_ = false;
	std::atomic<bool> connected_ = false;
	std::atomic<bool> close_notified_ = false;

	std::thread connection_thread_;
	std::thread send_thread_;

	std::mutex handles_mu_;
	HINTERNET session_ = nullptr;
	HINTERNET connection_ = nullptr;
	HINTERNET request_ = nullptr;
	HINTERNET websocket_ = nullptr;

	std::mutex send_mu_;
	std::condition_variable send_cv_;
	std::deque<std::vector<uint8_t>> send_buffers_;
	std::mutex send_operation_mu_;
	std::mutex callback_mu_;
};

}  // namespace

std::shared_ptr<CConnIO> CreateWinHttpWebsocket(
	const CAddress& address,
	std::shared_ptr<CConnEventDelegate> delegate)
{
	return std::make_shared<CWinHttpWebsocket>(address, std::move(delegate));
}
