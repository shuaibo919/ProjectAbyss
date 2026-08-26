#include "TreeGen/SlowTree/SlowTreeCompute.h"

#include "TreeGen/SlowTree/shaders/hello_compute.spv.h"

#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>
#include <cstring>
#include <vector>

using namespace godot;

namespace
{
	constexpr uint64_t HELLO_COMPUTE_THREADS = 64; // must match local_size_x in hello_compute.comp
	constexpr int32_t HELLO_COMPUTE_DISPATCH_ITERATIONS = 20; // timed submits after one warmup
	constexpr uint64_t MAX_ELEMENTS = 1 << 24; // 16M floats = 64 MB per buffer

	using Clock = std::chrono::steady_clock;

	double MsSince(Clock::time_point Start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
	}

	PackedByteArray ToBytes(const void* Data, uint64_t SizeBytes)
	{
		PackedByteArray Bytes;
		Bytes.resize(SizeBytes);
		memcpy(Bytes.ptrw(), Data, SizeBytes);
		return Bytes;
	}

	/** Binds one storage buffer at the given set/binding. */
	Ref<RDUniform> StorageUniform(RID Buffer, int32_t Binding)
	{
		Ref<RDUniform> Uniform;
		Uniform.instantiate();
		Uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		Uniform->set_binding(Binding);
		Uniform->add_id(Buffer);
		return Uniform;
	}
} // namespace

void SlowTreeCompute::_bind_methods()
{
	ClassDB::bind_static_method("SlowTreeCompute",
		D_METHOD("hello_compute_probe", "element_count", "verbose"),
		&SlowTreeCompute::hello_compute_probe,
		DEFVAL(1 << 20), DEFVAL(true));
}

Dictionary SlowTreeCompute::hello_compute_probe(uint64_t ElementCount, bool Verbose)
{
	Dictionary Result;
	Result["ok"] = false;
	Result["verified"] = false;
	Result["element_count"] = ElementCount;

	auto Fail = [&](const String& Message) {
		UtilityFunctions::printerr("[SlowTree] hello_compute_probe: ", Message);
		return Result;
	};

	if (ElementCount == 0 || ElementCount > MAX_ELEMENTS)
	{
		return Fail(vformat("element_count %d out of range (1..%d)", ElementCount, MAX_ELEMENTS));
	}

	RenderingServer* RS = RenderingServer::get_singleton();
	if (RS == nullptr)
	{
		return Fail("no RenderingServer singleton");
	}

	// Phase 0: local rendering device. The fork's D3D12 RD is used here exactly
	// as the tree pipeline will: fresh device, owned by the caller (RD is an
	// Object, not RefCounted — memdelete when done).
	Clock::time_point PhaseStart = Clock::now();
	RenderingDevice* RD = RS->create_local_rendering_device();
	if (RD == nullptr)
	{
		return Fail("create_local_rendering_device returned null");
	}
	double DeviceMs = MsSince(PhaseStart);

	RID Shader, Pipeline, UniformSet, SrcBuffer, DstBuffer, ParamsBuffer;
	double ShaderMs = 0, BuffersMs = 0, UniformSetMs = 0, PipelineMs = 0, ReadbackMs = 0;
	std::vector<double> DispatchTimes;
	bool Verified = false;
	uint64_t FirstMismatch = 0;

	// Phase 1: SPIR-V -> RD shader. Bytecode comes from the checked-in header
	// baked by Tools/build_slowtree_shaders.ps1.
	PhaseStart = Clock::now();
	Ref<RDShaderSPIRV> Spirv;
	Spirv.instantiate();
	{
		PackedByteArray Bytecode;
		Bytecode.resize(HELLO_COMPUTE_SPV_WORDS * 4);
		memcpy(Bytecode.ptrw(), HELLO_COMPUTE_SPV, HELLO_COMPUTE_SPV_WORDS * 4);
		Spirv->set_stage_bytecode(RenderingDevice::SHADER_STAGE_COMPUTE, Bytecode);
	}
	Shader = RD->shader_create_from_spirv(Spirv);
	ShaderMs = MsSince(PhaseStart);
	if (!Shader.is_valid())
	{
		memdelete(RD);
		return Fail("shader_create_from_spirv returned an invalid RID");
	}

	// Phase 2: buffers. Input pattern uses values < 512 so x*2+1 stays exact.
	const uint64_t DataBytes = ElementCount * sizeof(float);
	std::vector<float> SrcData(ElementCount);
	for (uint64_t i = 0; i < ElementCount; ++i)
	{
		SrcData[i] = float(i & 0x3FF) * 0.1f;
	}
	PhaseStart = Clock::now();
	SrcBuffer = RD->storage_buffer_create(uint32_t(DataBytes), ToBytes(SrcData.data(), DataBytes));
	DstBuffer = RD->storage_buffer_create(uint32_t(DataBytes));
	uint32_t ElementCountU32 = uint32_t(ElementCount);
	ParamsBuffer = RD->storage_buffer_create(sizeof(uint32_t), ToBytes(&ElementCountU32, sizeof(uint32_t)));
	BuffersMs = MsSince(PhaseStart);
	if (!SrcBuffer.is_valid() || !DstBuffer.is_valid() || !ParamsBuffer.is_valid())
	{
		RD->free_rid(Shader);
		memdelete(RD);
		return Fail("storage_buffer_create returned an invalid RID");
	}

	// Phase 3: uniform set + compute pipeline.
	PhaseStart = Clock::now();
	TypedArray<Ref<RDUniform>> Uniforms;
	Uniforms.append(StorageUniform(SrcBuffer, 0));
	Uniforms.append(StorageUniform(DstBuffer, 1));
	Uniforms.append(StorageUniform(ParamsBuffer, 2));
	UniformSet = RD->uniform_set_create(Uniforms, Shader, 0);
	UniformSetMs = MsSince(PhaseStart);
	if (!UniformSet.is_valid())
	{
		RD->free_rid(ParamsBuffer);
		RD->free_rid(DstBuffer);
		RD->free_rid(SrcBuffer);
		RD->free_rid(Shader);
		memdelete(RD);
		return Fail("uniform_set_create returned an invalid RID");
	}

	PhaseStart = Clock::now();
	Pipeline = RD->compute_pipeline_create(Shader);
	PipelineMs = MsSince(PhaseStart);
	if (!Pipeline.is_valid())
	{
		RD->free_rid(UniformSet);
		RD->free_rid(ParamsBuffer);
		RD->free_rid(DstBuffer);
		RD->free_rid(SrcBuffer);
		RD->free_rid(Shader);
		memdelete(RD);
		return Fail("compute_pipeline_create returned an invalid RID");
	}

	// Phase 4: dispatch, repeated to measure steady-state submit+sync latency.
	// One warmup iteration first so pipeline/binding creation isn't counted.
	const uint32_t GroupsX = uint32_t((ElementCount + HELLO_COMPUTE_THREADS - 1) / HELLO_COMPUTE_THREADS);
	for (int32_t Iteration = 0; Iteration <= HELLO_COMPUTE_DISPATCH_ITERATIONS; ++Iteration)
	{
		if (Iteration == 1)
		{
			PhaseStart = Clock::now();
		}

		int64_t List = RD->compute_list_begin();
		RD->compute_list_bind_compute_pipeline(List, Pipeline);
		RD->compute_list_bind_uniform_set(List, UniformSet, 0);
		RD->compute_list_dispatch(List, GroupsX, 1, 1);
		RD->compute_list_end();
		RD->submit();
		RD->sync();

		if (Iteration >= 1)
		{
			DispatchTimes.push_back(MsSince(PhaseStart));
			PhaseStart = Clock::now();
		}
	}

	// Phase 5: readback + bit-exact verification.
	PhaseStart = Clock::now();
	PackedByteArray Output = RD->buffer_get_data(DstBuffer);
	Verified = (uint64_t)Output.size() == DataBytes;
	if (Verified)
	{
		const float* OutputFloats = reinterpret_cast<const float*>(Output.ptr());
		for (uint64_t i = 0; i < ElementCount; ++i)
		{
			const float Expected = SrcData[i] * 2.0f + 1.0f;
			if (memcmp(&OutputFloats[i], &Expected, sizeof(float)) != 0)
			{
				Verified = false;
				FirstMismatch = i;
				break;
			}
		}
	}
	ReadbackMs = MsSince(PhaseStart);

	// Cleanup: RIDs first, then the device itself.
	RD->free_rid(Pipeline);
	RD->free_rid(UniformSet);
	RD->free_rid(ParamsBuffer);
	RD->free_rid(DstBuffer);
	RD->free_rid(SrcBuffer);
	RD->free_rid(Shader);
	memdelete(RD);

	// Summary.
	double DispatchSum = 0, DispatchMin = 0;
	for (size_t i = 0; i < DispatchTimes.size(); ++i)
	{
		DispatchSum += DispatchTimes[i];
		DispatchMin = (i == 0) ? DispatchTimes[i] : Math::min(DispatchMin, DispatchTimes[i]);
	}
	const double DispatchAvg = DispatchSum / double(DispatchTimes.size());

	Dictionary Timings;
	Timings["device_ms"] = DeviceMs;
	Timings["shader_ms"] = ShaderMs;
	Timings["buffers_ms"] = BuffersMs;
	Timings["uniform_set_ms"] = UniformSetMs;
	Timings["pipeline_ms"] = PipelineMs;
	Timings["dispatch_avg_ms"] = DispatchAvg;
	Timings["dispatch_min_ms"] = DispatchMin;
	Timings["readback_ms"] = ReadbackMs;

	Result["ok"] = true;
	Result["verified"] = Verified;
	Result["timings"] = Timings;
	if (!Verified)
	{
		Result["first_mismatch"] = FirstMismatch;
	}

	if (Verbose)
	{
		UtilityFunctions::print(vformat(
			"[SlowTree] hello-compute probe: %d floats (%d KB per buffer), %d dispatch groups",
			ElementCount, DataBytes / 1024, GroupsX));
		UtilityFunctions::print(vformat("  device create      %7.3f ms", DeviceMs));
		UtilityFunctions::print(vformat("  shader (SPIR-V)    %7.3f ms", ShaderMs));
		UtilityFunctions::print(vformat("  buffers + upload   %7.3f ms", BuffersMs));
		UtilityFunctions::print(vformat("  uniform set        %7.3f ms", UniformSetMs));
		UtilityFunctions::print(vformat("  pipeline           %7.3f ms", PipelineMs));
		UtilityFunctions::print(vformat("  dispatch x%d     min %7.3f / avg %7.3f ms", HELLO_COMPUTE_DISPATCH_ITERATIONS, DispatchMin, DispatchAvg));
		UtilityFunctions::print(vformat("  readback + verify  %7.3f ms", ReadbackMs));
		if (Verified)
		{
			UtilityFunctions::print("  verification: BIT-EXACT OK");
		}
		else
		{
			UtilityFunctions::print(vformat("  verification: MISMATCH at element %d", FirstMismatch));
		}
	}

	return Result;
}
