#pragma once

#include <memory>

#include "ConnIO.h"
#include "Address.h"

bool IsWinHttpWebsocketAvailable();

std::shared_ptr<CConnIO> CreateWinHttpWebsocket(
	const CAddress& address,
	std::shared_ptr<CConnEventDelegate> delegate);
