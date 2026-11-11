#pragma once

#include "ffx_interface.h"

FfxErrorCode GetResourceSizeFromDescription(FfxDevice device, const FfxCreateResourceDescription* description,
	uint64_t* size_in_bytes, uint64_t* alignment = nullptr);
