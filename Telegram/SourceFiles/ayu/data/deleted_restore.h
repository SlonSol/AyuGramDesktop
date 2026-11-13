// This file is part of AyuGram Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <vector>

namespace AyuDeletedRestore {

constexpr int kOpenEndedMessageId = 0x7FFFFFFF;

struct Bounds {
	int lower = 0;
	int upper = 0;
	bool valid = false;
};

[[nodiscard]] Bounds OlderSliceBounds(
	const std::vector<int> &sliceIds,
	int existingMin,
	int existingMax,
	int adjacentAbove,
	bool reachesBottom);

[[nodiscard]] Bounds NewerSliceBounds(
	const std::vector<int> &sliceIds,
	int existingMax,
	int adjacentBelow,
	bool reachesBottom);

[[nodiscard]] bool IdInBounds(int id, Bounds bounds);

} // namespace AyuDeletedRestore
