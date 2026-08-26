#pragma once

// Compute-shader plumbing for the SlowTree integration.
//
// Stage 0 artifact: hello_compute_probe() is the gate that proves the custom
// fork's D3D12 RenderingDevice can round-trip a compute dispatch end to end
// (SPIR-V -> pipeline -> dispatch -> readback) before any real tree work is
// built on it. Stage 2 grows this file into the descriptor/pass pipeline
// (cylinder / collar / leaf_card / frond).

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot
{
	class SlowTreeCompute : public Object
	{
		GDCLASS(SlowTreeCompute, Object)

	protected:
		static void _bind_methods();

	public:
		/**
		 * Runs the hello-compute round trip on a fresh local RenderingDevice.
		 * One thread per float: dst[i] = src[i] * 2.0 + 1.0 — bit-exact IEEE
		 * ops, so the readback must match bit-for-bit.
		 *
		 * Returns a Dictionary: ok, verified, element_count, and a timings
		 * sub-dictionary (ms per phase, dispatch min/avg over repeated submits).
		 * Prints a human-readable summary as it goes.
		 */
		static Dictionary hello_compute_probe(uint64_t ElementCount = 1 << 20, bool Verbose = true);
	};
} // namespace godot
