// Standalone check for the deleted-message restore window.
// Build from the repository root:
// c++ -std=c++20 -ITelegram/SourceFiles \
//   Telegram/SourceFiles/ayu/data/deleted_restore.cpp \
//   Telegram/SourceFiles/test/test_deleted_restore.cpp
#include "ayu/data/deleted_restore.h"

#include <cstdlib>
#include <iostream>

namespace {

int gFailures = 0;

void Expect(bool ok, const char *name) {
	if (ok) {
		return;
	}
	std::cerr << "FAIL " << name << '\n';
	++gFailures;
}

using AyuDeletedRestore::Bounds;
using AyuDeletedRestore::IdInBounds;
using AyuDeletedRestore::NewerSliceBounds;
using AyuDeletedRestore::OlderSliceBounds;
using AyuDeletedRestore::kOpenEndedMessageId;

bool Same(Bounds bounds, int lower, int upper) {
	return bounds.valid && bounds.lower == lower && bounds.upper == upper;
}

} // namespace

int main() {
	Expect(
		Same(OlderSliceBounds({ 100, 150 }, 0, 0, 0, true), 100, kOpenEndedMessageId),
		"bottom page keeps messages deleted after the newest survivor");
	Expect(
		Same(OlderSliceBounds({ 70, 90 }, 100, 200, 0, true), 70, 99),
		"scrolling up fills the gap under the loaded page");
	Expect(
		Same(OlderSliceBounds({ 70, 90 }, 0, 0, 0, false), 70, 90),
		"a middle page only covers its own ids");
	Expect(
		Same(OlderSliceBounds({}, 10, 40, 0, false), 1, 9),
		"hitting the top pulls in older saved messages");
	Expect(
		Same(OlderSliceBounds({}, 0, 0, 0, true), 1, kOpenEndedMessageId),
		"an empty chat at the bottom still shows saved messages");
	Expect(
		!OlderSliceBounds({}, 0, 0, 0, false).valid,
		"an empty unloaded page adds nothing");
	Expect(
		Same(OlderSliceBounds({ 70, 90 }, 0, 0, 100, false), 70, 99),
		"the id above a sparse page closes the gap");
	Expect(
		Same(NewerSliceBounds({}, 150, 0, true), 151, kOpenEndedMessageId),
		"the bottom edge keeps messages newer than the last server one");
	Expect(
		Same(NewerSliceBounds({ 110, 120 }, 100, 0, false), 101, 120),
		"loading downward fills the gap above the loaded page");
	Expect(
		IdInBounds(150, { 100, kOpenEndedMessageId, true }),
		"a deleted id inside the window is restored");
	Expect(
		!IdInBounds(50, { 100, kOpenEndedMessageId, true }),
		"a deleted id above the loaded page waits for that page");

	if (gFailures) {
		std::cerr << gFailures << " failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
