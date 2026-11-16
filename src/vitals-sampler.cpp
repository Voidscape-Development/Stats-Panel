/*
Vitals - a compact, adaptive stats dock for OBS Studio
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "vitals-sampler.hpp"

#include <obs-frontend-api.h>

#include <algorithm>
#include <cstring>

namespace vitals {

namespace {

constexpr double kNsPerSec = 1000000000.0;

struct Found {
	const void *identity = nullptr;
	obs_weak_output_t *weak = nullptr;
	std::string name;
	OutputKind kind = OutputKind::Other;
	bool main = false;
};

struct EnumContext {
	std::vector<Found> found;
	const obs_output_t *stream = nullptr;
	const obs_output_t *record = nullptr;
	const obs_output_t *replay = nullptr;
	const obs_output_t *vcam = nullptr;
};

OutputKind classify(obs_output_t *output)
{
	const uint32_t flags = obs_output_get_flags(output);
	if (flags & OBS_OUTPUT_SERVICE)
		return OutputKind::Stream;

	const char *id = obs_output_get_id(output);
	if (id && strcmp(id, "replay_buffer") == 0)
		return OutputKind::ReplayBuffer;
	if (id && strcmp(id, "virtualcam_output") == 0)
		return OutputKind::VirtualCam;
	if (flags & OBS_OUTPUT_ENCODED)
		return OutputKind::Recording;
	return OutputKind::Other;
}

bool enumOutput(void *param, obs_output_t *output)
{
	auto *ctx = static_cast<EnumContext *>(param);
	if (!obs_output_active(output))
		return true;

	Found f;
	f.identity = output;
	const char *name = obs_output_get_name(output);
	f.name = name ? name : "";

	if (output == ctx->stream) {
		f.kind = OutputKind::Stream;
		f.main = true;
	} else if (output == ctx->record) {
		f.kind = OutputKind::Recording;
		f.main = true;
	} else if (output == ctx->replay) {
		f.kind = OutputKind::ReplayBuffer;
	} else if (output == ctx->vcam) {
		f.kind = OutputKind::VirtualCam;
	} else {
		f.kind = classify(output);
	}

	f.weak = obs_output_get_weak_output(output);
	ctx->found.push_back(std::move(f));
	return true;
}

} // namespace

Sampler::Tracked::~Tracked()
{
	if (weak)
		obs_weak_output_release(weak);
}

Sampler::Sampler() : cpuInfo_(os_cpu_usage_info_start()), startNs_(os_gettime_ns()) {}

Sampler::~Sampler()
{
	clear();
	if (cpuInfo_)
		os_cpu_usage_info_destroy(cpuInfo_);
}

void Sampler::clear()
{
	tracked_.clear();
	snap_.others.clear();
	snap_.streaming = false;
	snap_.recording = false;
	snap_.streamHistory = nullptr;
	snap_.recordHistory = nullptr;
}

void Sampler::rescanOutputs()
{
	obs_output_t *stream = obs_frontend_get_streaming_output();
	obs_output_t *record = obs_frontend_get_recording_output();
	obs_output_t *replay = obs_frontend_get_replay_buffer_output();
	obs_output_t *vcam = obs_frontend_get_virtualcam_output();

	EnumContext ctx;
	ctx.stream = stream;
	ctx.record = record;
	ctx.replay = replay;
	ctx.vcam = vcam;
	obs_enum_outputs(enumOutput, &ctx);

	obs_output_release(stream);
	obs_output_release(record);
	obs_output_release(replay);
	obs_output_release(vcam);

	const uint64_t now = os_gettime_ns();
	for (auto &t : tracked_)
		t->seen = false;

	for (Found &f : ctx.found) {
		auto it = std::find_if(tracked_.begin(), tracked_.end(), [&f](const std::unique_ptr<Tracked> &t) {
			return t->identity == f.identity && t->stat.name == f.name;
		});
		if (it != tracked_.end()) {
			Tracked &t = **it;
			t.seen = true;
			t.main = f.main;
			t.stat.kind = f.kind;
			obs_weak_output_release(f.weak);
			continue;
		}

		auto t = std::make_unique<Tracked>();
		t->weak = f.weak;
		t->identity = f.identity;
		t->main = f.main;
		t->seen = true;
		t->startNs = now;
		t->stat.name = std::move(f.name);
		t->stat.kind = f.kind;
		tracked_.push_back(std::move(t));
	}

	tracked_.erase(std::remove_if(tracked_.begin(), tracked_.end(),
				      [](const std::unique_ptr<Tracked> &t) { return !t->seen; }),
		       tracked_.end());

	/* Video settings can change while OBS runs, so refresh them here
	 * instead of on every tick. */
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.fps_num > 0 && ovi.fps_den > 0) {
		snap_.targetFps = static_cast<double>(ovi.fps_num) / static_cast<double>(ovi.fps_den);
		snap_.budgetMs = 1000.0 * static_cast<double>(ovi.fps_den) / static_cast<double>(ovi.fps_num);
	}
}

void Sampler::sampleOutput(Tracked &t, uint64_t now)
{
	obs_output_t *output = obs_weak_output_get_output(t.weak);
	if (!output) {
		t.stat.kbps = 0.0;
		return;
	}

	/* Bitrate from the byte delta, the same way the built-in Stats dock does it. */
	const uint64_t bytes = obs_output_get_total_bytes(output);
	if (t.lastNs == 0 || bytes < t.lastBytes) {
		t.lastBytes = bytes;
		t.lastNs = now;
		t.stat.kbps = 0.0;
	} else {
		const double seconds = static_cast<double>(now - t.lastNs) / kNsPerSec;
		if (seconds >= 0.01) {
			t.stat.kbps = static_cast<double>(bytes - t.lastBytes) * 8.0 / seconds / 1000.0;
			t.lastBytes = bytes;
			t.lastNs = now;
		}
	}
	t.stat.bytes = bytes;
	t.history.push(static_cast<float>(t.stat.kbps));

	const int dropped = obs_output_get_frames_dropped(output);
	const int frames = obs_output_get_total_frames(output);
	if (t.rebase || dropped < t.droppedBase || frames < t.framesBase) {
		t.droppedBase = dropped;
		t.framesBase = frames;
		t.rebase = false;
	}
	t.stat.dropped = static_cast<uint64_t>(dropped - t.droppedBase);
	t.stat.frames = static_cast<uint64_t>(frames - t.framesBase);
	t.stat.reconnecting = obs_output_reconnecting(output);

	const bool paused = obs_output_paused(output);
	if (paused && t.pauseStartNs == 0) {
		t.pauseStartNs = now;
	} else if (!paused && t.pauseStartNs != 0) {
		t.pausedNs += now - t.pauseStartNs;
		t.pauseStartNs = 0;
	}
	t.stat.paused = paused;

	const uint64_t pausedTotal = t.pausedNs + (t.pauseStartNs ? now - t.pauseStartNs : 0);
	const uint64_t elapsed = now > t.startNs ? now - t.startNs : 0;
	t.stat.activeSec = static_cast<double>(elapsed > pausedTotal ? elapsed - pausedTotal : 0) / kNsPerSec;

	if (t.stat.kind == OutputKind::VirtualCam) {
		t.stat.width = obs_output_get_width(output);
		t.stat.height = obs_output_get_height(output);
	}

	obs_output_release(output);
}

void Sampler::sample(double gpuPercent, int64_t diskFreeBytes)
{
	const uint64_t now = os_gettime_ns();

	snap_.cpu = cpuInfo_ ? os_cpu_usage_info_query(cpuInfo_) : 0.0;
	snap_.gpu = gpuPercent;
	snap_.diskFreeBytes = diskFreeBytes;
	snap_.memoryBytes = static_cast<double>(os_get_proc_resident_size());
	snap_.sessionSec = static_cast<double>(now - startNs_) / kNsPerSec;
	snap_.fps = obs_get_active_fps();
	snap_.frameMs = static_cast<double>(obs_get_average_frame_time_ns()) / 1000000.0;

	/* Missed and skipped frames use the same baseline logic as the built-in
	 * Stats dock, including recovery when libobs counters restart. */
	const uint32_t rendered = obs_get_total_frames();
	const uint32_t lagged = obs_get_lagged_frames();
	video_t *video = obs_get_video();
	const uint32_t encoded = video ? video_output_get_total_frames(video) : 0;
	const uint32_t skipped = video ? video_output_get_skipped_frames(video) : 0;

	if (rebaseRender_ || rendered < firstRendered_ || lagged < firstLagged_) {
		firstRendered_ = rendered;
		firstLagged_ = lagged;
	}
	if (rebaseRender_ || encoded < firstEncoded_ || skipped < firstSkipped_) {
		firstEncoded_ = encoded;
		firstSkipped_ = skipped;
	}
	rebaseRender_ = false;
	snap_.rendered = rendered - firstRendered_;
	snap_.missed = lagged - firstLagged_;
	snap_.encoded = encoded - firstEncoded_;
	snap_.skipped = skipped - firstSkipped_;

	snap_.streaming = false;
	snap_.recording = false;
	snap_.streamHistory = nullptr;
	snap_.recordHistory = nullptr;
	snap_.others.clear();

	for (auto &tracked : tracked_) {
		Tracked &t = *tracked;
		sampleOutput(t, now);
		if (t.main && t.stat.kind == OutputKind::Stream) {
			snap_.streaming = true;
			snap_.stream = t.stat;
			snap_.streamHistory = &t.history;
		} else if (t.main && t.stat.kind == OutputKind::Recording) {
			snap_.recording = true;
			snap_.record = t.stat;
			snap_.recordHistory = &t.history;
		} else {
			snap_.others.push_back(t.stat);
		}
	}
}

void Sampler::resetCounters()
{
	rebaseRender_ = true;
	for (auto &t : tracked_)
		t->rebase = true;
}

void Sampler::resetRenderCounters()
{
	rebaseRender_ = true;
}

} // namespace vitals
