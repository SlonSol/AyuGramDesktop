// This file is part of AyuGram Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/deleted_restore.h"

#include <algorithm>

namespace AyuDeletedRestore {

Bounds OlderSliceBounds(
		const std::vector<int> &sliceIds,
		int existingMin,
		int existingMax,
		int adjacentAbove,
		bool reachesBottom) {
	if (sliceIds.empty()) {
		auto ceiling = 0;
		if (existingMin > 1) {
			ceiling = existingMin - 1;
		} else if (adjacentAbove > 1
			&& adjacentAbove <= kOpenEndedMessageId) {
			ceiling = adjacentAbove - 1;
		}
		if (ceiling > 0) {
			return { 1, ceiling, true };
		}
		if (reachesBottom) {
			return { 1, kOpenEndedMessageId, true };
		}
		return {};
	}

	const auto lower = *std::min_element(sliceIds.begin(), sliceIds.end());
	auto upper = *std::max_element(sliceIds.begin(), sliceIds.end());
	if (existingMin > upper) {
		upper = existingMin - 1;
	} else if (adjacentAbove > upper
		&& adjacentAbove <= kOpenEndedMessageId) {
		upper = adjacentAbove - 1;
	} else if (reachesBottom && (existingMax <= 0 || upper >= existingMax)) {
		upper = kOpenEndedMessageId;
	}
	if (lower > upper) {
		return {};
	}
	return { lower, upper, true };
}

Bounds NewerSliceBounds(
		const std::vector<int> &sliceIds,
		int existingMax,
		int adjacentBelow,
		bool reachesBottom) {
	if (sliceIds.empty()) {
		const auto floor = (existingMax > 0) ? existingMax : adjacentBelow;
		if (reachesBottom && floor > 0 && floor < kOpenEndedMessageId) {
			return { floor + 1, kOpenEndedMessageId, true };
		}
		return {};
	}

	auto lower = *std::min_element(sliceIds.begin(), sliceIds.end());
	auto upper = *std::max_element(sliceIds.begin(), sliceIds.end());
	if (existingMax > 0 && existingMax < lower) {
		lower = existingMax + 1;
	} else if (adjacentBelow > 0 && adjacentBelow < lower) {
		lower = adjacentBelow + 1;
	}
	if (reachesBottom && (existingMax <= 0 || upper >= existingMax)) {
		upper = kOpenEndedMessageId;
	}
	if (lower > upper) {
		return {};
	}
	return { lower, upper, true };
}

bool IdInBounds(int id, Bounds bounds) {
	return bounds.valid && id >= bounds.lower && id <= bounds.upper;
}

} // namespace AyuDeletedRestore
