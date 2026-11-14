#pragma once

namespace Cull
{
	// Reads the config and starts the scan timer. Call once after DataLoaded.
	void Init();

	// Drops every cached shape reference. Call (on the main thread) before a save is loaded or a new game starts,
	// so no overlay shape is kept alive across a load.
	void Reset();
}
