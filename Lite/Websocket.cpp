#include "Websocket.h"
#include "WinHttpWebsocket.h"

#include <utility>

std::shared_ptr<CConnIO> CreateWebsocket(
	const CAddress& address,
	std::shared_ptr<CConnEventDelegate> delegate)
{
	return CreateWinHttpWebsocket(address, std::move(delegate));
}
