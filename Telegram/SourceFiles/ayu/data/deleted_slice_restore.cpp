// This file is part of AyuGram Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/deleted_slice_restore.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/deleted_restore.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "api/api_text_entities.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <algorithm>

namespace {

constexpr auto kFlagOut = 0x00000002;
constexpr auto kFlagPost = 0x00004000;
constexpr auto kRestoreLimit = 10000;

[[nodiscard]] int NarrowId(int64 bare) {
	if (bare <= 0 || bare > AyuDeletedRestore::kOpenEndedMessageId) {
		return 0;
	}
	return int(bare);
}

[[nodiscard]] std::vector<int> CollectIds(const QVector<MTPMessage> &slice) {
	auto ids = std::vector<int>();
	ids.reserve(slice.size());
	for (const auto &message : slice) {
		if (const auto id = NarrowId(IdFromMessage(message).bare)) {
			ids.push_back(id);
		}
	}
	return ids;
}

[[nodiscard]] std::vector<int> CollectIds(const std::vector<MsgId> &messageIds) {
	auto ids = std::vector<int>();
	ids.reserve(messageIds.size());
	for (const auto &id : messageIds) {
		if (const auto narrow = NarrowId(id.bare)) {
			ids.push_back(narrow);
		}
	}
	return ids;
}

[[nodiscard]] MTPMessage BuildRestoredMessage(
		not_null<History*> history,
		const DeletedMessage &message) {
	using Flag = MTPDmessage::Flag;
	auto from = PeerId(0);
	const auto outgoing = (message.flags & kFlagOut) != 0;
	const auto post = (message.flags & kFlagPost) != 0;
	if (outgoing) {
		from = history->session().userPeerId();
	} else if (!post && message.fromId > 0) {
		from = peerFromUser(UserId(BareId(message.fromId)));
	}

	auto entities = MTPVector<MTPMessageEntity>();
	if (!message.textEntities.empty()) {
		entities = AyuMapper::deserializeTextWithEntities(message.textEntities);
	}
	const auto flags = (outgoing ? (Flag::f_out | Flag::f_from_id) : Flag(0))
		| (post ? Flag::f_post : Flag(0))
		| ((!outgoing && from) ? Flag::f_from_id : Flag(0))
		| (entities.v.empty() ? Flag(0) : Flag::f_entities);

	const auto text = QString::fromStdString(message.text);
	return MTP_message(
		MTP_flags(flags),
		MTP_int(message.messageId),
		from ? peerToMTP(from) : MTPPeer(),
		MTPint(),
		MTPstring(),
		peerToMTP(history->peer->id),
		MTPPeer(),
		MTPMessageFwdHeader(),
		MTPlong(),
		MTPlong(),
		MTPPeer(),
		MTPMessageReplyHeader(),
		MTP_int(message.date),
		MTP_string(text),
		MTPMessageMedia(),
		MTPReplyMarkup(),
		entities,
		MTPint(),
		MTPint(),
		MTPMessageReplies(),
		MTPint(),
		MTPstring(),
		MTPlong(),
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(),
		MTPint(),
		MTPlong(),
		MTPFactCheck(),
		MTPint(),
		MTPlong(),
		MTPSuggestedPost(),
		MTPint(),
		MTPstring(),
		MTPRichMessage());
}

[[nodiscard]] std::vector<DeletedMessage> LoadSaved(
		not_null<History*> history,
		AyuDeletedRestore::Bounds bounds) {
	if (!bounds.valid || !AyuSettings::getInstance().saveDeletedMessages()) {
		return {};
	}
	const auto userId = ID(history->peer->session().userId().bare
		& PeerId::kChatTypeMask);
	const auto dialogId = getDialogIdFromPeer(history->peer);
	const auto minExclusive = (bounds.lower <= 1)
		? ID(0)
		: ID(bounds.lower - 1);
	const auto maxExclusive = (bounds.upper >= AyuDeletedRestore::kOpenEndedMessageId)
		? ID(0)
		: ID(bounds.upper + 1);
	return AyuDatabase::getDeletedMessagesInRange(
		userId,
		dialogId,
		0,
		minExclusive,
		maxExclusive,
		kRestoreLimit);
}

[[nodiscard]] base::flat_set<int> IdSet(const std::vector<int> &ids) {
	auto result = base::flat_set<int>();
	result.reserve(ids.size());
	for (const auto id : ids) {
		result.emplace(id);
	}
	return result;
}

} // namespace

namespace AyuMessages {

PreparedDeletedSlice PrepareSliceWithDeleted(
		not_null<History*> history,
		const QVector<MTPMessage> &slice,
		bool loadingNewer,
		bool reachesBottom) {
	auto result = PreparedDeletedSlice();
	result.messages = slice;
	const auto ids = CollectIds(slice);
	const auto existingMin = NarrowId(history->minMsgId().bare);
	const auto existingMax = NarrowId(history->maxMsgId().bare);
	const auto bounds = loadingNewer
		? AyuDeletedRestore::NewerSliceBounds(
			ids,
			existingMax,
			0,
			reachesBottom)
		: AyuDeletedRestore::OlderSliceBounds(
			ids,
			existingMin,
			existingMax,
			0,
			reachesBottom);
	auto present = IdSet(ids);
	for (const auto &saved : LoadSaved(history, bounds)) {
		if (saved.messageId <= 0 || !present.emplace(saved.messageId).second) {
			continue;
		}
		if (const auto existing = history->owner().message(
				history->peer->id,
				MsgId(saved.messageId))) {
			if (!existing->isDeleted() || existing->mainView()) {
				continue;
			}
		}
		result.messages.push_back(BuildRestoredMessage(history, saved));
		result.restoredIds.emplace(MsgId(saved.messageId));
	}
	if (!result.restoredIds.empty()) {
		std::sort(
			result.messages.begin(),
			result.messages.end(),
			[](const MTPMessage &a, const MTPMessage &b) {
				return IdFromMessage(a) > IdFromMessage(b);
			});
	}
	return result;
}

void RestoreDeletedIntoMessageIds(
		not_null<History*> history,
		std::vector<MsgId> &messageIds,
		MsgRange &noSkipRange,
		MsgId aroundId) {
	const auto ids = CollectIds(messageIds);
	const auto reachesBottom = history->loadedAtBottom()
		|| (noSkipRange.till == ServerMaxMsgId)
		|| (aroundId.bare > AyuDeletedRestore::kOpenEndedMessageId);
	const auto adjacentAbove = (!reachesBottom
			&& aroundId.bare > 0
			&& aroundId.bare <= AyuDeletedRestore::kOpenEndedMessageId)
		? int(aroundId.bare)
		: 0;
	const auto existingMin = NarrowId(history->minMsgId().bare);
	const auto existingMax = NarrowId(history->maxMsgId().bare);
	const auto loadingNewer = adjacentAbove > 0
		&& !ids.empty()
		&& adjacentAbove < *std::min_element(ids.begin(), ids.end());
	const auto bounds = loadingNewer
		? AyuDeletedRestore::NewerSliceBounds(
			ids,
			existingMax,
			adjacentAbove,
			reachesBottom)
		: AyuDeletedRestore::OlderSliceBounds(
			ids,
			existingMin,
			existingMax,
			adjacentAbove,
			reachesBottom);
	auto present = IdSet(ids);
	auto restoredMin = 0;
	auto restoredMax = 0;
	for (const auto &saved : LoadSaved(history, bounds)) {
		if (saved.messageId <= 0 || !present.emplace(saved.messageId).second) {
			continue;
		}
		const auto id = MsgId(saved.messageId);
		if (const auto existing = history->owner().message(history->peer->id, id)) {
			if (!existing->isDeleted()) {
				continue;
			}
		} else if (const auto item = history->owner().addNewMessage(
				BuildRestoredMessage(history, saved),
				MessageFlags(),
				NewMessageType::Existing)) {
			item->setDeleted();
		} else {
			continue;
		}
		messageIds.push_back(id);
		if (!restoredMin || saved.messageId < restoredMin) {
			restoredMin = saved.messageId;
		}
		if (saved.messageId > restoredMax) {
			restoredMax = saved.messageId;
		}
	}
	if (restoredMin) {
		if (!noSkipRange.from || noSkipRange.from > restoredMin) {
			noSkipRange.from = restoredMin;
		}
		if (noSkipRange.till < restoredMax) {
			noSkipRange.till = restoredMax;
		}
	}
	if (reachesBottom) {
		noSkipRange.till = ServerMaxMsgId;
	}
}

} // namespace AyuMessages
