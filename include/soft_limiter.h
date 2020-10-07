/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2020  The dosbox-staging team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef DOSBOX_SOFT_LIMITER_H
#define DOSBOX_SOFT_LIMITER_H

/*
Soft Limiter
------------
Given an input array and output array where the input can support wider values
than the output, the limiter will detect when the input contains one or more
values that would exceed the output's type bounds.

When detected, it scale-down the entire input set such that they fit within the
output bounds. By scaling the entire series of values, it ensure relative
differences are retained without distorion. (This is known as Soft Limiting,
which is superior to Hard Limiting that truncates or clips values and causes
distortion).

This scale-down-to-fit effect continues to be applied to subsequent input sets,
each time with less effect (provided new peaks aren't detected), until the
scale-down is complete - this period is known as 'Release'.

The release duration is a function of how much we needed to scale down in the
first place.  The larger the scale-down, the longer the release (typically
ranging from 10's of milliseconds to low-hundreds for > 2x overages).

Use:

The SoftLimiter reads and writes arrays of the same length, which is
defined during object creation as a template size. For example:

  SoftLimiter<48> limiter;

You can then repeatedly call:
  limiter.Apply(in_samples, out_samples);

Where in_samples is a std::array<float, 48> and out_samples is a 
std::array<int16_t, 48>. The limiter will either copy or limit the in_samples
into the out_samples array.

The PrintStats function will make mixer suggestions if the in_samples
were (at most) 40% under the allowed bounds, in which case the recommendation
will describe how to scale up the channel amplitude. 

On the other hand, if the limiter found itself scaling down more than 10% of
the inbound stream, then it will in turn recommend how to scale down the
channel. 
*/

#include "dosbox.h"
#include "logging.h"
#include "mixer.h"
#include "support.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <cmath>
#include <string>

template <size_t array_frames>
class SoftLimiter {
public:
	SoftLimiter() = delete;
	SoftLimiter(const std::string &name, const AudioFrame &scale);

	using in_array_t = std::array<float, array_frames * 2>; // 2 for stereo
	using out_array_t = std::array<int16_t, array_frames * 2>; // 2 for stereo
	using in_array_iterator_t = typename std::array<float, array_frames * 2>::iterator;
	using out_array_iterator_t = typename std::array<int16_t, array_frames * 2>::iterator;

	const out_array_t &Apply(in_array_t &in, uint16_t frames) noexcept;
	const AudioFrame &GetPeaks() const noexcept { return peak; }
	void PrintStats() const;
	void Reset() noexcept;

private:
	// Amplitude level constants
	using out_limits = std::numeric_limits<int16_t>;
	constexpr static float upper_bound = static_cast<float>(out_limits::max());
	constexpr static float lower_bound = static_cast<float>(out_limits::min());

	static constexpr size_t LEFT = 0;
	static constexpr size_t RIGHT = 1;
	void FindPeaks(in_array_t &stream, uint16_t frames) noexcept;
	void Release() noexcept;
	void SaveTailFrame(const uint16_t req_frames) noexcept;

	void ScaleSide(in_array_iterator_t in_pos,
	               const in_array_iterator_t in_pos_last,
	               out_array_iterator_t out_pos,
	               const float poly_add,
	               const float poly_mult) noexcept;
	void TriageSignal(in_array_t &in,
	                  out_array_t &out,
	                  const size_t side,
	                  float &local,
	                  const in_array_iterator_t last_pos,
	                  const float pre,
	                  float &existing,
	                  const float add,
	                  float &scale);
	out_array_t out;
	std::string channel_name = {};
	const AudioFrame &prescale;      // scale before operating on the stream
	AudioFrame limit_scale = {1, 1}; // real-time limit applied to stream
	AudioFrame peak = {1, 1};        // holds real-time peak amplitudes
	AudioFrame tail = {0, 0}; // holds the prior sequence's tail frame
	in_array_iterator_t peak_pos_left = nullptr;
	in_array_iterator_t peak_pos_right = nullptr;
	int limited_ms = 0;     // milliseconds that needed limiting
	int non_limited_ms = 0; // milliseconds that didn't need limiting
	uint16_t remaining_incr_left = 0;
	uint16_t remaining_incr_right = 0;
};

template <size_t array_frames>
SoftLimiter<array_frames>::SoftLimiter(const std::string &name, const AudioFrame &scale)
        : channel_name(name),
          prescale(scale)
{
	static_assert(array_frames > 0, "need some quantity of frames");
	static_assert(array_frames < 16384, "too many frames adds audible latency");
}

template <size_t array_frames>
void SoftLimiter<array_frames>::ScaleSide(in_array_iterator_t in_pos,
                                          const in_array_iterator_t in_pos_last,
                                          out_array_iterator_t out_pos,
                                          const float poly_add,
                                          const float poly_mult) noexcept
{
	while (in_pos <= in_pos_last) {
		const auto adjusted = poly_add + (*in_pos) * poly_mult;
		*out_pos = static_cast<int16_t>(adjusted);
		out_pos += 2;
		in_pos += 2;
	}
}

template <size_t array_frames>
void SoftLimiter<array_frames>::TriageSignal(in_array_t &in,
                                             out_array_t &out,
                                             const size_t side,
                                             float &local,
                                             const in_array_iterator_t last_pos,
                                             const float pre,
                                             float &existing,
                                             const float add,
                                             float &scale)
{
	local *= pre;

	// New peak!
	if (local > existing && local > upper_bound) {
		existing = local;
		const auto mult = (upper_bound - add) / (local - add);
		// Scale from the previous tail up to the local peak
		// using a polynomial
		ScaleSide(in.begin() + side, last_pos, out.begin() + side, add, mult);

		// Scale from after the local peak to the end of the
		// sequence using a pure scalar
		scale = upper_bound / existing;
		ScaleSide(last_pos + side + 2, in.end() - (side + 2),
		          out.begin() + (last_pos - in.begin()) + side + 2, 0,
		          scale);
	} else if (existing > upper_bound) {
		scale = upper_bound / existing;
		ScaleSide(in.begin() + side, in.end() - (side + 2),
		          out.begin() + side, 0, scale);
	} else {
		ScaleSide(in.begin() + side, in.end() - (side + 2),
		          out.begin() + side, 0, 1.0f);
	}
}

template <size_t array_frames>
void SoftLimiter<array_frames>::FindPeaks(in_array_t &in, const uint16_t samples) noexcept
{
	auto pos = in.begin();
	const auto pos_end = in.begin() + samples;
	AudioFrame local_peak{};

	while (pos < pos_end) {
		const auto val_left = fabsf(*pos);
		if (val_left > local_peak.left) {
			local_peak.left = val_left;
			peak_pos_left = pos;
		}
		++pos;
		const auto val_right = fabsf(*pos);
		if (val_right > local_peak.right) {
			local_peak.right = val_right;
			peak_pos_right = pos;
		}
		++pos;
	}
	TriageSignal(in, out, LEFT, local_peak.left, peak_pos_left,
	             prescale.left, peak.left, tail.left, limit_scale.left);
	TriageSignal(in, out, RIGHT, local_peak.right, peak_pos_right,
	             prescale.right, peak.right, tail.right, limit_scale.right);
}

template <size_t array_frames>
void SoftLimiter<array_frames>::SaveTailFrame(const uint16_t req_frames) noexcept
{
	if (req_frames) {
		const size_t offset = (req_frames - 1) * 2;
		tail.left = static_cast<float>(out[offset]);
		tail.right = static_cast<float>(out[offset + 1]);
	} else {
		tail = {0, 0};
	}
}

template <size_t array_frames>
const typename SoftLimiter<array_frames>::out_array_t &SoftLimiter<array_frames>::Apply(
        in_array_t &in, const uint16_t req_frames) noexcept
{
	// Ensure the buffers are large enough to handle the request
	const uint16_t samples = req_frames * 2; // left and right channels
	assert(samples <= in.size());

	FindPeaks(in, samples);
	SaveTailFrame(req_frames);
	Release();
	return out;
}

template <size_t array_frames>
void SoftLimiter<array_frames>::Release() noexcept
{
	++limited_ms;
	// Decrement the peak(s) one step
	constexpr float delta_db = 0.002709201f; // 0.0235 dB increments
	constexpr float release_amplitude = upper_bound * delta_db;
	if (peak.left > upper_bound)
		peak.left -= release_amplitude;
	if (peak.right > upper_bound)
		peak.right -= release_amplitude;
	// LOG_MSG("GUS: releasing peak_amplitude = %.2f | %.2f",
	//         static_cast<double>(peak.left),
	//         static_cast<double>(peak.right));
}

template <size_t array_frames>
void SoftLimiter<array_frames>::PrintStats() const
{
	constexpr auto ms_per_minute = 1000 * 60;
	const auto ms_total = static_cast<double>(limited_ms) + non_limited_ms;
	const auto minutes_total = ms_total / ms_per_minute;

	// Only print stats if we have more than 30 seconds of data
	if (minutes_total < 0.5)
		return;

	// Only print levels if the peak is at-least 5% of the max
	const auto peak_sample = std::max(peak.left, peak.right);
	constexpr auto five_percent_of_max = upper_bound / 20;
	if (peak_sample < five_percent_of_max)
		return;

	// It's expected and normal for multi-channel audio to periodically
	// accumulate beyond the max, which the soft-limiter gracefully handles.
	// More importantly, users typically care when their overall stream
	// never achieved the maximum levels.
	auto peak_ratio = peak_sample / upper_bound;
	peak_ratio = std::min(peak_ratio, 1.0f);
	LOG_MSG("%s: Peak amplitude reached %.0f%% of max",
	        channel_name.c_str(), 100 * static_cast<double>(peak_ratio));

	// Make a suggestion if the peak amplitude was well below 3 dB. Note that
	// we remove the effect of the external scale by dividing by it. This
	// avoids making a recommendation if the user delibertely wanted quiet
	// output (as an example).
	const auto scale = std::max(prescale.left, prescale.right);
	constexpr auto well_below_3db = static_cast<float>(0.6);
	if (peak_ratio / scale < well_below_3db) {
		const auto suggested_mix_val = 100 * scale / peak_ratio;
		LOG_MSG("%s: If it should be louder, use: mixer %s %.0f",
		        channel_name.c_str(), channel_name.c_str(),
		        static_cast<double>(suggested_mix_val));
	}
	// Make a suggestion if more than 20% of the stream required limiting
	const auto time_ratio = limited_ms / (ms_total + 1); // one ms avoids div-by-0
	if (time_ratio > 0.2) {
		const auto minutes_limited = static_cast<double>(limited_ms) / ms_per_minute;
		const auto reduction_ratio = 1 - time_ratio / 2;
		const auto suggested_mix_val = 100 * reduction_ratio * static_cast<double>(scale);
		LOG_MSG("%s: %.1f%% or %.2f of %.2f minutes needed limiting, consider: mixer %s %.0f",
		        channel_name.c_str(), 100 * time_ratio, minutes_limited,
		        minutes_total, channel_name.c_str(), suggested_mix_val);
	}
}

template <size_t array_frames>
void SoftLimiter<array_frames>::Reset() noexcept
{
	peak = {1, 1};
	limited_ms = 0;
	non_limited_ms = 0;
}

#endif
