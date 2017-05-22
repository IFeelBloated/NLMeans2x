#include <cmath>
#include "VapourSynth.h"
#include "VSHelper.h"
#include "Helpers.hpp"

#if defined(_MSC_VER)
#include <malloc.h>
#elif defined(__GNUC__) || defined(__GNUG__)
#include <alloca.h>
#endif

struct NLMeans2xData final {
	const VSAPI *vsapi = nullptr;
	VSNodeRef *node = nullptr;
	VSVideoInfo *vi = nullptr;
	decltype(false) Illformed = false;
	decltype(0_i64) a = 0;
	decltype(0_i64) s = 0;
	decltype(0.) h = 0.;
	decltype(0.) sdev = 0.;
	NLMeans2xData(const VSMap *in, VSMap *out, const VSAPI *api) {
		constexpr auto ScalingFactor = 199.09020197967370907985363966763;
		auto Error = 0;
		vsapi = api;
		node = vsapi->propGetNode(in, "clip", 0, nullptr);
		vi = new VSVideoInfo{ *vsapi->getVideoInfo(node) };
		if (!isConstantFormat(vi) || vi->format->sampleType != stFloat || vi->format->bitsPerSample < 32) {
			vsapi->setError(out, "NLMeans2x: input clip must be single precision fp, with constant dimensions.");
			Illformed = true;
			return;
		}
		a = vsapi->propGetInt(in, "a", 0, &Error);
		if (Error)
			a = 8;
		if (a < 0) {
			vsapi->setError(out, "NLMeans2x: a must be no less than 0!");
			Illformed = true;
			return;
		}
		s = vsapi->propGetInt(in, "s", 0, &Error);
		if (Error)
			s = 4;
		if (s < 0) {
			vsapi->setError(out, "NLMeans2x: s must be no less than 0!");
			Illformed = true;
			return;
		}
		h = vsapi->propGetFloat(in, "h", 0, &Error);
		if (Error)
			h = 1.6;
		h /= ScalingFactor;
		sdev = vsapi->propGetFloat(in, "sdev", 0, &Error);
		if (Error)
			sdev = 1.;
	}
	NLMeans2xData(NLMeans2xData &&) = delete;
	NLMeans2xData(const NLMeans2xData &) = delete;
	auto &operator=(NLMeans2xData &&) = delete;
	auto &operator=(const NLMeans2xData &) = delete;
	~NLMeans2xData() {
		delete vi;
		vsapi->freeNode(node);
	}
};

auto VS_CC nlmeans2xInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<NLMeans2xData *>(*instanceData);
	vsapi->setVideoInfo(d->vi, 1, node);
}

auto VS_CC nlmeans2xGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	auto Data = reinterpret_cast<NLMeans2xData *>(*instanceData);
	auto ProcessFrame = [&]() {
		auto SourceFrame = vsapi->getFrameFilter(n, Data->node, frameCtx);
		auto FormatInfo = Data->vi->format;
		auto DestinationFrame = vsapi->newVideoFrame(FormatInfo, Data->vi->width, Data->vi->height, SourceFrame, core);
		auto ProcessPlane = [&](auto Plane) {
			auto Height = vsapi->getFrameHeight(SourceFrame, Plane);
			auto Width = vsapi->getFrameWidth(SourceFrame, Plane);
			const auto SourcePlane = FramePointer<float>{ vsapi->getReadPtr(SourceFrame, Plane), Width };
			auto DestinationPlane = FramePointer<float>{ vsapi->getWritePtr(DestinationFrame, Plane), Width };
			auto ProcessPixel = [&](auto &Pixel) {
				auto Position = FramePointer<float>{ &Pixel, Width };
				auto a = Data->a, SearchWindowSize = static_cast<decltype(a)>(std::pow(2 * a + 1, 2));
				struct NLMeansPixelPack final {
					decltype(0.) Value = 0.;
					decltype(0.) Weight = 0.;
					NLMeansPixelPack(const float &Value, const float &Reference, NLMeans2xData *Data, std::int64_t Width) {
						auto CalculateWeight = [&]() {
							auto WeightedSSE = 0., GaussianWeightNormalizingConstant = 0., h = Data->h;
							auto s = Data->s;
							const auto PatchCursor = FramePointer<float>{ &Value, Width };
							const auto ReferencePatchCursor = FramePointer<float>{ &Reference, Width };
							auto CalculateGaussianWeight = [&](auto x, auto y) {
								auto Variance = std::pow(Data->sdev, 2.);
								auto SquaredDistance = std::pow(x, 2.) + std::pow(y, 2.);
								return std::exp(-SquaredDistance / (2. * Variance));
							};
							for (auto y = -s; y <= s; ++y)
								for (auto x = -s; x <= s; ++x) {
									auto GaussianWeight = CalculateGaussianWeight(x, y);
									WeightedSSE += GaussianWeight * std::pow(PatchCursor[y][x] - ReferencePatchCursor[y][x], 2.);
									GaussianWeightNormalizingConstant += GaussianWeight;
								}
							WeightedSSE /= GaussianWeightNormalizingConstant;
							return std::exp(-WeightedSSE / std::pow(h, 2.));
						};
						this->Value = Value;
						this->Weight = CalculateWeight();
					}
					NLMeansPixelPack(NLMeansPixelPack &&) = default;
					NLMeansPixelPack(const NLMeansPixelPack &) = default;
					auto operator=(NLMeansPixelPack &&)->NLMeansPixelPack & = default;
					auto operator=(const NLMeansPixelPack &)->NLMeansPixelPack & = default;
					~NLMeansPixelPack() = default;
				};
				auto Buffer = reinterpret_cast<NLMeansPixelPack *>(alloca(SearchWindowSize * sizeof(NLMeansPixelPack)));
				auto InitalizeBuffer = [&]() {
					auto CenteredBufferPointer = Buffer + a * (2 * a + 1) + a;
					auto BufferCursor = FramePointer<NLMeansPixelPack>{ CenteredBufferPointer, 2 * a + 1 };
					for (auto y = -a; y <= a; ++y)
						for (auto x = -a; x <= a; ++x)
							BufferCursor[y][x] = NLMeansPixelPack{ Position[y][x], Pixel, Data, Width };
				};
				auto Evaluate = [&]() {
					auto CalculateWeightSum = [&]() {
						auto WeightSum = 0.;
						for (auto i = 0_i64; i < SearchWindowSize; ++i)
							WeightSum += Buffer[i].Weight;
						return WeightSum;
					};
					auto Value = 0., NormalizingConstant = CalculateWeightSum();
					for (auto i = 0_i64; i < SearchWindowSize; ++i)
						Value += Buffer[i].Value * Buffer[i].Weight;
					return Value / NormalizingConstant;
				};
				InitalizeBuffer();
				return Evaluate();
			};

			for (auto y = 12; y < Height - 12; ++y)
				for (auto x = 12; x < Width - 12; ++x)
					DestinationPlane[y][x] = static_cast<float>(ProcessPixel(SourcePlane[y][x]));

		};
		for (auto Plane = 0; Plane < FormatInfo->numPlanes; ++Plane)
			ProcessPlane(Plane);
		vsapi->freeFrame(SourceFrame);
		return DestinationFrame;
	};
	if (activationReason == arInitial)
		vsapi->requestFrameFilter(n, Data->node, frameCtx);
	else if (activationReason == arAllFramesReady)
		return const_cast<const VSFrameRef *>(ProcessFrame());
	return static_cast<const VSFrameRef *>(nullptr);
}

auto VS_CC nlmeans2xFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<NLMeans2xData *>(instanceData);
	delete d;
}

auto VS_CC nlmeans2xCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	auto d = new NLMeans2xData{ in, out, vsapi };
	if (d->Illformed == false)
		vsapi->createFilter(in, out, "NLMeans2x", nlmeans2xInit, nlmeans2xGetFrame, nlmeans2xFree, fmParallel, 0, d, core);
	else
		delete d;
}

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.lol.nlm2x", "nlm2x", "Image Upscaling with NLMeans", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("NLMeans2x",
		"clip:clip;"
		"a:int:opt;"
		"s:int:opt;"
		"h:float:opt;"
		"sdev:float:opt;"
		, nlmeans2xCreate, nullptr, plugin);
}
