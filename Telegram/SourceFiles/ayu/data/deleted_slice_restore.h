// This file is part of AyuGram Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "history/history.h"

namespace AyuMessages {

struct PreparedDeletedSlice {
	QVector<MTPMessage> messages;
	base::flat_set<MsgId> restoredIds;
};

[[nodiscard]] PreparedDeletedSlice PrepareSliceWithDeleted(
	not_null<History*> history,
	const QVector<MTPMessage> &slice,
	bool loadingNewer,
	bool reachesBottom);

void RestoreDeletedIntoMessageIds(
	not_null<History*> history,
	std::vector<MsgId> &messageIds,
	MsgRange &noSkipRange,
	MsgId aroundId);

} // namespace AyuMessages
