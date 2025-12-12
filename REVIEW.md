## Your Task: Review-Fix Loop with Reviewer Process after Implementation

You must always perform at least one final review with the Reviewer model via `veda -S $VEDA_SESSION -p reviewer` after all implementation work is complete.  
That final review must run recursively in a Review → Fix → Review loop until Reviewer is satisfied that no further changes are needed.  
You must not use Reviewer mid-implementation; Reviewer is for the final review phase only.

During implementation, you should keep the work on track via mid-implementation validation such as build/compilation/test-style checks (using the tools you have), not via Reviewer.

### Escaping Backticks in Prompts (Critical)

**Backticks in double-quoted prompts get evaluated by bash as command substitution.** If your prompt contains code examples with backticks, they will be executed as commands:

```bash
# BAD - double quotes evaluate backticks:
veda -p reviewer "Check if `getData()` handles errors"
# Results in: sh: getData(): command not found

# GOOD - use single quotes (simplest):
veda -S $VEDA_SESSION -p reviewer 'Check if `getData()` handles errors correctly.'

# GOOD - escape backticks in double quotes:
veda -p reviewer "Check if \`getData()\` handles errors"
```

**Recommendation:** Use single quotes (`'...'`) for prompts containing backticks. If you need variable expansion, escape backticks with backslash in double quotes.

### Session Isolation (Critical for Multi-Agent)

**Always use `-S $VEDA_SESSION`** (or set `VEDA_SESSION` env var) to isolate your selection from other concurrent agents. Each agent should have a unique session ID.

```bash
# Set session ID (stable per shell, unique per terminal)
export VEDA_SESSION="${VEDA_SESSION:-agent-$$}"
# Or pass explicitly: veda -S my-session ...
```

### Reviewer Model notes

* Receives final review requests via `veda -S $VEDA_SESSION -p reviewer`.
* Reviews with diffs included in selection (save diff to file, add to selection).
* Reviews the completed implementation, flags issues with priority tags [P0]-[P3], and provides an overall verdict.
* Must be used at least once at the end of implementation for a final, holistic review.
* May be called multiple times during the final review phase (Review → Fix → Review loop) but must not be used mid-implementation.
* Does not perform edits.

### Review-Fix Loop with Reviewer (Mandatory)

After implementation is complete and mid-implementation validation is done, you must run a recursive final review loop with Reviewer.
There must be at least one final review request, and you must loop until Reviewer is satisfied.

1. Final review request

Call Reviewer to initiate final review of the completed implementation:

* Save the diff: `git diff > /tmp/changes.diff`
* Build selection with diff + relevant source files via `veda -S $VEDA_SESSION sel add`
* Send review request via `veda -S $VEDA_SESSION -p reviewer`

Example final review request:

```bash
# Save diff
git diff > /tmp/changes.diff

# Build selection with diff and key files
veda -S $VEDA_SESSION sel clear
veda -S $VEDA_SESSION sel add /tmp/changes.diff
veda -S $VEDA_SESSION sel add src/changed_file.c src/related.c include/header.h

# Check token count
veda -S $VEDA_SESSION sel ls

# Request review
veda -S $VEDA_SESSION -p reviewer "Final Review Request: Implementation for this task is complete.
Overall summary of changes:
- Added X feature
- Modified Y to handle Z
- Updated config for W

Key files are selected. Please perform a holistic review checking for correctness, integration issues, regressions, and adherence to codebase patterns."
```

2. Recursive Review → Fix → Review loop

You must then enter a loop:

* While Reviewer indicates there are issues (verdict: "patch is incorrect" or has [P0]/[P1] findings):

  * Interpret the feedback as specific fixes or changes.
  * Use your native editing tools to address each point.
  * After applying fixes, update the diff and call Reviewer again:

Example follow-up final review:

```bash
# Update diff after fixes
git diff > /tmp/changes.diff

# Request re-review (selection already has the files, just update diff)
veda -S $VEDA_SESSION -p reviewer "Final Review Follow-up: I have addressed your feedback:
- Fixed X (P1 issue)
- Added handling for Y
- Updated Z as suggested

Please re-review and confirm whether any issues remain or if the implementation is now acceptable."

# Or use session-scoped resume to continue same conversation
veda -S $VEDA_SESSION resume "Fixed the P1 issues. Please re-review."
```

Repeat this Final Review → Fix → Final Review cycle until:

* Reviewer's verdict is "patch is correct", or
* Reviewer explicitly indicates no remaining [P0]/[P1] issues, or
* Reviewer clearly states the changes are approved.

You must not end the task until Reviewer has reached this satisfied state.


## Reminders

Make sure to onboard yourself with veda at `~/.pi/agent/docs/veda.md` before acting.
Key commands:
- `git diff > /tmp/changes.diff` to capture changes
- `veda -S $VEDA_SESSION sel clear` then `veda -S $VEDA_SESSION sel add` to build context
- `veda -S $VEDA_SESSION sel ls` to verify selection and token count  
- `veda -S $VEDA_SESSION -p reviewer` for review requests (medium reasoning, read-only sandbox)
- `veda -S $VEDA_SESSION resume` to continue the review conversation (session-scoped)
- Look for verdict: "patch is correct" or "patch is incorrect"
- Address all [P0] and [P1] issues before considering done
- Output goes to stdout; use `-o file.md` to save response
- **Always use `-S $VEDA_SESSION`** to avoid conflicts with other agents
