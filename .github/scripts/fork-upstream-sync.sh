#!/usr/bin/env bash
# Merge AyuGram/AyuGramDesktop dev into this fork.
# A clean merge is pushed to dev. A conflict is left on an upstream-sync/* branch
# and reported with a pull request (or an issue if the pull request cannot be opened).
# A GITHUB_TOKEN push does not start other workflows, so a clean sync then
# dispatches the macOS build explicitly.

set -euo pipefail

UPSTREAM_REPO="${UPSTREAM_REPO:-AyuGram/AyuGramDesktop}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-dev}"
TARGET_BRANCH="${TARGET_BRANCH:-dev}"
FORK_REPO="${FORK_REPO:-${GITHUB_REPOSITORY:-}}"
MACOS_WORKFLOW="${MACOS_WORKFLOW:-fork-macos-build.yml}"
upstream_url="https://github.com/${UPSTREAM_REPO}.git"

if [[ "${FORK_REPO}" != "SlonSol/AyuGramDesktop" ]]; then
	echo "Refusing to sync: this script only runs in SlonSol/AyuGramDesktop (got '${FORK_REPO}')."
	exit 1
fi

cd "$(git rev-parse --show-toplevel)"

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

export GIT_TERMINAL_PROMPT=0
export GH_REPO="${FORK_REPO}"

if git remote get-url upstream >/dev/null 2>&1; then
	git remote set-url upstream "${upstream_url}"
else
	git remote add upstream "${upstream_url}"
fi

git fetch --no-tags upstream \
	"+refs/heads/${UPSTREAM_BRANCH}:refs/remotes/upstream/${UPSTREAM_BRANCH}"

upstream_sha="$(git rev-parse "upstream/${UPSTREAM_BRANCH}")"
echo "Upstream ${UPSTREAM_REPO}@${UPSTREAM_BRANCH} is ${upstream_sha}."
echo "Fork ${TARGET_BRANCH} is $(git rev-parse HEAD)."

write_summary() {
	if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
		printf '%s\n' "$1" >> "${GITHUB_STEP_SUMMARY}"
	fi
}

if git merge-base --is-ancestor "${upstream_sha}" HEAD; then
	echo "SYNC_STATUS=uptodate"
	write_summary "dev already contains upstream ${upstream_sha}. No push."
	exit 0
fi

conflict_file="$(mktemp)"
report_conflicts() {
	local branch="$1"
	local title="Upstream sync has conflicts (${branch})"
	local body_file
	body_file="$(mktemp)"
	{
		echo "The weekly sync from ${upstream_url} (\`${UPSTREAM_BRANCH}\`) did not merge cleanly into \`${TARGET_BRANCH}\`."
		echo
		echo "Nothing was pushed to \`${TARGET_BRANCH}\`."
		echo
		echo "Upstream commit: \`${upstream_sha}\`"
		echo "Branch with that upstream commit: \`${branch}\`"
		echo
		echo "Conflicting paths:"
		echo
		if [[ -s "${conflict_file}" ]]; then
			sed 's/^/- /' "${conflict_file}"
		else
			echo "- (git did not list any files; see the workflow log)"
		fi
		echo
		echo "Submodule pointers are in this list when both sides moved the same submodule."
		echo "Resolve the conflicts locally, including those submodule pointers, then push \`${TARGET_BRANCH}\`."
		echo
		echo "The macOS build was not started."
	} > "${body_file}"

	if gh pr create --base "${TARGET_BRANCH}" --head "${branch}" --title "${title}" --body-file "${body_file}"; then
		return 0
	fi
	echo "Could not open a pull request. Opening an issue instead."
	gh issue create --title "${title}" --body-file "${body_file}"
}

merge_log="$(mktemp)"
if git merge --no-edit "upstream/${UPSTREAM_BRANCH}" >"${merge_log}" 2>&1; then
	cat "${merge_log}"
	updated=0
	for attempt in 1 2 3; do
		if git submodule sync --recursive && git submodule update --init --recursive; then
			updated=1
			break
		fi
		echo "Submodule update failed (attempt ${attempt} of 3)."
		sleep $((attempt * 10))
	done
	if [[ "${updated}" != 1 ]]; then
		echo "SYNC_STATUS=submodule"
		write_summary "Merge commit was not pushed: a submodule could not be fetched."
		exit 1
	fi
	if ! git diff --quiet || ! git diff --cached --quiet; then
		echo "SYNC_STATUS=submodule"
		echo "Submodule update left the worktree dirty. Not pushing."
		git status --short
		write_summary "Merge commit was not pushed: submodule update changed the worktree."
		exit 1
	fi

	if ! git push origin "HEAD:refs/heads/${TARGET_BRANCH}"; then
		echo "SYNC_STATUS=push-failed"
		title="Upstream sync could not push to dev"
		body_file="$(mktemp)"
		{
			echo "A clean merge of \`${upstream_sha}\` was prepared, but pushing it to \`${TARGET_BRANCH}\` was rejected."
			echo
			echo "dev was not changed. If dev is protected, allow the GitHub Actions bot to push, or merge the result by hand."
			echo "The macOS build was not started."
		} > "${body_file}"
		existing="$(gh issue list --state open --search "in:title ${title}" --json number --jq '.[0].number')"
		if [[ -n "${existing}" && "${existing}" != "null" ]]; then
			gh issue comment "${existing}" --body-file "${body_file}"
		else
			gh issue create --title "${title}" --body-file "${body_file}"
		fi
		exit 1
	fi

	echo "SYNC_STATUS=merged"
	write_summary "Pushed the merge of ${upstream_sha} to ${TARGET_BRANCH}. Starting the macOS build."

	# The ref has to be visible before the other workflow checks it out.
	# workflow_dispatch is one of the events a GITHUB_TOKEN push is allowed to start.
	sleep "${SYNC_DISPATCH_DELAY:-10}"
	for attempt in 1 2 3 4 5; do
		if gh workflow run "${MACOS_WORKFLOW}" --ref "${TARGET_BRANCH}"; then
			exit 0
		fi
		echo "workflow_dispatch failed (attempt ${attempt} of 5)."
		sleep $((attempt * 5))
	done
	echo "The merge is on ${TARGET_BRANCH}, but the macOS workflow did not start."
	exit 1
fi

cat "${merge_log}" >&2
if git rev-parse -q --verify MERGE_HEAD >/dev/null; then
	git diff --name-only --diff-filter=U > "${conflict_file}" || true
	git merge --abort
else
	cp "${merge_log}" "${conflict_file}"
fi

date_stamp="$(date -u +%Y-%m-%d)"
branch="upstream-sync/${date_stamp}"
if git ls-remote --exit-code --heads origin "${branch}" >/dev/null 2>&1; then
	branch="upstream-sync/${date_stamp}-${GITHUB_RUN_ID:-local}"
fi

git push origin "${upstream_sha}:refs/heads/${branch}"
echo "SYNC_STATUS=conflicts"
echo "Conflicts. Upstream commit is on ${branch}."
write_summary "dev was not updated. Upstream ${upstream_sha} is on \`${branch}\`."
report_conflicts "${branch}"
