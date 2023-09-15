#include "pch.h"

#include "libWSAController.h"
#include "CallbackLogger.h"

libWSAController::lib_callback callback = nullptr;

void SetCallbackFunction(libWSAController::lib_callback callbackFunction)
{
	callback = callbackFunction;
}