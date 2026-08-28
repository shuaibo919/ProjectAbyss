#include "TreeGen/SlowTree/SlowTreeCompute.h"

#include "TreeGen/SlowTree/shaders/hello_compute.spv.h"
#include "TreeGen/SlowTree/shaders/cylinder.spv.h"
#include "TreeGen/SlowTree/shaders/collar.spv.h"
#include "TreeGen/SlowTree/shaders/leaf_card.spv.h"
#include "TreeGen/SlowTree/shaders/frond.spv.h"

#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
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
	// baked by Source/TreeGen/SlowTree/tools/build_slowtree_shaders.ps1.
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

// ================= Stage 2: GPU 细分 + 读回 =================
// CPU 阶段(TreeGenerator GPU 发射模式)产出 TreeGpuEmission; 这里把四类描述子
// 上传为 SSBO, 由 cylinder/collar/leaf_card/frond 四个 compute 着色器展开成
// 统一 float 顶点缓冲 + uint32 索引缓冲(尺寸 = CPU 精确前缀和), 读回后按
// chunk 区间拼回 OutMesh.batches——批次顺序/材质/骨架全程保持 CPU 阶段的产物。

bool SlowTreeCompute::RunGpu(const TreeGpuEmission& Emission, TreeMeshData& OutMesh,
                             Dictionary* OutStats, String& OutError)
{
	// 无几何: 直接成功(批次为空, 装配层自然跳过)。
	if (Emission.VertFloats == 0)
	{
		return true;
	}
	if (Emission.IndexCount == 0)
	{
		OutError = "发射含顶点但无索引(数据不一致)。";
		UtilityFunctions::printerr("[SlowTree] RunGpu: ", OutError);
		return false;
	}

	RenderingServer* RS = RenderingServer::get_singleton();
	if (RS == nullptr)
	{
		OutError = "no RenderingServer singleton";
		UtilityFunctions::printerr("[SlowTree] RunGpu: ", OutError);
		return false;
	}

	Clock::time_point PhaseStart = Clock::now();

	// 池化(Stage 2): create_local_rendering_device 实测 ~175ms/次, 着色器+管线创建
	// ~25ms/次——每代新建会把这些固定成本叠加到每次生成上。设备/着色器/管线按进程
	// 生命周期缓存(RID 归 RenderingServer 所有, 引擎退出时统一回收); 缓冲与 uniform
	// set 依赖每次生成的尺寸, 仍每代新建。Stage 3 的 worker 线程需要独立 RD
	// (RD 非线程安全), 不共享此池。
	static RenderingDevice* s_PoolRD = nullptr;
	static RID s_PoolShader[4];     // 按着色器槽位缓存(4 个 .spv.h 是静态存储)
	static RID s_PoolPipeline[4];
	const bool PooledDevice = (s_PoolRD != nullptr);   // 本次调用复用了池

	RenderingDevice* RD = s_PoolRD;
	if (RD == nullptr)
	{
		RD = RS->create_local_rendering_device();
		if (RD == nullptr)
		{
			OutError = "create_local_rendering_device returned null";
			UtilityFunctions::printerr("[SlowTree] RunGpu: ", OutError);
			return false;
		}
		s_PoolRD = RD;
	}
	const double DeviceMs = PooledDevice ? 0.0 : MsSince(PhaseStart);

	// 资源追踪: 每次生成的新建 RID(缓冲/uniform set)统一释放, 逆序
	// (依赖者先释放: uniform set → buffer)。池化资源不进此列表。
	std::vector<RID> Created;
	bool Ok = true;
	String Error;
	auto Cleanup = [&]() {
		for (auto it = Created.rbegin(); it != Created.rend(); ++it)
		{
			RD->free_rid(*it);
		}
		// 池拥有设备: 只要创建成功入池就永不释放(进程级); 仅在未入池时删除。
		if (RD != s_PoolRD)
		{
			memdelete(RD);
		}
	};
	auto Track = [&](RID r) -> RID {
		if (r.is_valid()) Created.push_back(r);
		return r;
	};

	// 每类描述子的数量/字节数(扁平 float 数组, 每 batch 一段)。
	struct DescStats { uint64_t Count = 0; uint64_t Bytes = 0; };
	auto GatherDescStats = [](const std::vector<std::vector<float>>& PerBatch, uint32_t StrideFloats) {
		DescStats s;
		for (const auto& v : PerBatch) s.Count += v.size();
		s.Count /= StrideFloats;
		s.Bytes = s.Count * StrideFloats * 4;
		return s;
	};
	const DescStats Branch = GatherDescStats(Emission.BranchDescs, GPU_BRANCH_SEG_FLOATS);
	const DescStats Collar = GatherDescStats(Emission.CollarDescs, GPU_COLLAR_FLOATS);
	const DescStats Leaf   = GatherDescStats(Emission.LeafDescs,   GPU_LEAF_CARD_FLOATS);
	const DescStats Frond  = GatherDescStats(Emission.FrondDescs,  GPU_FROND_FLOATS);

	// ---- 缓冲创建(计时) ----
	PhaseStart = Clock::now();

	// 输出缓冲: 精确尺寸来自发射前缀和, 空区由着色器按 firstVertex/firstIdx 写入。
	RID VertBuffer = Track(RD->storage_buffer_create(uint32_t(Emission.VertFloats * 4)));
	RID IdxBuffer  = Track(RD->storage_buffer_create(uint32_t(Emission.IndexCount * 4)));

	// 描述子缓冲(仅在有描述子时创建)。
	auto CreateDescBuffer = [&](const std::vector<std::vector<float>>& PerBatch, uint64_t Bytes) -> RID {
		PackedByteArray Data;
		Data.resize(Bytes);
		uint8_t* dst = Data.ptrw();
		for (const auto& v : PerBatch)
		{
			if (v.empty()) continue;
			memcpy(dst, v.data(), v.size() * 4);
			dst += v.size() * 4;
		}
		return Track(RD->storage_buffer_create(uint32_t(Bytes), Data));
	};
	RID BranchBuf = Branch.Count ? CreateDescBuffer(Emission.BranchDescs, Branch.Bytes) : RID();
	RID CollarBuf = Collar.Count ? CreateDescBuffer(Emission.CollarDescs, Collar.Bytes) : RID();
	RID LeafBuf   = Leaf.Count   ? CreateDescBuffer(Emission.LeafDescs,   Leaf.Bytes)   : RID();
	RID FrondBuf  = Frond.Count  ? CreateDescBuffer(Emission.FrondDescs,  Frond.Bytes)  : RID();

	// 共享池缓冲: 可能为空的池也要建(着色器绑定集要求全部 binding 存在),
	// 以 16 字节兜底防零尺寸。
	auto CreatePoolBuffer = [&](const std::vector<float>& Pool) -> RID {
		const uint64_t Bytes = Pool.empty() ? 16 : Pool.size() * 4;
		PackedByteArray Data;
		Data.resize(Bytes);
		if (!Pool.empty()) memcpy(Data.ptrw(), Pool.data(), Pool.size() * 4);
		else memset(Data.ptrw(), 0, 16);
		return Track(RD->storage_buffer_create(uint32_t(Bytes), Data));
	};
	auto CreateTriBuffer = [&]() -> RID {
		const uint64_t Bytes = Emission.CutoutTris.empty() ? 16 : Emission.CutoutTris.size() * 4;
		PackedByteArray Data;
		Data.resize(Bytes);
		if (!Emission.CutoutTris.empty())
			memcpy(Data.ptrw(), Emission.CutoutTris.data(), Emission.CutoutTris.size() * 4);
		else memset(Data.ptrw(), 0, 16);
		return Track(RD->storage_buffer_create(uint32_t(Bytes), Data));
	};
	RID RingBuf   = CreatePoolBuffer(Emission.Rings);
	RID PointBuf  = CreatePoolBuffer(Emission.CutoutPoints);
	RID TriBuf    = CreateTriBuffer();

	if (!VertBuffer.is_valid() || !IdxBuffer.is_valid() ||
	    (Branch.Count && !BranchBuf.is_valid()) || (Collar.Count && !CollarBuf.is_valid()) ||
	    (Leaf.Count && !LeafBuf.is_valid()) || (Frond.Count && !FrondBuf.is_valid()) ||
	    !RingBuf.is_valid() || !PointBuf.is_valid() || !TriBuf.is_valid())
	{
		Ok = false;
		Error = "storage_buffer_create 返回无效 RID";
	}
	const double BuffersMs = MsSince(PhaseStart);

	// ---- 着色器 + uniform set + pipeline(计时) ----
	PhaseStart = Clock::now();
	// 着色器/管线走进程级缓存(槽位 0..3 对应 4 个 .comp); uniform set 依赖
	// 每代缓冲, 不进缓存。
	auto GetShader = [&](int Slot, const uint32_t* Words, uint32_t WordCount) -> RID {
		if (s_PoolShader[Slot].is_valid()) return s_PoolShader[Slot];
		Ref<RDShaderSPIRV> Spirv;
		Spirv.instantiate();
		PackedByteArray Bytecode;
		Bytecode.resize(int64_t(WordCount) * 4);
		memcpy(Bytecode.ptrw(), Words, uint64_t(WordCount) * 4);
		Spirv->set_stage_bytecode(RenderingDevice::SHADER_STAGE_COMPUTE, Bytecode);
		const RID sh = RD->shader_create_from_spirv(Spirv);
		if (sh.is_valid()) s_PoolShader[Slot] = sh;
		return sh;
	};

	struct Pass { RID Shader, UniformSet, Pipeline; uint32_t Groups = 0; };
	Pass CylinderPass, CollarPass, LeafPass, FrondPass;
	auto SetupPass = [&](Pass& P, int Slot, const uint32_t* Words, uint32_t WordCount,
	                     uint64_t DescCount, uint32_t GroupSize,
	                     const std::vector<std::pair<RID, int32_t>>& Bindings) {
		if (DescCount == 0 || !Ok) return;
		P.Shader = GetShader(Slot, Words, WordCount);
		if (!P.Shader.is_valid()) { Ok = false; Error = "shader_create_from_spirv 失败"; return; }
		TypedArray<Ref<RDUniform>> Uniforms;
		for (const auto& [buf, binding] : Bindings)
		{
			Uniforms.append(StorageUniform(buf, binding));
		}
		P.UniformSet = Track(RD->uniform_set_create(Uniforms, P.Shader, 0));
		if (s_PoolPipeline[Slot].is_valid())
		{
			P.Pipeline = s_PoolPipeline[Slot];
		}
		else
		{
			P.Pipeline = RD->compute_pipeline_create(P.Shader);
			if (P.Pipeline.is_valid()) s_PoolPipeline[Slot] = P.Pipeline;
		}
		if (!P.UniformSet.is_valid() || !P.Pipeline.is_valid())
		{
			Ok = false;
			Error = "uniform_set/compute_pipeline 创建失败";
			return;
		}
		P.Groups = uint32_t((DescCount + GroupSize - 1) / GroupSize);
	};

	constexpr uint32_t CYLD_THREADS = 64;   // cylinder.comp local_size_x
	SetupPass(CylinderPass, 0, CYLINDER_SPV, CYLINDER_SPV_WORDS, Branch.Count, CYLD_THREADS,
	          {{BranchBuf, 0}, {VertBuffer, 1}, {IdxBuffer, 2}});
	SetupPass(CollarPass, 1, COLLAR_SPV, COLLAR_SPV_WORDS, Collar.Count, 1,
	          {{CollarBuf, 0}, {RingBuf, 1}, {VertBuffer, 2}, {IdxBuffer, 3}});
	SetupPass(LeafPass, 2, LEAF_CARD_SPV, LEAF_CARD_SPV_WORDS, Leaf.Count, 1,
	          {{LeafBuf, 0}, {PointBuf, 1}, {TriBuf, 2}, {VertBuffer, 3}, {IdxBuffer, 4}});
	SetupPass(FrondPass, 3, FROND_SPV, FROND_SPV_WORDS, Frond.Count, 1,
	          {{FrondBuf, 0}, {RingBuf, 1}, {PointBuf, 2}, {TriBuf, 3}, {VertBuffer, 4}, {IdxBuffer, 5}});
	const double SetupMs = MsSince(PhaseStart);

	// ---- dispatch(每个 pass 一个 compute list, 单次 submit + sync) ----
	double GpuMs = 0, ReadbackMs = 0, AssembleMs = 0;
	if (Ok)
	{
		PhaseStart = Clock::now();
		auto DispatchPass = [&](const Pass& P) {
			const int64_t List = RD->compute_list_begin();
			RD->compute_list_bind_compute_pipeline(List, P.Pipeline);
			RD->compute_list_bind_uniform_set(List, P.UniformSet, 0);
			RD->compute_list_dispatch(List, P.Groups, 1, 1);
			RD->compute_list_end();
		};
		if (CylinderPass.Groups > 0) DispatchPass(CylinderPass);
		if (CollarPass.Groups   > 0) DispatchPass(CollarPass);
		if (LeafPass.Groups     > 0) DispatchPass(LeafPass);
		if (FrondPass.Groups    > 0) DispatchPass(FrondPass);
		RD->submit();
		RD->sync();
		GpuMs = MsSince(PhaseStart);

		// ---- 读回 + 按 chunk 拼回 batch ----
		PhaseStart = Clock::now();
		PackedByteArray VertBytes = RD->buffer_get_data(VertBuffer);
		PackedByteArray IdxBytes  = RD->buffer_get_data(IdxBuffer);
		if ((uint64_t)VertBytes.size() < Emission.VertFloats * 4 ||
		    (uint64_t)IdxBytes.size() < Emission.IndexCount * 4)
		{
			Ok = false;
			Error = vformat("读回尺寸不符(顶点 %d/%d 字节, 索引 %d/%d 字节)",
			                VertBytes.size(), Emission.VertFloats * 4,
			                IdxBytes.size(), Emission.IndexCount * 4);
		}
		ReadbackMs = MsSince(PhaseStart);

		if (Ok)
		{
			PhaseStart = Clock::now();
			const float* V = reinterpret_cast<const float*>(VertBytes.ptr());
			const uint32_t* I = reinterpret_cast<const uint32_t*>(IdxBytes.ptr());
			// 按发射顺序逐 chunk 拼回 batch: 顶点直接复制; 索引是全局顶点单位
			// (着色器按描述子 firstVertex 写), 需重定位到 batch 局部顶点基
			// (chunk 顶点插入前的 batch 顶点数 / stride)。
			for (const GpuRangeChunk& c : Emission.Chunks)
			{
				if (c.Batch >= OutMesh.batches.size()) continue;
				MeshBatch& b = OutMesh.batches[c.Batch];
				if (c.VertCount == 0) continue;
				const uint32_t stride = b.isLeaf ? 16u : 10u;
				const uint64_t localBase = b.vertices.size() / stride;
				b.vertices.insert(b.vertices.end(), V + c.VertStart, V + c.VertStart + c.VertCount);
				if (c.IdxCount > 0)
				{
					const int64_t delta = int64_t(localBase) - int64_t(c.FirstVertex);
					const size_t oldSize = b.indices.size();
					b.indices.resize(oldSize + size_t(c.IdxCount));
					for (uint64_t k = 0; k < c.IdxCount; ++k)
					{
						b.indices[oldSize + size_t(k)] = uint32_t(int64_t(I[c.IdxStart + k]) + delta);
					}
				}
			}
			AssembleMs = MsSince(PhaseStart);
		}
	}

	Cleanup();

	if (!Ok)
	{
		UtilityFunctions::printerr("[SlowTree] RunGpu: ", Error);
		OutError = Error;
		return false;
	}

	if (OutStats)
	{
		(*OutStats)["device_ms"]     = DeviceMs;
		(*OutStats)["buffer_ms"]     = BuffersMs;
		(*OutStats)["setup_ms"]      = SetupMs;
		(*OutStats)["gpu_ms"]        = GpuMs;
		(*OutStats)["readback_ms"]   = ReadbackMs;
		(*OutStats)["assemble_ms"]   = AssembleMs;
		(*OutStats)["vertex_floats"] = Emission.VertFloats;
		(*OutStats)["index_count"]   = Emission.IndexCount;
		(*OutStats)["cylinder_descs"] = Branch.Count;
		(*OutStats)["collar_descs"]   = Collar.Count;
		(*OutStats)["leaf_descs"]     = Leaf.Count;
		(*OutStats)["frond_descs"]    = Frond.Count;
	}

	return true;
}
